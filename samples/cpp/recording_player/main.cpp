// Desktop playback inspired by EVS_Ubuntu_QT's sample_evs_player/player_window:
// timestamp pacing, red/blue fading events, APS overlay and a common timeline.
// Uses the SDK codecs/AVI reader and OpenCV; no camera or Qt runtime required.
#include "recording.h"
#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace {
const char* title="HV Recording Player";
struct Options {
    std::string raw,avi,output,snapshot;
    double speed=1, fps=30, at=0, trail=.10, aps_offset=0;
    bool check=false;
};
void usage(const char* exe) {
    std::cout<<"Usage: "<<exe<<" events.raw [images.avi] [options]\n"
      "  --check                Validate recordings without a window\n"
      "  --snapshot image.png   Save a preview without a window\n"
      "  --at SECONDS           Initial/preview position (default 0)\n"
      "  --export output.avi    Export side-by-side MJPEG, no display needed\n"
      "  --fps FPS              Display/export FPS (default 30)\n"
      "  --speed FACTOR         Playback speed, 0.1..4 (default 1)\n"
      "  --trail-ms MS          EVS accumulation/decay window (default 100)\n"
      "  --aps-offset SECONDS   Shift APS start later on the shared timeline\n"
      "Keys: Space pause; a/d previous/next display frame; r restart;\n"
      "      +/- speed; m side-by-side/overlay/EVS/APS; q/ESC quit.\n";
}
double number(const std::string& s) {
    size_t used=0; double n=std::stod(s,&used);
    if(used!=s.size() || !std::isfinite(n)) throw std::runtime_error("Invalid numeric value: "+s);
    return n;
}
Options options(int argc,char** argv) {
    Options o;
    if(argc<2) throw std::runtime_error("RAW recording is required");
    o.raw=argv[1];
    for(int i=2;i<argc;++i) {
        const std::string arg=argv[i];
        if(arg=="--check") {o.check=true;continue;}
        if(arg.rfind("--",0)!=0) {
            if(!o.avi.empty()) throw std::runtime_error("Unexpected argument: "+arg);
            o.avi=arg; continue;
        }
        if(i+1>=argc) throw std::runtime_error("Missing value for "+arg);
        const std::string value=argv[++i];
        if(arg=="--export") o.output=value;
        else if(arg=="--snapshot") o.snapshot=value;
        else if(arg=="--at") o.at=number(value);
        else if(arg=="--fps") o.fps=number(value);
        else if(arg=="--speed") o.speed=number(value);
        else if(arg=="--trail-ms") o.trail=number(value)/1000;
        else if(arg=="--aps-offset") o.aps_offset=number(value);
        else throw std::runtime_error("Unknown option: "+arg);
    }
    if(o.at<0 || o.speed<.1 || o.speed>4 || o.fps<1 || o.fps>120 || o.trail<=0 || o.trail>5)
        throw std::runtime_error("Invalid position, speed, FPS or trail window");
    if(int(o.check)+int(!o.output.empty())+int(!o.snapshot.empty())>1)
        throw std::runtime_error("Choose one of --check, --export or --snapshot");
    return o;
}

cv::Mat fit(const cv::Mat& image,int width=500,int height=720) {
    cv::Mat canvas(height,width,CV_8UC3,cv::Scalar(20,20,20));
    if(image.empty()) return canvas;
    const double scale=std::min(double(width)/image.cols,double(height)/image.rows);
    cv::Mat scaled;
    cv::resize(image,scaled,cv::Size(std::max(1,int(image.cols*scale)),std::max(1,int(image.rows*scale))),0,0,cv::INTER_NEAREST);
    scaled.copyTo(canvas(cv::Rect((width-scaled.cols)/2,(height-scaled.rows)/2,scaled.cols,scaled.rows)));
    return canvas;
}

cv::Mat compose(EventRecording& evs,ApsRecording* aps,double time,int mode,const Options& o,bool paused) {
    cv::Mat events=evs.render(time,o.trail), image;
    if(aps && time>=o.aps_offset) image=aps->frameAt(time-o.aps_offset);
    cv::Mat view;
    if(mode==0) cv::hconcat(fit(events),fit(image),view);
    else if(mode==1 && !image.empty()) {
        cv::Mat overlay;
        cv::resize(image,overlay,events.size());
        overlay*=.65;
        for(int y=0;y<events.rows;++y) {
            const auto* p=events.ptr<cv::Vec3b>(y); auto* dst=overlay.ptr<cv::Vec3b>(y);
            for(int x=0;x<events.cols;++x) if(p[x][0] || p[x][2]) dst[x]=p[x];
        }
        view=fit(overlay,1000);
    } else view=fit(mode==3?image:events,1000);
    cv::Mat canvas(view.rows+100,view.cols,CV_8UC3,cv::Scalar(25,25,25));
    view.copyTo(canvas(cv::Rect(0,40,view.cols,view.rows)));
    const char* modes[]={"EVS (OFF blue / ON red)                 APS", "EVS + APS overlay", "EVS", "APS"};
    cv::putText(canvas,modes[mode],{15,27},cv::FONT_HERSHEY_SIMPLEX,.65,{230,230,230},1);
    std::ostringstream info;
    info<<std::fixed<<std::setprecision(3)<<time<<" s   "<<o.speed<<"x   "<<(paused?"PAUSED":"PLAYING");
    cv::putText(canvas,info.str(),{15,view.rows+65},cv::FONT_HERSHEY_SIMPLEX,.6,{230,230,230},1);
    cv::putText(canvas,"Space pause | a/d step | r restart | +/- speed | m view | q exit",
                {15,view.rows+88},cv::FONT_HERSHEY_SIMPLEX,.45,{190,190,190},1);
    return canvas;
}

