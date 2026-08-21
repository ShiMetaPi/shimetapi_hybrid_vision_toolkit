/**
 * @file live_widgets.h
 * @brief HV live_record_display 的可视化/录制/解码辅助层。
 *
 * 把 main 之外的 EVS 可视化、录制管理、NV12 解码集中于此，
 * main.cpp 只保留参数解析与采集/显示主循环。所有符号位于命名空间 hv_live。
 * 这些都是「应用层」逻辑（表现/编排），不属于工具包公有 API。
 */
#ifndef HV_LIVE_WIDGETS_H
#define HV_LIVE_WIDGETS_H

#include <mutex>
#include <string>
#include <vector>

#include <opencv2/opencv.hpp>

#include <shimetapi/core/event_cd.h>
#include <shimetapi/core/evs_timestamp.h>
#include <shimetapi/core/frame.h>
#include <shimetapi/io/hybrid_writer.h>

namespace hv_live {

/** @brief 显示几何与帧率常量。 */
constexpr int    kDefaultEvsWidth   = 768;
constexpr int    kDefaultEvsHeight  = 608;
constexpr int    kDefaultApsWidth   = 768;
constexpr int    kDefaultApsHeight  = 608;
constexpr int    kDefaultDisplayFps = 30;
constexpr double kEvsDecayFactor    = 0.95;   ///< EVS 可视化每帧衰减系数
constexpr double kDefaultApsFps     = 30.0;
constexpr const char* kWindowName   = "EVS / APS Live";

/**
 * @brief EVS 事件可视化器：把事件累积到一张图，每取帧衰减一次（拖影效果）。
 */
class EvsVisualizer {
public:
    EvsVisualizer();
    /** @brief 追加一批事件到可视化图（线程安全）。@param events 事件向量。 */
    void addEvents(const std::vector<Shimeta::EventCD>& events);
    /** @brief 取当前可视化帧（BGR），并衰减内部图。 */
    cv::Mat getFrame();
private:
    cv::Mat     frame_;
    std::mutex  mutex_;
};

/**
 * @brief 录制管理器：封装 HybridWriter，按键开关、每次录制生成带时间戳的文件名。
 */
class RecordManager {
public:
    bool isRecording() const;
    /** @brief 开始录制（生成时间戳文件名 + 打开 HybridWriter）。@param evsPrefix EVS 文件前缀；@param apsPrefix APS 文件前缀。@return 是否成功。 */
    bool start(const std::string& evsPrefix, const std::string& apsPrefix);
    void stop();
    /** @brief 写一帧（录制中才写）。 */
    void writeFrame(const Shimeta::Frame& f, const Shimeta::EvsTimestamp* evs_ts = nullptr);
private:
    Shimeta::io::HybridWriter writer_;
    std::string evsPath_;
    std::string apsPath_;
    bool recording_ = false;
    bool seenAps_ = false;
};

/** @brief NV12 packed 字节 → BGR cv::Mat（应用层解码，工具包保持零 OpenCV）。 */
cv::Mat nv12ToBgr(const uint8_t* data, size_t data_size, int width, int height);

/** @brief 打印命令行用法。@param prog 程序名。 */
void printUsage(const char* prog);

} // namespace hv_live

#endif // HV_LIVE_WIDGETS_H
