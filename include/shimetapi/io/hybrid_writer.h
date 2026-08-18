#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>
#include <shimetapi/core/evs_timestamp.h>
#include <shimetapi/core/frame.h>
#include <shimetapi/io/event_writer.h>

namespace Shimeta::io {

// One recording facade: EVS is stored as RAW and packed NV12 APS as AVI.
class HybridWriter {
public:
    ~HybridWriter();
    bool open(const std::string& evs_path, const std::string& aps_path,
              uint32_t width, uint32_t height, RawFormat evs_format = RawFormat::Evt3,
              double aps_fps = 30.0);
    bool writeFrame(const Shimeta::Frame& frame, const Shimeta::EvsTimestamp* evs_ts = nullptr);
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