struct Timeline { bool updating=false; int requested=-1; };
void onSeek(int position,void* data) {
    auto& timeline=*static_cast<Timeline*>(data);
    if(!timeline.updating) timeline.requested=position;
}
}

int main(int argc,char** argv) {
    if(argc==2 && std::string(argv[1])=="--help") {usage(argv[0]);return 0;}
    try {
        Options o=options(argc,argv);
        for(const auto& output:{o.output,o.snapshot}) {
            if(output.empty()) continue;
            const auto resolved=std::filesystem::weakly_canonical(output);
            if(resolved==std::filesystem::weakly_canonical(o.raw) ||
               (!o.avi.empty() && resolved==std::filesystem::weakly_canonical(o.avi)))
                throw std::runtime_error("Output must not overwrite an input recording");
        }
        EventRecording evs(o.raw);
        std::unique_ptr<ApsRecording> aps;
        if(!o.avi.empty()) aps=std::make_unique<ApsRecording>(o.avi);
        evs.print(); if(aps) aps->print();
        const double duration=std::max(evs.duration(),aps?aps->duration()+o.aps_offset:0.0);
        if(duration<=0 || duration>86400) throw std::runtime_error("Invalid recording duration (>24h); check timestamps");
        std::cout<<"Timeline: EVS packet timestamps; APS uses capture sidecar when present, otherwise AVI FPS; starts aligned"
                 <<" (APS offset "<<o.aps_offset<<"s).\n"
                 <<"No common capture timestamps are assumed: alignment is approximate, not hardware synchronization.\n";
        if(o.at>=duration) throw std::runtime_error("--at is beyond the recording duration");
        if(o.check) {
            // Exercise actual display conversions, too, without initializing GTK.
            compose(evs,aps.get(),std::min(duration/2,duration-.000001),0,o,true);
            std::cout<<"Recording check passed.\n";return 0;
        }
        if(!o.snapshot.empty()) {
            if(!cv::imwrite(o.snapshot,compose(evs,aps.get(),o.at,0,o,true)))
                throw std::runtime_error("Cannot write preview image");
            std::cout<<"Saved preview: "<<o.snapshot<<"\n";return 0;
        }
        if(!o.output.empty()) {
            cv::Mat first=compose(evs,aps.get(),o.at,0,o,false);
            cv::VideoWriter writer(o.output,cv::VideoWriter::fourcc('M','J','P','G'),o.fps,first.size());
            if(!writer.isOpened()) throw std::runtime_error("Cannot open MJPEG AVI output");
            uint64_t frames=0;
            for(double t=o.at;t<duration;t=o.at+double(++frames)*o.speed/o.fps)
                writer.write(compose(evs,aps.get(),t,0,o,false));
            writer.release();
            std::cout<<"Exported "<<frames<<" frames to "<<o.output<<"\n";return 0;
        }
        if(!std::getenv("DISPLAY") && !std::getenv("WAYLAND_DISPLAY"))
            throw std::runtime_error("No desktop display. Run from a desktop terminal or SSH -X; --check/--snapshot/--export work without DISPLAY.");
        cv::namedWindow(title,cv::WINDOW_NORMAL);
        cv::resizeWindow(title,1000,850);
        Timeline timeline;
        const int ticks=std::max(1,int(std::ceil(duration*1000)));
        cv::createTrackbar("Time (ms)",title,nullptr,ticks,onSeek,&timeline);
        bool paused=false; int mode=0;
        bool window_was_visible=false;
        double position=o.at;
        auto previous=std::chrono::steady_clock::now();
        while(true) {
            auto now=std::chrono::steady_clock::now();
            if(!paused) position+=std::chrono::duration<double>(now-previous).count()*o.speed;
            previous=now;
            if(timeline.requested>=0) {position=timeline.requested/1000.0;timeline.requested=-1;}
            if(position>=duration) {position=std::max(0.0,duration-.000001);paused=true;}
            position=std::max(0.0,position);
            cv::imshow(title,compose(evs,aps.get(),position,mode,o,paused));
            timeline.updating=true;
            cv::setTrackbarPos("Time (ms)",title,std::min(ticks,int(position*1000)));
            timeline.updating=false;
            const int key=cv::waitKeyEx(std::max(1,int(1000/o.fps)));
            // X11 forwarding may report an unmapped window during startup.
            const double visible=cv::getWindowProperty(title,cv::WND_PROP_VISIBLE);
            if(visible>=1) window_was_visible=true;
            if(key==27 || key=='q' || (window_was_visible && visible==0)) break;
            if(key==' ') paused=!paused;
            else if(key=='a' || key=='d') {paused=true;position+=key=='a'?-1/o.fps:1/o.fps;}
            else if(key=='r') position=0;
            else if(key=='+' || key=='=') o.speed=std::min(4.0,o.speed*2);
            else if(key=='-') o.speed=std::max(.1,o.speed/2);
            else if(key=='m') mode=(mode+1)%4;
        }
        cv::destroyAllWindows();return 0;
    } catch(const std::exception& e) {
        std::cerr<<"recording_player: "<<e.what()<<"\n";return 1;
    }
}
