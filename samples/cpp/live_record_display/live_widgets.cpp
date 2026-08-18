/**
 * @file live_widgets.cpp
 * @brief HV live_record_display 辅助层实现。详见 live_widgets.h。
 */
#include "live_widgets.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace hv_live {
namespace {

/** @brief 去掉路径末尾已知的 .raw/.avi 扩展名。 */
std::string stripKnownExtension(const std::string& path) {
    size_t dot = path.rfind('.');
    if (dot == std::string::npos) return path;
    size_t slash = path.rfind('/');
    if (slash != std::string::npos && dot < slash) return path;
    std::string ext = path.substr(dot);
    if (ext == ".raw" || ext == ".avi") return path.substr(0, dot);
    return path;
}

/** @brief 在 prefix 上加「_YYYYMMDD_HHMMSS_mmm.ext」生成时间戳文件名。 */
std::string makeTimestampedPath(const std::string& prefix, const std::string& ext) {
    auto now = std::chrono::system_clock::now();
    auto tt  = std::chrono::system_clock::to_time_t(now);
    auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(
                   now.time_since_epoch()) % 1000;
    std::ostringstream ss;
    ss << stripKnownExtension(prefix) << "_"
       << std::put_time(std::localtime(&tt), "%Y%m%d_%H%M%S")
       << "_" << std::setfill('0') << std::setw(3) << ms.count()
       << "." << ext;
    return ss.str();
}

} // namespace

// ============================================================================
// EvsVisualizer
// ============================================================================
EvsVisualizer::EvsVisualizer()
    : frame_(kDefaultEvsHeight, kDefaultEvsWidth, CV_32FC3, cv::Scalar(0, 0, 0)) {}

void EvsVisualizer::addEvents(const std::vector<Shimeta::EventCD>& events) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& e : events) {
        if (e.x >= kDefaultEvsWidth || e.y >= kDefaultEvsHeight) continue;
        cv::Vec3f& px = frame_.at<cv::Vec3f>(int(e.y), int(e.x));
        if (e.polarity)
            px = cv::Vec3f(1.0f, 1.0f, 1.0f);   // ON：白
        else
            px = cv::Vec3f(0.0f, 0.627f, 1.0f); // OFF：橙
    }
}

cv::Mat EvsVisualizer::getFrame() {
    std::lock_guard<std::mutex> lock(mutex_);
    cv::Mat result;
    frame_.convertTo(result, CV_8UC3, 255.0);
    frame_ *= kEvsDecayFactor;
    return result;
}

// ============================================================================
// RecordManager
// ============================================================================
bool RecordManager::isRecording() const { return recording_; }

bool RecordManager::start(const std::string& evsPrefix, const std::string& apsPrefix) {
    if (recording_) return true;
    evsPath_ = makeTimestampedPath(evsPrefix, "raw");
    apsPath_ = makeTimestampedPath(apsPrefix, "avi");
    if (!writer_.open(evsPath_, apsPath_, kDefaultEvsWidth, kDefaultApsHeight,
                      Shimeta::io::RawFormat::Evt3, kDefaultApsFps)) {
        std::fprintf(stderr, "RecordManager: open failed\n");
        return false;
    }
    recording_ = true;
    std::printf("\n开始录制: %s / %s\n", evsPath_.c_str(), apsPath_.c_str());
    return true;
}

void RecordManager::stop() {
    if (!recording_) return;
    writer_.close();
    recording_ = false;
    std::printf("停止录制: %s / %s (APS帧=%u)\n",
                evsPath_.c_str(), apsPath_.c_str(), writer_.apsFrameCount());
}

void RecordManager::writeFrame(const Shimeta::Frame& f, const Shimeta::EvsTimestamp* evs_ts) {
    if (!recording_) return;
    writer_.writeFrame(f, evs_ts);
}

// ============================================================================
// NV12 解码 / 用法
// ============================================================================
cv::Mat nv12ToBgr(const uint8_t* data, int width, int height) {
    if (data == nullptr || width <= 0 || height <= 0) return {};
    cv::Mat y(height, width, CV_8UC1, const_cast<uint8_t*>(data));
    cv::Mat uv(height / 2, width / 2, CV_8UC2, const_cast<uint8_t*>(data + width * height));
    cv::Mat bgr;
    cv::cvtColorTwoPlane(y, uv, bgr, cv::COLOR_YUV2BGR_NV12);
    return bgr;
}

void printUsage(const char* prog) {
    std::fprintf(stderr,
        "Usage: %s [OPTIONS]\n"
        "Options:\n"
        "  --no-display         禁用 OpenCV 窗口，使用控制台交互\n"
        "  --evs-prefix <str>   EVS 录制文件前缀 (default: live_events)\n"
        "  --aps-prefix <str>   APS 录制文件前缀 (default: live_video)\n"
        "  -h, --help           显示帮助\n"
        "\nKeys:\n"
        "  r       开始/停止录制（带时间戳文件名）\n"
        "  d       开启/关闭显示更新\n"
        "  q/ESC   退出\n",
        prog);
}

} // namespace hv_live
