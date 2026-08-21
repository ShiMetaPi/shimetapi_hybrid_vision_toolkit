/**
 * @file player_widgets.cpp
 * @brief HV Player 数据/回放/UI 辅助层实现。
 *        详见 player_widgets.h。所有符号位于命名空间 hv_player。
 */
#include "player_widgets.h"

#include <shimetapi/codec/mipi_raw8_codec.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

namespace hv_player {

// ============================================================================
// UI 输入共享状态（定义）
// ============================================================================
std::mutex            g_ui_mutex;
std::vector<UiButton> g_ui_buttons;
std::atomic<int>      g_pending_action{static_cast<int>(UiAction::None)};

// ============================================================================
// VideoReader
// ============================================================================
bool VideoReader::open(const std::string& path, double fallback_fps) {
    reader_ = std::make_unique<Shimeta::io::HybridReader>();
    if (!reader_->open("", path)) return false;
    fps_ = reader_->apsFps();
    total_frames_ = reader_->apsFrameCount();
    fallback_fps_ = fallback_fps > 0.0 ? fallback_fps : 30.0;
    current_index_ = 0;
    return true;
}
double VideoReader::fps() const { return fps_ > 0.0 ? fps_ : fallback_fps_; }
uint64_t VideoReader::totalFrameCount() const { return total_frames_; }

/** @brief 顺序读到目标索引，NV12 原始字节 → BGR（工具包零 OpenCV 依赖）。 */
bool VideoReader::readFrameAt(uint64_t target_index, cv::Mat& frame, Shimeta::EvsTimestamp* timestamp) {
    if (target_index < current_index_) return false;
    while (current_index_ <= target_index) {
        Shimeta::Frame f;
        Shimeta::EvsTimestamp ts;
        if (!reader_->readApsFrame(f, &ts)) return false;
        uint8_t* base = const_cast<uint8_t*>(f.aps.data);
        cv::Mat y(f.height, f.width, CV_8UC1, base);
        cv::Mat uv(f.height / 2, f.width / 2, CV_8UC2, base + size_t(f.width) * f.height);
        cv::cvtColorTwoPlane(y, uv, current_frame_, cv::COLOR_YUV2BGR_NV12);
        current_timestamp_ = ts;
        ++current_index_;
    }
    frame = current_frame_.clone();
    if (timestamp) *timestamp = current_timestamp_;
    return !frame.empty();
}

// ============================================================================
// ApsFrameCache
// ============================================================================
bool ApsFrameCache::open(const std::string& path, double fallback_fps) {
    frames_.clear(); timestamps_.clear();
    return reader_.open(path, fallback_fps);
}
double ApsFrameCache::fps() const { return reader_.fps(); }
size_t ApsFrameCache::frameCount() const {
    uint64_t total = reader_.totalFrameCount();
    return total > 0 ? size_t(total) : frames_.size();
}
bool ApsFrameCache::frameAt(uint64_t index, cv::Mat& frame, uint64_t* actual_index) {
    while (frames_.size() <= index) {
        cv::Mat next;
        Shimeta::EvsTimestamp ts;
        if (!reader_.readFrameAt(frames_.size(), next, &ts)) break;
        frames_.push_back(next.clone());
        timestamps_.push_back(ts);
    }
    if (frames_.empty()) return false;
    size_t clamped = std::min<size_t>(size_t(index), frames_.size() - 1);
    if (actual_index) *actual_index = uint64_t(clamped);
    frame = frames_[clamped].clone();
    return !frame.empty();
}
Shimeta::EvsTimestamp ApsFrameCache::timestampAt(uint64_t index) const {
    if (timestamps_.empty()) return {};
    size_t clamped = std::min<size_t>(size_t(index), timestamps_.size() - 1);
    return timestamps_[clamped];
}
size_t ApsFrameCache::cachedFrameCount() const { return frames_.size(); }

// ============================================================================
// EvsFrameSequence
// ============================================================================
/** @brief 用公有 HybridReader 读 EVS 原始字节，MipiRaw8 逐包解码，每 4 子帧合成 1 极性帧。 */
bool EvsFrameSequence::open(const std::string& filename) {
    Shimeta::io::HybridReader reader;
    if (!reader.open(filename, "")) {
        std::cerr << "无法打开事件文件: " << filename << std::endl;
        return false;
    }
    width_ = 768;
    height_ = 608;

    Shimeta::codec::MipiRaw8Decoder decoder;
    std::vector<Shimeta::EventCD> all_events;
    int packet_count = 0;
    size_t total_bytes = 0;
    Shimeta::Frame f;
    while (reader.readEvsPacket(f)) {
        std::vector<Shimeta::EventCD> packet_events;
        decoder.Decode(f.evs.data, f.evs.size, packet_events);
        all_events.insert(all_events.end(), packet_events.begin(), packet_events.end());
        total_bytes += f.evs.size;
        ++packet_count;
    }
    std::cout << "RAW8 data size: " << total_bytes << " bytes" << std::endl;
    std::cout << "Decoded " << packet_count << " packets, "
              << all_events.size() << " events total" << std::endl;

    if (all_events.empty()) {
        std::cerr << "事件文件解码后为空" << std::endl;
        return false;
    }

    // 按时间窗口（4 个唯一时间戳 = 1 EVS 帧）累积为极性帧
    frames_.clear();
    processed_timestamps_.clear();
    cv::Mat frame(height_, width_, CV_8UC1, cv::Scalar(0));
    int64_t current_ts = all_events.front().t;
    uint64_t unique_ts_in_frame = 1;
    uint64_t frame_processed_timestamp = 0;
    bool frame_has_timestamp = false;

    for (const auto& e : all_events) {
        if (e.t != current_ts) {
            if (unique_ts_in_frame >= kEvsSubframesPerFrame) {
                frames_.push_back(frame.clone());
                processed_timestamps_.push_back(frame_has_timestamp ? frame_processed_timestamp : uint64_t(current_ts));
                frame.setTo(cv::Scalar(0));
                frame_processed_timestamp = 0;
                frame_has_timestamp = false;
                unique_ts_in_frame = 0;
            }
            ++unique_ts_in_frame;
            current_ts = e.t;
        }
        if (!frame_has_timestamp) {
            frame_processed_timestamp = uint64_t(e.t);
            frame_has_timestamp = true;
        }
        if (uint32_t(e.x) < width_ && uint32_t(e.y) < height_)
            frame.at<uint8_t>(int(e.y), int(e.x)) = e.polarity ? 1 : 2;
    }
    frames_.push_back(frame.clone());
    processed_timestamps_.push_back(frame_has_timestamp ? frame_processed_timestamp : uint64_t(current_ts));

    std::cout << "EVS frames cached: " << frames_.size() << std::endl;
    return !frames_.empty();
}
size_t EvsFrameSequence::frameCount() const { return frames_.size(); }
cv::Mat EvsFrameSequence::frameAt(size_t index, EvsColorMode mode) const {
    if (frames_.empty()) return cv::Mat();
    return renderPolarityFrame(frames_[std::min(index, frames_.size() - 1)], mode);
}
cv::Mat EvsFrameSequence::accumulatedFrameAt(size_t index, size_t count, EvsColorMode mode) const {
    if (frames_.empty()) return cv::Mat();
    cv::Mat polarity(height_, width_, CV_8UC1, cv::Scalar(0));
    size_t begin = std::min(index, frames_.size() - 1);
    size_t end = std::min(frames_.size(), begin + std::max<size_t>(1, count));
    for (size_t i = begin; i < end; ++i) {
        const cv::Mat& src = frames_[i];
        for (int y = 0; y < src.rows; ++y) {
            const uint8_t* sr = src.ptr<uint8_t>(y);
            uint8_t* dr = polarity.ptr<uint8_t>(y);
            for (int x = 0; x < src.cols; ++x)
                if (sr[x] != 0) dr[x] = sr[x];
        }
    }
    return renderPolarityFrame(polarity, mode);
}
uint64_t EvsFrameSequence::processedTimestampAt(size_t index) const {
    if (processed_timestamps_.empty()) return 0;
    return processed_timestamps_[std::min(index, processed_timestamps_.size() - 1)];
}
uint64_t EvsFrameSequence::frameIndexForTimestamp(uint64_t timestamp, EvsStepMode mode) const {
    if (processed_timestamps_.empty()) return 0;
    auto tsAt = [&](uint64_t i) { return processed_timestamps_[std::min<uint64_t>(i, uint64_t(processed_timestamps_.size() - 1))]; };
    auto deltaAt = [&](uint64_t i) { uint64_t v = tsAt(i); return v > timestamp ? v - timestamp : timestamp - v; };
    auto it = std::lower_bound(processed_timestamps_.begin(), processed_timestamps_.end(), timestamp);
    uint64_t index = 0;
    if (it == processed_timestamps_.end()) index = uint64_t(processed_timestamps_.size() - 1);
    else if (it == processed_timestamps_.begin()) index = 0;
    else {
        uint64_t n = uint64_t(std::distance(processed_timestamps_.begin(), it));
        uint64_t p = n - 1;
        index = deltaAt(p) <= deltaAt(n) ? p : n;
    }
    if (mode != EvsStepMode::Aps) return index;
    uint64_t aligned_prev = (index / kEvsPerApsFrame) * kEvsPerApsFrame;
    uint64_t aligned_next = std::min(aligned_prev + kEvsPerApsFrame,
        ((processed_timestamps_.size() - 1) / kEvsPerApsFrame) * kEvsPerApsFrame);
    return deltaAt(aligned_prev) <= deltaAt(aligned_next) ? aligned_prev : aligned_next;
}
std::pair<uint32_t, uint32_t> EvsFrameSequence::imageSize() const { return {width_, height_}; }

/** @brief 极性帧着色：mode 决定 ON/OFF 对应的颜色（蓝红或橙黄）。 */
cv::Mat EvsFrameSequence::renderPolarityFrame(const cv::Mat& polarity, EvsColorMode mode) const {
    if (polarity.empty()) return cv::Mat();
    cv::Vec3b pos = mode == EvsColorMode::BlueRed ? cv::Vec3b(255, 0, 0) : cv::Vec3b(0, 160, 255);
    cv::Vec3b neg = mode == EvsColorMode::BlueRed ? cv::Vec3b(0, 0, 255) : cv::Vec3b(0, 255, 255);
    cv::Mat dst(polarity.rows, polarity.cols, CV_8UC3, cv::Scalar(0, 0, 0));
    for (int y = 0; y < polarity.rows; ++y) {
        const uint8_t* sr = polarity.ptr<uint8_t>(y);
        cv::Vec3b* dr = dst.ptr<cv::Vec3b>(y);
        for (int x = 0; x < polarity.cols; ++x) {
            if (sr[x] == 1) dr[x] = pos;
            else if (sr[x] == 2) dr[x] = neg;
        }
    }
    return dst;
}

// ============================================================================
// TimestampSyncMap
// ============================================================================
/** @brief 加载外挂 .timestamps.csv，启用 EVS↔APS 时间戳同步。 */
bool TimestampSyncMap::open(const std::string& raw_path, const std::string& avi_path) {
    bool evs_ok = loadEvs(raw_path + ".timestamps.csv");
    bool aps_ok = loadAps(avi_path + ".timestamps.csv");
    enabled_ = aps_ok && !aps_.empty() && (aps_has_evs_raw_ts_ || (evs_ok && !evs_.empty()));
    if (enabled_) {
        base_vpf_tv_us_ = evs_.empty() ? aps_.front().vpf_tv_us : std::max(evs_.front().vpf_tv_us, aps_.front().vpf_tv_us);
        std::cout << "Timestamp sync CSV enabled." << std::endl;
    }
    return enabled_;
}
bool TimestampSyncMap::enabled() const { return enabled_; }
uint64_t TimestampSyncMap::videoIndexForEvsTs(uint64_t evs_ts_us) const {
    if (!enabled_) return 0;
    if (aps_has_evs_raw_ts_) {
        auto it = std::lower_bound(aps_.begin(), aps_.end(), evs_ts_us,
            [](const ApsEntry& e, uint64_t ts) { return e.evs_processed_ts_us < ts; });
        if (it == aps_.begin()) return it->avi_frame_index;
        if (it == aps_.end()) return aps_.back().avi_frame_index;
        auto prev = it - 1;
        uint64_t nd = it->evs_processed_ts_us > evs_ts_us ? it->evs_processed_ts_us - evs_ts_us : evs_ts_us - it->evs_processed_ts_us;
        uint64_t pd = prev->evs_processed_ts_us > evs_ts_us ? prev->evs_processed_ts_us - evs_ts_us : evs_ts_us - prev->evs_processed_ts_us;
        return pd <= nd ? prev->avi_frame_index : it->avi_frame_index;
    }
    auto evs_it = std::lower_bound(evs_.begin(), evs_.end(), evs_ts_us,
        [](const EvsEntry& e, uint64_t ts) { return e.evs_ts_us < ts; });
    const EvsEntry& evs_e = evs_it == evs_.end() ? evs_.back() : *evs_it;
    auto aps_it = std::lower_bound(aps_.begin(), aps_.end(), evs_e.vpf_tv_us,
        [](const ApsEntry& e, uint64_t v) { return e.vpf_tv_us < v; });
    if (aps_it == aps_.begin()) return aps_it->avi_frame_index;
    if (aps_it == aps_.end()) return aps_.back().avi_frame_index;
    auto prev = aps_it - 1;
    uint64_t nd = aps_it->vpf_tv_us > evs_e.vpf_tv_us ? aps_it->vpf_tv_us - evs_e.vpf_tv_us : evs_e.vpf_tv_us - aps_it->vpf_tv_us;
    uint64_t pd = prev->vpf_tv_us > evs_e.vpf_tv_us ? prev->vpf_tv_us - evs_e.vpf_tv_us : evs_e.vpf_tv_us - prev->vpf_tv_us;
    return pd <= nd ? prev->avi_frame_index : aps_it->avi_frame_index;
}
uint64_t TimestampSyncMap::apsVpfTvUsForVideoIndex(uint64_t vi) const {
    if (!enabled_) return 0;
    auto it = std::lower_bound(aps_.begin(), aps_.end(), vi,
        [](const ApsEntry& e, uint64_t i) { return e.avi_frame_index < i; });
    return (it == aps_.end() ? aps_.back() : *it).vpf_tv_us;
}

/** @brief CSV 行按逗号分割。 */
std::vector<std::string> TimestampSyncMap::split(const std::string& line) {
    std::vector<std::string> f;
    std::stringstream ss(line); std::string s;
    while (std::getline(ss, s, ',')) f.push_back(s);
    return f;
}
uint64_t TimestampSyncMap::parseU64(const std::string& s) { try { return std::stoull(s); } catch (...) { return 0; } }

/** @brief 加载 EVS 侧 .timestamps.csv（字段：vpf_tv_us, evs_ts_us）。 */
bool TimestampSyncMap::loadEvs(const std::string& path) {
    std::ifstream f(path); if (!f.is_open()) return false;
    std::string line; std::getline(f, line);
    while (std::getline(f, line)) {
        auto fields = split(line);
        if (fields.size() < 14 || parseU64(fields[13]) == 0) continue;
        EvsEntry e; e.vpf_tv_us = parseU64(fields[6]); e.evs_ts_us = parseU64(fields[12]);
        if (e.vpf_tv_us > 0) evs_.push_back(e);
    }
    return !evs_.empty();
}

/** @brief 加载 APS 侧 .timestamps.csv（字段：avi_frame_index, vpf_tv_us, evs_raw_ts, evs_processed_ts_us）。 */
bool TimestampSyncMap::loadAps(const std::string& path) {
    std::ifstream f(path); if (!f.is_open()) return false;
    std::string line; std::getline(f, line);
    std::vector<ApsEntry> entries;
    while (std::getline(f, line)) {
        auto fields = split(line);
        if (fields.size() < 8) continue;
        ApsEntry e; e.avi_frame_index = parseU64(fields[1]); e.vpf_tv_us = parseU64(fields[4]);
        if (fields.size() >= 13 && parseU64(fields[12]) != 0) {
            e.evs_raw_ts = parseU64(fields[10]);
            e.evs_processed_ts_us = parseU64(fields[11]);
            if (e.evs_processed_ts_us == 0 && e.evs_raw_ts != 0) e.evs_processed_ts_us = e.evs_raw_ts / 200;
            if (e.evs_processed_ts_us != 0) aps_has_evs_raw_ts_ = true;
        }
        if (e.vpf_tv_us > 0) entries.push_back(e);
    }
    for (const auto& e : entries) {
        if (!aps_has_evs_raw_ts_ || e.evs_processed_ts_us != 0) aps_.push_back(e);
    }
    return !aps_.empty();
}

// ============================================================================
// UI 绘制 / 辅助函数
// ============================================================================
cv::Mat makeBlank(int w, int h) { return cv::Mat::zeros(h, w, CV_8UC3); }

/** @brief 最近邻缩放（整数比 num/den，src 为空时返回空白画布）。 */
cv::Mat scaleNearest(const cv::Mat& src, int num, int den) {
    if (src.empty() || src.type() != CV_8UC3) return makeBlank(kEventDisplayWidth, kEventDisplayHeight);
    if (num <= 0 || den <= 0) return src.clone();
    int ow = std::max(1, (src.cols * num) / den);
    int oh = std::max(1, (src.rows * num) / den);
    cv::Mat dst(oh, ow, CV_8UC3);
    for (int y = 0; y < oh; ++y) {
        int sy = std::min((y * den) / num, src.rows - 1);
        const cv::Vec3b* sr = src.ptr<cv::Vec3b>(sy);
        cv::Vec3b* dr = dst.ptr<cv::Vec3b>(y);
        for (int x = 0; x < ow; ++x) dr[x] = sr[std::min((x * den) / num, src.cols - 1)];
    }
    return dst;
}
cv::Mat scaleDisplayNearest(const cv::Mat& src) { return scaleNearest(src, kDisplayScaleNumerator, kDisplayScaleDenominator); }
cv::Mat scaleHalfNearest(const cv::Mat& src) { return scaleNearest(src, 1, 2); }

/** @brief 取点阵字符位图（5×7 字模）。@param c 字符；@param row 行号 0..6。@return 行位图 byte。 */
uint8_t glyphBits(char c, int row) {
    if (row < 0 || row >= 7) return 0;
    c = char(std::toupper(unsigned(c)));
    switch (c) {
        case '0':{static constexpr uint8_t g[]={0x0E,0x11,0x13,0x15,0x19,0x11,0x0E};return g[row];}
        case '1':{static constexpr uint8_t g[]={0x04,0x0C,0x04,0x04,0x04,0x04,0x0E};return g[row];}
        case '2':{static constexpr uint8_t g[]={0x0E,0x11,0x01,0x02,0x04,0x08,0x1F};return g[row];}
        case '3':{static constexpr uint8_t g[]={0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E};return g[row];}
        case '4':{static constexpr uint8_t g[]={0x02,0x06,0x0A,0x12,0x1F,0x02,0x02};return g[row];}
        case '5':{static constexpr uint8_t g[]={0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E};return g[row];}
        case '6':{static constexpr uint8_t g[]={0x06,0x08,0x10,0x1E,0x11,0x11,0x0E};return g[row];}
        case '7':{static constexpr uint8_t g[]={0x1F,0x01,0x02,0x04,0x08,0x08,0x08};return g[row];}
        case '8':{static constexpr uint8_t g[]={0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E};return g[row];}
        case '9':{static constexpr uint8_t g[]={0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C};return g[row];}
        case 'A':{static constexpr uint8_t g[]={0x0E,0x11,0x11,0x1F,0x11,0x11,0x11};return g[row];}
        case 'B':{static constexpr uint8_t g[]={0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E};return g[row];}
        case 'C':{static constexpr uint8_t g[]={0x0E,0x11,0x10,0x10,0x10,0x11,0x0E};return g[row];}
        case 'D':{static constexpr uint8_t g[]={0x1E,0x11,0x11,0x11,0x11,0x11,0x1E};return g[row];}
        case 'E':{static constexpr uint8_t g[]={0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F};return g[row];}
        case 'F':{static constexpr uint8_t g[]={0x1F,0x10,0x10,0x1E,0x10,0x10,0x10};return g[row];}
        case 'G':{static constexpr uint8_t g[]={0x0E,0x11,0x10,0x17,0x11,0x11,0x0F};return g[row];}
        case 'H':{static constexpr uint8_t g[]={0x11,0x11,0x11,0x1F,0x11,0x11,0x11};return g[row];}
        case 'I':{static constexpr uint8_t g[]={0x0E,0x04,0x04,0x04,0x04,0x04,0x0E};return g[row];}
        case 'J':{static constexpr uint8_t g[]={0x07,0x02,0x02,0x02,0x12,0x12,0x0C};return g[row];}
        case 'K':{static constexpr uint8_t g[]={0x11,0x12,0x14,0x18,0x14,0x12,0x11};return g[row];}
        case 'L':{static constexpr uint8_t g[]={0x10,0x10,0x10,0x10,0x10,0x10,0x1F};return g[row];}
        case 'M':{static constexpr uint8_t g[]={0x11,0x1B,0x15,0x15,0x11,0x11,0x11};return g[row];}
        case 'N':{static constexpr uint8_t g[]={0x11,0x19,0x15,0x13,0x11,0x11,0x11};return g[row];}
        case 'O':{static constexpr uint8_t g[]={0x0E,0x11,0x11,0x11,0x11,0x11,0x0E};return g[row];}
        case 'P':{static constexpr uint8_t g[]={0x1E,0x11,0x11,0x1E,0x10,0x10,0x10};return g[row];}
        case 'Q':{static constexpr uint8_t g[]={0x0E,0x11,0x11,0x11,0x15,0x12,0x0D};return g[row];}
        case 'R':{static constexpr uint8_t g[]={0x1E,0x11,0x11,0x1E,0x14,0x12,0x11};return g[row];}
        case 'S':{static constexpr uint8_t g[]={0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E};return g[row];}
        case 'T':{static constexpr uint8_t g[]={0x1F,0x04,0x04,0x04,0x04,0x04,0x04};return g[row];}
        case 'U':{static constexpr uint8_t g[]={0x11,0x11,0x11,0x11,0x11,0x11,0x0E};return g[row];}
        case 'V':{static constexpr uint8_t g[]={0x11,0x11,0x11,0x11,0x0A,0x0A,0x04};return g[row];}
        case 'W':{static constexpr uint8_t g[]={0x11,0x11,0x11,0x15,0x15,0x1B,0x11};return g[row];}
        case 'X':{static constexpr uint8_t g[]={0x11,0x11,0x0A,0x04,0x0A,0x11,0x11};return g[row];}
        case 'Y':{static constexpr uint8_t g[]={0x11,0x11,0x0A,0x04,0x04,0x04,0x04};return g[row];}
        case 'Z':{static constexpr uint8_t g[]={0x1F,0x01,0x02,0x04,0x08,0x10,0x1F};return g[row];}
        case '/':{static constexpr uint8_t g[]={0x01,0x02,0x02,0x04,0x04,0x08,0x10};return g[row];}
        case ':':{static constexpr uint8_t g[]={0x00,0x04,0x04,0x00,0x04,0x04,0x00};return g[row];}
        case '.':{static constexpr uint8_t g[]={0x00,0x00,0x00,0x00,0x00,0x0C,0x0C};return g[row];}
        case '-':{static constexpr uint8_t g[]={0x00,0x00,0x00,0x1F,0x00,0x00,0x00};return g[row];}
        case '_':{static constexpr uint8_t g[]={0x00,0x00,0x00,0x00,0x00,0x00,0x1F};return g[row];}
        case '(':{static constexpr uint8_t g[]={0x02,0x04,0x08,0x08,0x08,0x04,0x02};return g[row];}
        case ')':{static constexpr uint8_t g[]={0x08,0x04,0x02,0x02,0x02,0x04,0x08};return g[row];}
        default: return 0;
    }
}

/** @brief 在 frame 上画点阵文本（5×7 字符）。@param scale 字号（像素倍数）。 */
void drawDigitText(cv::Mat& frame, int x, int y, const std::string& text,
                   const cv::Vec3b& color, int scale) {
    if (frame.empty() || frame.type() != CV_8UC3) return;
    int cx = x;
    for (char c : text) {
        if (c == ' ') { cx += 4 * scale; continue; }
        for (int row = 0; row < 7; ++row) {
            uint8_t bits = glyphBits(c, row);
            for (int col = 0; col < 5; ++col) {
                if ((bits & (1u << (4 - col))) == 0) continue;
                for (int sy = 0; sy < scale; ++sy) {
                    int py = y + row * scale + sy;
                    if (py < 0 || py >= frame.rows) continue;
                    cv::Vec3b* dr = frame.ptr<cv::Vec3b>(py);
                    for (int sx = 0; sx < scale; ++sx) {
                        int px = cx + col * scale + sx;
                        if (px >= 0 && px < frame.cols) dr[px] = color;
                    }
                }
            }
        }
        cx += 6 * scale;
    }
}
void drawPixelText(cv::Mat& frame, int x, int y, const std::string& text,
                   const cv::Vec3b& color, int scale) {
    drawDigitText(frame, x, y, text, color, scale);
}

/** @brief 实心矩形（像素级，不带 CV 坐标边界检查之外的裁剪）。 */
void fillRectPixels(cv::Mat& frame, const cv::Rect& rect, const cv::Vec3b& color) {
    int x0 = std::max(0, rect.x), y0 = std::max(0, rect.y);
    int x1 = std::min(frame.cols, rect.x + rect.width), y1 = std::min(frame.rows, rect.y + rect.height);
    for (int y = y0; y < y1; ++y) {
        cv::Vec3b* row = frame.ptr<cv::Vec3b>(y);
        for (int x = x0; x < x1; ++x) row[x] = color;
    }
}

/** @brief 矩形描边（像素级，4 条边各 1px）。 */
void strokeRectPixels(cv::Mat& frame, const cv::Rect& rect, const cv::Vec3b& color) {
    fillRectPixels(frame, cv::Rect(rect.x, rect.y, rect.width, 1), color);
    fillRectPixels(frame, cv::Rect(rect.x, rect.y + rect.height - 1, rect.width, 1), color);
    fillRectPixels(frame, cv::Rect(rect.x, rect.y, 1, rect.height), color);
    fillRectPixels(frame, cv::Rect(rect.x + rect.width - 1, rect.y, 1, rect.height), color);
}

/** @brief 画线（Bresenham，3px 粗端点）。 */
void drawLinePixels(cv::Mat& frame, cv::Point a, cv::Point b, const cv::Vec3b& color) {
    int x0 = a.x, y0 = a.y, x1 = b.x, y1 = b.y;
    int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy;
    while (true) {
        fillRectPixels(frame, cv::Rect(x0 - 1, y0 - 1, 3, 3), color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/** @brief 把 src 拷到 dst 的 (dx,dy) 处（边界裁剪，仅 CV_8UC3）。 */
void copyToCanvas(const cv::Mat& src, cv::Mat& dst, int dx, int dy) {
    if (src.empty() || src.type() != CV_8UC3 || dst.empty() || dst.type() != CV_8UC3) return;
    int rows = std::min(src.rows, dst.rows - dy);
    int cols = std::min(src.cols, dst.cols - dx);
    for (int y = 0; y < rows; ++y) {
        const cv::Vec3b* sr = src.ptr<cv::Vec3b>(y);
        cv::Vec3b* dr = dst.ptr<cv::Vec3b>(dy + y) + dx;
        for (int x = 0; x < cols; ++x) dr[x] = sr[x];
    }
}

/** @brief 由帧号 + 帧率估算回放时间戳（微秒）。 */
uint64_t apsPlaybackTimestampUs(uint64_t fi, double fps) {
    return fps > 0.0 ? uint64_t(double(fi) * 1000000.0 / fps) : 0;
}
uint64_t alignEvsFrameToApsBoundary(uint64_t fi) { return (fi / kEvsPerApsFrame) * kEvsPerApsFrame; }

/** @brief 限制 EVS 帧号范围 [0, fc) 并依 mode 对齐 APS 边界。 */
uint64_t clampEvsFrameIndex(uint64_t fi, size_t fc, EvsStepMode mode) {
    if (fc == 0) return 0;
    uint64_t clamped = std::min(fi, uint64_t(fc - 1));
    return mode != EvsStepMode::Aps ? clamped : alignEvsFrameToApsBoundary(clamped);
}

/** @brief 计算第 fi 帧在速度 spd 下的播放起始时间点（steady_clock 偏移）。 */
std::chrono::steady_clock::time_point evsStartForFrame(uint64_t fi, double spd) {
    return std::chrono::steady_clock::now() - std::chrono::microseconds(int64_t(double(fi) * 1000000.0 / (kEvsFps * spd)));
}

/** @brief 同上，APS 侧（使用 kApsFps）。 */
std::chrono::steady_clock::time_point apsStartForFrame(uint64_t fi, double spd) {
    return std::chrono::steady_clock::now() - std::chrono::microseconds(int64_t(double(fi) * 1000000.0 / (kApsFps * spd)));
}

/** @brief EVS 是否已到末尾。 */
bool isEvsAtEnd(uint64_t fi, size_t fc, EvsStepMode mode) {
    return fc > 0 && fi >= clampEvsFrameIndex(fc - 1, fc, mode);
}

/** @brief APS 是否已到已知末尾（已缓存的最后一帧）。 */
bool isApsAtKnownEnd(uint64_t fi, const ApsFrameCache& vc) {
    return vc.cachedFrameCount() > 0 && fi >= uint64_t(vc.cachedFrameCount() - 1);
}

/** @brief UiAction → 播放速度（非速度类动作返回 0）。 */
double speedForAction(UiAction a) {
    switch (a) {
        case UiAction::SpeedOneEighth: return 0.125;
        case UiAction::SpeedOneQuarter: return 0.25;
        case UiAction::SpeedOneHalf: return 0.5;
        case UiAction::SpeedOne: return 1.0;
        case UiAction::SpeedTwo: return 2.0;
        default: return 0.0;
    }
}

// ======== 侧边栏绘制 ========
/** @brief 绘制右侧信息栏（时间戳/帧号、Sync、Speed、EVS colors 按钮区）。 */
void drawSidebar(cv::Mat& canvas, int x, int height,
                 uint64_t aps_ts, const std::string& aps_ts_src,
                 uint64_t aps_fi, uint64_t evs_ts, uint64_t evs_fi,
                 EvsColorMode evs_cm, bool sync_en, double speed,
                 std::vector<UiButton>& buttons) {
    const cv::Vec3b bg(24,24,24), white(235,235,235), muted(165,165,165), accent(80,185,255);
    fillRectPixels(canvas, cv::Rect(x, 0, kSidebarWidth, height), bg);
    fillRectPixels(canvas, cv::Rect(x, 0, 1, height), cv::Vec3b(90,90,90));

    int left = x + 20, y = 42, gap = 78, lh = 24;
    auto drawBlock = [&](const std::string& label, const std::string& value, const std::string& src) {
        drawPixelText(canvas, left, y - 14, label, muted, 2); y += lh;
        drawPixelText(canvas, left, y - 14, value, white, 2); y += lh;
        if (!src.empty()) drawPixelText(canvas, left, y - 12, src, muted, 2);
        y += gap - lh * 2;
    };
    drawPixelText(canvas, left, y - 14, "TIMESTAMPS", accent, 2); y += 50;
    drawBlock("APS timestamp (us)", std::to_string(aps_ts), "source: " + aps_ts_src);
    drawBlock("APS frame", std::to_string(aps_fi), "");
    drawBlock("EVS timestamp (us)", std::to_string(evs_ts), "source: raw EVT2");
    drawBlock("EVS frame", std::to_string(evs_fi), "");

    y += 6;
    drawPixelText(canvas, left, y - 14, "Sync", muted, 2); y += 20;
    int sw = 142, sh = 30, sg = 10;
    cv::Rect syn_rect(left, y, sw, sh), fre_rect(left + sw + sg, y, sw, sh);
    auto drawOpt = [&](const cv::Rect& r, const std::string& t, bool act) {
        fillRectPixels(canvas, r, cv::Vec3b(40,40,40));
        strokeRectPixels(canvas, r, act ? cv::Vec3b(80,185,255) : cv::Vec3b(120,120,120));
        drawPixelText(canvas, r.x + 16, r.y + 8, t, white, 2);
    };
    drawOpt(syn_rect, "Sync", sync_en);
    drawOpt(fre_rect, "Free", !sync_en);
    buttons.push_back({syn_rect, UiAction::SyncOn});
    buttons.push_back({fre_rect, UiAction::SyncOff});

    y += sh + 24;
    drawPixelText(canvas, left, y - 14, "Speed", muted, 2); y += 20;
    int spw = 90, sph = 28, spg = 8;
    auto drawSpd = [&](const cv::Rect& r, const std::string& t, double sp, UiAction a) {
        bool act = std::abs(speed - sp) < 0.001;
        fillRectPixels(canvas, r, cv::Vec3b(40,40,40));
        strokeRectPixels(canvas, r, act ? cv::Vec3b(80,185,255) : cv::Vec3b(120,120,120));
        drawPixelText(canvas, r.x + 14, r.y + 7, t, white, 2);
        buttons.push_back({r, a});
    };
    drawSpd(cv::Rect(left, y, spw, sph), "1/8", 0.125, UiAction::SpeedOneEighth);
    drawSpd(cv::Rect(left + spw + spg, y, spw, sph), "1/4", 0.25, UiAction::SpeedOneQuarter);
    drawSpd(cv::Rect(left + (spw + spg) * 2, y, spw, sph), "1/2", 0.5, UiAction::SpeedOneHalf);
    y += sph + spg;
    drawSpd(cv::Rect(left, y, spw, sph), "1", 1.0, UiAction::SpeedOne);
    drawSpd(cv::Rect(left + spw + spg, y, spw, sph), "2", 2.0, UiAction::SpeedTwo);

    y += sph + 24;
    drawPixelText(canvas, left, y - 14, "EVS colors", muted, 2); y += 20;
    int ow = 142, oh = 30, og = 10;
    cv::Rect br_rect(left, y, ow, oh), oy_rect(left + ow + og, y, ow, oh);
    auto drawColor = [&](const cv::Rect& r, bool act, const cv::Vec3b& pos, const cv::Vec3b& neg) {
        fillRectPixels(canvas, r, cv::Vec3b(40,40,40));
        strokeRectPixels(canvas, r, act ? cv::Vec3b(80,185,255) : cv::Vec3b(120,120,120));
        fillRectPixels(canvas, cv::Rect(r.x+12, r.y+8, 28, 14), pos);
        fillRectPixels(canvas, cv::Rect(r.x+50, r.y+8, 28, 14), neg);
    };
    drawColor(br_rect, evs_cm == EvsColorMode::BlueRed, cv::Vec3b(255,0,0), cv::Vec3b(0,0,255));
    drawColor(oy_rect, evs_cm == EvsColorMode::OrangeYellow, cv::Vec3b(0,160,255), cv::Vec3b(0,255,255));
    buttons.push_back({br_rect, UiAction::EvsColorBlueRed});
    buttons.push_back({oy_rect, UiAction::EvsColorOrangeYellow});
}

// ======== 主画面合成 ========
/**
 * @brief 合成整幅画面：左 EVS、中 APS、右信息栏、底部进度条+播放按钮。
 * @param buttons 写到 g_ui_buttons（加锁）。
 */
cv::Mat composeSideBySide(const cv::Mat& evs_frame, const cv::Mat& video_frame,
                          uint64_t aps_ts, const std::string& aps_ts_src,
                          uint64_t aps_fi, uint64_t evs_ts, uint64_t evs_fi,
                          size_t aps_fc, size_t evs_fc,
                          EvsStepMode evs_sm, EvsColorMode evs_cm,
                          bool sync_en, double speed, bool evs_play, bool aps_play) {
    cv::Mat evs = scaleDisplayNearest(evs_frame.empty() ? makeBlank(kEventDisplayWidth, kEventDisplayHeight) : evs_frame);
    cv::Mat aps = scaleDisplayNearest(scaleHalfNearest(video_frame));
    int cw = evs.cols + aps.cols, ih = std::max(evs.rows, aps.rows);
    int w = cw + kSidebarWidth, h = std::max(ih + kBottomBarHeight, kMinSidebarHeight);
    int cy = std::max(0, (h - (ih + kBottomBarHeight)) / 2);
    int by = cy + ih;
    cv::Mat canvas(h, w, CV_8UC3, cv::Scalar(0,0,0));
    std::vector<UiButton> buttons;
    copyToCanvas(evs, canvas, 0, cy);
    copyToCanvas(aps, canvas, evs.cols, cy);
    drawSidebar(canvas, evs.cols + aps.cols, h, aps_ts, aps_ts_src, aps_fi, evs_ts, evs_fi,
                evs_cm, sync_en, speed, buttons);
    fillRectPixels(canvas, cv::Rect(0, by, cw, kBottomBarHeight), cv::Vec3b(28,28,28));
    fillRectPixels(canvas, cv::Rect(0, by, cw, 1), cv::Vec3b(90,90,90));

    auto drawProgress = [&](int px, int pw, uint64_t fi, size_t fc, const cv::Vec3b& fill) {
        if (pw <= 32) return;
        int mg = 16, bh = 6, x0 = px + mg, y0 = by + 8, bw = std::max(1, pw - mg * 2);
        fillRectPixels(canvas, cv::Rect(x0, y0, bw, bh), cv::Vec3b(48,48,48));
        strokeRectPixels(canvas, cv::Rect(x0, y0, bw, bh), cv::Vec3b(120,120,120));
        if (fc == 0) return;
        int fw = std::max(1, int((double(std::min(fi, uint64_t(fc-1)) + 1) / double(fc)) * bw));
        fillRectPixels(canvas, cv::Rect(x0, y0, std::min(fw, bw), bh), fill);
    };
    auto drawBtns = [&](int px, int pw, UiAction prev, UiAction play, UiAction next, bool playing) {
        int bw = 42, bh = 30, g = 8, tw = bw * 3 + g * 2;
        int x0 = px + std::max(0, (pw - tw) / 2), y0 = by + 36;
        cv::Rect pr(x0, y0, bw, bh), plr(x0 + bw + g, y0, bw, bh), nr(x0 + (bw + g) * 2, y0, bw, bh);
        auto drawRect = [&](const cv::Rect& r) {
            fillRectPixels(canvas, r, cv::Vec3b(40,40,40));
            strokeRectPixels(canvas, r, cv::Vec3b(255,255,255));
        };
        drawRect(pr); drawRect(plr); drawRect(nr);
        drawLinePixels(canvas, cv::Point(pr.x+26, pr.y+8), cv::Point(pr.x+16, pr.y+15), cv::Vec3b(255,255,255));
        drawLinePixels(canvas, cv::Point(pr.x+16, pr.y+15), cv::Point(pr.x+26, pr.y+22), cv::Vec3b(255,255,255));
        drawLinePixels(canvas, cv::Point(nr.x+16, nr.y+8), cv::Point(nr.x+26, nr.y+15), cv::Vec3b(255,255,255));
        drawLinePixels(canvas, cv::Point(nr.x+26, nr.y+15), cv::Point(nr.x+16, nr.y+22), cv::Vec3b(255,255,255));
        if (playing) {
            fillRectPixels(canvas, cv::Rect(plr.x+14, plr.y+8, 4, 14), cv::Vec3b(255,255,255));
            fillRectPixels(canvas, cv::Rect(plr.x+24, plr.y+8, 4, 14), cv::Vec3b(255,255,255));
        } else {
            for (int r = 0; r < 15; ++r) {
                int ww = r < 8 ? r : 14 - r;
                fillRectPixels(canvas, cv::Rect(plr.x+16, plr.y+8+r, 4+ww, 1), cv::Vec3b(255,255,255));
            }
        }
        buttons.push_back({pr, prev}); buttons.push_back({plr, play}); buttons.push_back({nr, next});
    };

    auto drawEvsMode = [&]() {
        int tw = 132, th = 24, hw = tw / 2;
        int x0 = std::min(std::max(12, evs.cols / 2 - 250), std::max(0, evs.cols - tw - 12));
        int y0 = by + 39;
        cv::Rect tr(x0, y0, tw, th), sr(x0, y0, hw, th), ar(x0 + hw, y0, tw - hw, th);
        cv::Rect kr = evs_sm == EvsStepMode::Single ? cv::Rect(x0+2, y0+2, hw-4, th-4) : cv::Rect(x0+hw+2, y0+2, tw-hw-4, th-4);
        fillRectPixels(canvas, tr, cv::Vec3b(40,40,40));
        strokeRectPixels(canvas, tr, cv::Vec3b(255,255,255));
        fillRectPixels(canvas, kr, cv::Vec3b(42,90,135));
        fillRectPixels(canvas, cv::Rect(x0+hw, y0+4, 1, th-8), cv::Vec3b(175,175,175));
        drawDigitText(canvas, sr.x+12, sr.y+5, "1/8", evs_sm == EvsStepMode::Single ? cv::Vec3b(255,255,255) : cv::Vec3b(175,175,175), 2);
        drawDigitText(canvas, ar.x+18, ar.y+5, "1X", evs_sm == EvsStepMode::Aps ? cv::Vec3b(255,255,255) : cv::Vec3b(175,175,175), 2);
        buttons.push_back({sr, UiAction::EvsStepSingle});
        buttons.push_back({ar, UiAction::EvsStepAps});
    };

    drawProgress(0, evs.cols, evs_fi, evs_fc, cv::Vec3b(80,185,255));
    drawProgress(evs.cols, aps.cols, aps_fi, aps_fc, cv::Vec3b(90,210,140));
    drawEvsMode();
    drawBtns(0, evs.cols, UiAction::EvsPrev, UiAction::EvsTogglePlay, UiAction::EvsNext, evs_play);
    drawBtns(evs.cols, aps.cols, UiAction::ApsPrev, UiAction::ApsTogglePlay, UiAction::ApsNext, aps_play);
    std::lock_guard<std::mutex> lock(g_ui_mutex);
    g_ui_buttons = std::move(buttons);
    return canvas;
}

/** @brief 打印 OpenCV 窗口初始化失败诊断。 */
void printGuiStartupError(const cv::Exception& e) {
    std::cerr << "Failed to initialize OpenCV window:\n" << e.what() << "\n"
              << "DISPLAY=" << (std::getenv("DISPLAY") ?: "(unset)") << "\n"
              << "Run with DISPLAY or X11 forwarding.\n";
}

/** @brief 打印命令行用法。 */
void printUsage(const char* prog) {
    std::cerr << "Usage: " << prog << " <events.raw> <video.avi> [fps] [speed]\n"
              << "       " << prog << " <events.raw> <video.avi> --dump-timestamps\n"
              << "  events.raw : EVS 录制文件（toolkit EventReader 格式）\n"
              << "  video.avi  : APS AVI 录制文件（含 tsmp chunk）\n"
              << "  fps        : AVI 帧率回退值（默认 30）\n"
              << "  speed      : 播放速度（默认 1.0）\n"
              << "  --dump-timestamps : CSV 输出所有 EVS RAW8 与 APS tsmp 时间戳后退出\n";
}

bool dumpTimestamps(const std::string& raw_path, const std::string& avi_path,
                    std::ostream& out) {
    out << "stream,packet_index,subframe_index,raw_timestamp,processed_timestamp_us,valid\n";

    Shimeta::io::HybridReader evs_reader;
    if (!evs_reader.open(raw_path, "")) {
        std::cerr << "无法打开 EVS RAW: " << raw_path << std::endl;
        return false;
    }

    uint64_t evs_packet_index = 0;
    Shimeta::Frame evs_frame;
    while (evs_reader.readEvsPacket(evs_frame)) {
        const size_t subframe_count = evs_frame.evs.size / Shimeta::codec::MipiRaw8Layout::kSubframeBytes;
        for (size_t sub = 0; sub < subframe_count; ++sub) {
            const uint8_t* data = evs_frame.evs.data + sub * Shimeta::codec::MipiRaw8Layout::kSubframeBytes;
            uint64_t first_word = 0;
            std::memcpy(&first_word, data, sizeof(first_word));
            if ((uint32_t(first_word) & 0x00FFFFFFu) != Shimeta::codec::MipiRaw8Layout::kHeaderMask)
                continue;

            const uint64_t raw_timestamp = (first_word >> 24) & 0xFFFFFFFFFFULL;
            out << "EVS," << evs_packet_index << ',' << sub << ',' << raw_timestamp
                << ',' << raw_timestamp / 200 << ",1\n";
        }
        ++evs_packet_index;
    }

    Shimeta::io::HybridReader aps_reader;
    if (!aps_reader.open("", avi_path)) {
        std::cerr << "无法打开 APS AVI: " << avi_path << std::endl;
        return false;
    }

    uint64_t aps_index = 0;
    Shimeta::Frame aps_frame;
    Shimeta::EvsTimestamp aps_timestamp;
    while (aps_reader.readApsFrame(aps_frame, &aps_timestamp)) {
        out << "APS," << aps_index++ << ",," << aps_timestamp.raw_timestamp << ','
            << aps_timestamp.processed_timestamp << ',' << (aps_timestamp.valid ? 1 : 0) << '\n';
    }
    return true;
}

/** @brief OpenCV 鼠标回调：左键命中按钮时把其动作写入 g_pending_action。 */
void mouseCallback(int event, int x, int y, int, void*) {
    if (event != cv::EVENT_LBUTTONDOWN) return;
    std::lock_guard<std::mutex> lock(g_ui_mutex);
    for (const auto& btn : g_ui_buttons) {
        if (btn.rect.contains(cv::Point(x, y))) {
            g_pending_action = static_cast<int>(btn.action);
            return;
        }
    }
}

} // namespace hv_player
