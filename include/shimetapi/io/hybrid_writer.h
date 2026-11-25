#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>
#include <shimetapi/core/evs_timestamp.h>
#include <shimetapi/core/frame.h>
#include <shimetapi/core/pixel_format.h>
#include <shimetapi/io/event_writer.h>

namespace Shimeta::io {

// One recording facade: EVS is stored as RAW and packed APS (NV12 or Gray8) as AVI.
class HybridWriter {
public:
    ~HybridWriter();
    /// aps_format 决定 AVI 像素格式与帧字节长（NV12=w*h*3/2，默认；Gray8=w*h，014）。
    bool open(const std::string& evs_path, const std::string& aps_path,
              uint32_t width, uint32_t height, RawFormat evs_format = RawFormat::Evt3,
              double aps_fps = 30.0, PixelFormat aps_format = PixelFormat::NV12);
    /// 双尺寸版：014 的 EVS 头（1280×1024）与 APS AVI（640×1024）尺寸不同。
    bool open(const std::string& evs_path, const std::string& aps_path,
              uint32_t evs_w, uint32_t evs_h, uint32_t aps_w, uint32_t aps_h,
              RawFormat evs_format = RawFormat::Evt3, double aps_fps = 30.0,
              PixelFormat aps_format = PixelFormat::NV12);
    bool writeFrame(const Shimeta::Frame& frame, const Shimeta::EvsTimestamp* evs_ts = nullptr);
    // Finalize constant-rate AVI timing from the measured recording window.
    // Call after the last frame and before close(); frame payloads are unchanged.
    bool setApsPlaybackDuration(double seconds);
    void close();
    uint32_t apsFrameCount() const { return aps_frames_; }

private:
    static constexpr uint32_t kAviTsmpMagic   = 0x31535645u; // "EVS1"
    static constexpr uint32_t kAviTsmpVersion  = 1;
    struct IndexEntry { uint32_t offset, size; };
    EventWriter evs_;
    std::ofstream avi_;
    std::string aps_path_;
    std::vector<IndexEntry> index_;
    uint32_t riff_pos_ = 0, avih_frames_pos_ = 0, strh_length_pos_ = 0;
    uint32_t movi_pos_ = 0, movi_data_ = 0, frame_bytes_ = 0, aps_frames_ = 0;
    uint32_t aps_width_ = 0, aps_height_ = 0;
    PixelFormat aps_format_ = PixelFormat::NV12;   // AVI 像素格式（open 时定档）
    double aps_fps_ = 30.0;

    uint32_t pos();
    void fourcc(const char (&s)[5]);
    void le16(uint16_t v);
    void le32(uint32_t v);
    void le64(uint64_t v);
    void patch(uint32_t p, uint32_t v);
    bool openAvi(const std::string& path, uint32_t width, uint32_t height);
    bool writeAps(const Shimeta::Frame& frame, const Shimeta::EvsTimestamp* evs_ts);
    void closeAvi();
};

} // namespace Shimeta::io
