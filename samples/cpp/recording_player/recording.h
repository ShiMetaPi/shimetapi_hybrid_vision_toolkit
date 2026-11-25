#pragma once
#include <shimetapi/core/event_cd.h>
#include <shimetapi/io/hybrid_reader.h>
#include <opencv2/core.hpp>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

// Index packets rather than cache an image per EVS frame. This preserves gaps
// and empty packets, and bounds memory for high-rate board recordings.
class EventRecording {
public:
    explicit EventRecording(const std::string& path);
    cv::Mat render(double seconds, double window_seconds);
    double duration() const { return duration_; }
    void print() const;
private:
    struct Packet { uint64_t offset, time_us; uint32_t size; };
    std::ifstream file_;
    std::vector<Packet> packets_;
    std::vector<Shimeta::EventCD> plain_;
    std::vector<uint8_t> bytes_;
    unsigned width_=640, height_=1024;
    uint64_t events_=0, invalid_coords_=0, bad_packets_=0, skipped_bytes_=0;
    uint64_t regressions_=0, first_us_=0;
    double duration_=0;
    std::string format_;
    void paint(cv::Mat& image, unsigned x, unsigned y, bool on, double age, double window);
};

// SDK AVI supports raw Gray8/NV12 and its custom tsmp chunks. Seeking backwards
// reopens the sequential reader instead of caching every uncompressed image.
class ApsRecording {
public:
    explicit ApsRecording(const std::string& path);
    cv::Mat frameAt(double seconds);
    double duration() const { return times_.empty() ? double(count_)/fps_ : times_.back()+1/fps_; }
    bool hasCaptureTiming() const { return !times_.empty(); }
    double fps() const { return fps_; }
    void print() const;
private:
    std::string path_;
    Shimeta::io::HybridReader reader_;
    uint64_t count_=0, next_=0, cached_index_=UINT64_MAX;
    double fps_=30;
    cv::Mat cached_;
    std::vector<double> times_;
};
