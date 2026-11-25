#include "recording.h"
#include <shimetapi/codec/evs_packet_codec.h>
#include <shimetapi/io/event_reader.h>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <filesystem>
#include <sstream>

EventRecording::EventRecording(const std::string& path):file_(path,std::ios::binary) {
    namespace c=Shimeta::codec;
    if(!file_) throw std::runtime_error("Cannot open RAW: "+path);
    file_.seekg(0,std::ios::end);
    const uint64_t end=uint64_t(file_.tellg());
    file_.seekg(0);
    uint64_t start=0;
    std::string header_format;
    while(file_.peek()=='%' && start<65536) {
        std::string line;
        // Bound line length, including corrupt or unterminated text headers.
        char ch=0;
        while(file_.get(ch) && ch!='\n') {
            if(line.size()>=4096) throw std::runtime_error("RAW header line too long");
            line.push_back(ch);
        }
        if(!line.empty() && line.back()=='\r') line.pop_back();
        unsigned w=0,h=0;
        if(std::sscanf(line.c_str(),"%% format %*[^;];width=%u;height=%u",&w,&h)==2) {
            width_=w; height_=h;
        }
        if(line.find("EVT2")!=std::string::npos) header_format="EVT2";
        else if(line.find("EVT3")!=std::string::npos) header_format="EVT3";
        if(!file_) throw std::runtime_error("Truncated RAW header");
        start=uint64_t(file_.tellg());
        if(line=="% end") break;
    }
    if(!width_ || !height_ || width_>4096 || height_>4096)
        throw std::runtime_error("Invalid RAW image geometry");
    uint8_t probe[4]{};
    file_.read(reinterpret_cast<char*>(probe),4);
    file_.clear(); file_.seekg(std::streamoff(start));
    // Current board files say EVT3 in the text header, but contain DVS1 packets.
    // Content detection takes priority over that historical container label.
    if(!c::evsPacketLooksLikeHeader(probe) && header_format=="EVT2") {
        format_="standard EVT2 (Qt recorder)";
        Shimeta::io::EventReader reader;
        if(!reader.open(path) || !reader.readAllEvents(plain_))
            throw std::runtime_error("EVT2 contains no decodable events");
        bool backwards=false;
        for(size_t i=1;i<plain_.size();++i) backwards |= plain_[i].t<plain_[i-1].t;
        if(backwards) throw std::runtime_error("Non-monotonic EVT2 timestamps");
        first_us_=uint64_t(plain_.front().t);
        duration_=double(plain_.back().t-plain_.front().t)/1e6+1.0/600;
        for(const auto& e:plain_) {
            if(e.x<width_ && e.y<height_) ++events_; else ++invalid_coords_;
        }
        return;
    }
    format_="APX014 DVS1 / EVT2 payload";
    uint64_t offset=start, last_time=0;
    std::vector<uint64_t> gaps;
    uint32_t last_frame=0;
    bool first=true;
    c::Evs014PacketDecoder decoder;
    std::vector<Shimeta::EventCD> decoded;
    while(offset+c::kEvsHdrLen<=end) {
        bytes_.resize(c::kEvsHdrLen);
        file_.clear(); file_.seekg(std::streamoff(offset));
        file_.read(reinterpret_cast<char*>(bytes_.data()),c::kEvsHdrLen);
        const size_t size=c::evsPacketSize(bytes_.data(),bytes_.size());
        if(!size) {
            ++offset; ++skipped_bytes_; continue;
        }
        if(offset+size>end) { ++offset; ++skipped_bytes_; continue; }
        bytes_.resize(size);
        file_.read(reinterpret_cast<char*>(bytes_.data()+c::kEvsHdrLen),size-c::kEvsHdrLen);
        c::EvsPacketHeader h;
        if(!c::parseEvsPacket(bytes_.data(),size,h) || h.timestamp_usec>=1000000) {
            ++bad_packets_; ++offset; ++skipped_bytes_; continue;
        }
        uint64_t time=uint64_t(h.timestamp_sec)*1000000+h.timestamp_usec;
        if(first) { first_us_=time; last_time=time; first=false; }
        if(time<last_time) { ++regressions_; time=last_time; }
        if(time>last_time && h.reserved!=last_frame) gaps.push_back(time-last_time);
        last_time=time; last_frame=h.reserved;
        packets_.push_back({offset,time-first_us_,uint32_t(size)});
        decoded.clear();
        decoder.Decode(bytes_.data(),bytes_.size(),decoded);
        decoder.Flush(decoded); // Preserve packet timestamps and empty-packet indexing.
        for(const auto& event:decoded) {
            if(event.x<width_ && event.y<height_) ++events_; else ++invalid_coords_;
        }
        offset+=size;
    }
    skipped_bytes_+=end-offset;
    if(packets_.empty()) throw std::runtime_error("No valid DVS1 packets found; expected current APX014 board recording or standard EVT2");
    uint64_t tail=1667;
    if(!gaps.empty()) { auto mid=gaps.begin()+gaps.size()/2; std::nth_element(gaps.begin(),mid,gaps.end()); tail=*mid; }
    duration_=double(packets_.back().time_us+tail)/1e6;
}

void EventRecording::paint(cv::Mat& image,unsigned x,unsigned y,bool on,double age,double window) {
    if(x>=width_ || y>=height_) return;
    // Same polarity as EVS_Ubuntu_QT: OFF blue, ON red, with fading trails.
    const auto intensity=uint8_t(std::clamp(255.0*std::exp(-3.0*std::max(0.0,age)/window),0.0,255.0));
    image.at<cv::Vec3b>(int(y),int(x))=on ? cv::Vec3b(0,0,intensity):cv::Vec3b(intensity,0,0);
}

cv::Mat EventRecording::render(double seconds,double window) {
    cv::Mat image=cv::Mat::zeros(int(height_),int(width_),CV_8UC3);
    const uint64_t target=uint64_t(std::max(0.0,seconds)*1e6);
    const uint64_t begin=target>uint64_t(window*1e6)?target-uint64_t(window*1e6):0;
    if(!plain_.empty()) {
        auto it=std::lower_bound(plain_.begin(),plain_.end(),first_us_+begin,
            [](const Shimeta::EventCD& e,uint64_t t){return uint64_t(e.t)<t;});
        for(;it!=plain_.end() && uint64_t(it->t)<=first_us_+target;++it)
            paint(image,it->x,it->y,it->polarity,seconds-double(uint64_t(it->t)-first_us_)/1e6,window);
        return image;
    }
    Shimeta::codec::Evs014PacketDecoder decoder;
    std::vector<Shimeta::EventCD> decoded;
    auto it=std::lower_bound(packets_.begin(),packets_.end(),begin,
        [](const Packet& p,uint64_t t){return p.time_us<t;});
    for(;it!=packets_.end() && it->time_us<=target;++it) {
        bytes_.resize(it->size);
        file_.clear(); file_.seekg(std::streamoff(it->offset));
        if(!file_.read(reinterpret_cast<char*>(bytes_.data()),it->size))
            throw std::runtime_error("RAW changed or became unreadable during playback");
        decoded.clear();
        decoder.Decode(bytes_.data(),bytes_.size(),decoded);
        decoder.Flush(decoded);
        for(const auto& event:decoded)
            paint(image,event.x,event.y,event.polarity,
                  seconds-double(it->time_us)/1e6,window);
    }
    return image;
}

void EventRecording::print() const {
    std::cout<<"EVS: "<<format_<<", "<<width_<<"x"<<height_<<", packets="<<packets_.size()
             <<", events="<<events_<<", out-of-range="<<invalid_coords_
             <<", rejected-packets="<<bad_packets_<<", skipped-bytes="<<skipped_bytes_
             <<", timestamp-regressions="<<regressions_<<", duration="<<duration_<<"s\n";
}

ApsRecording::ApsRecording(const std::string& path):path_(path) {
    if(!reader_.open("",path)) throw std::runtime_error("Cannot read SDK Gray8/NV12 AVI: "+path);
    fps_=reader_.apsFps();
    // Read all frames once to validate real frame count, not just AVI metadata.
    Shimeta::Frame frame;
    while(reader_.readApsFrame(frame)) ++count_;
    if(!count_) throw std::runtime_error("AVI contains no valid image frames");
    const std::string timing_path=path+".timestamps.csv";
    if (std::filesystem::exists(timing_path)) {
        std::ifstream timing(timing_path);
        std::string line;
        if (!std::getline(timing,line) || line!="frame,aps_ts_ns,fnv1a64")
            throw std::runtime_error("Invalid APS timestamp header");
        int64_t first=0, previous=0;
        while (std::getline(timing,line)) {
            std::istringstream row(line);
            uint64_t index; int64_t stamp; char comma;
            if (!(row>>index>>comma) || comma!=',' || !(row>>stamp>>comma) || comma!=',' ||
                index!=times_.size() || stamp<=0 || (!times_.empty() && stamp<=previous))
                throw std::runtime_error("Invalid or non-increasing APS timestamp row");
            if (times_.empty()) first=stamp;
            times_.push_back(double(stamp-first)/1e9); previous=stamp;
        }
        if (times_.size()!=count_) throw std::runtime_error("APS timestamp count does not match AVI; check copied files");
    }
    if(!reader_.open("",path)) throw std::runtime_error("Cannot reopen AVI");
}

cv::Mat ApsRecording::frameAt(double seconds) {
    const uint64_t index=times_.empty()
        ? std::min(count_-1,uint64_t(std::max(0.0,seconds)*fps_))
        : uint64_t(std::upper_bound(times_.begin(),times_.end(),std::max(0.0,seconds))-times_.begin()-1);
    if(index==cached_index_) return cached_;
    if(index<next_) {
        if(!reader_.open("",path_)) throw std::runtime_error("Cannot reopen AVI");
        next_=0;
    }
    while(next_<=index) {
        Shimeta::Frame f;
        if(!reader_.readApsFrame(f)) throw std::runtime_error("AVI became unreadable during playback");
        if(next_==index) {
            if(f.format==Shimeta::PixelFormat::Gray8) {
                cv::Mat gray(f.height,f.width,CV_8UC1,const_cast<uint8_t*>(f.aps.data));
                cv::cvtColor(gray,cached_,cv::COLOR_GRAY2BGR);
            } else {
                cv::Mat nv12(f.height*3/2,f.width,CV_8UC1,const_cast<uint8_t*>(f.aps.data));
                cv::cvtColor(nv12,cached_,cv::COLOR_YUV2BGR_NV12);
            }
        }
        ++next_;
    }
    cached_index_=index;
    return cached_;
}

void ApsRecording::print() const {
    std::cout<<"APS: "<<reader_.width()<<"x"<<reader_.height()<<", format="
             <<(reader_.apsFormat()==Shimeta::PixelFormat::Gray8?"Gray8":"NV12")
             <<", frames="<<count_<<", fps="<<fps_<<", duration="<<duration()<<"s"
             <<", timing="<<(times_.empty()?"AVI nominal FPS":"APS capture timestamps")<<"\n";
}
