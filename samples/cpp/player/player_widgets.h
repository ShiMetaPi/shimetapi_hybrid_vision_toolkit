/**
 * @file player_widgets.h
 * @brief HV Player 的数据/回放/UI 辅助层。
 *
 * 把 main 之外的类（数据读取、帧缓存、回放同步、时间戳映射）和 UI 绘制函数集中于此，
 * main.cpp 只保留信号处理与播放编排。所有符号位于命名空间 hv_player。
 * 这些都是「应用层」逻辑（表现/交互/策略），不属于工具包公有 API。
 */
#ifndef HV_PLAYER_WIDGETS_H
#define HV_PLAYER_WIDGETS_H

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <opencv2/opencv.hpp>

#include <shimetapi/core/event_cd.h>
#include <shimetapi/core/evs_timestamp.h>
#include <shimetapi/io/hybrid_reader.h>

namespace hv_player {

/** @brief 播放/显示几何与帧率常量。 */
constexpr uint64_t kEvsFps               = 240;   ///< EVS 事件帧率（包/s）
constexpr uint64_t kApsFps               = 30;    ///< APS 图像帧率
constexpr uint64_t kEvsPerApsFrame       = kEvsFps / kApsFps;        ///< 一个 APS 帧对应的 EVS 帧数（=8）
constexpr uint64_t kEvsSubframesPerFrame = 4;     ///< 一个 EVS 帧含 4 个空间子帧
constexpr int      kEventDisplayWidth    = 768;   ///< EVS 显示宽
constexpr int      kEventDisplayHeight   = 608;   ///< EVS 显示高
constexpr int      kDisplayScaleNumerator   = 4;  ///< 显示缩放分子（4/5）
constexpr int      kDisplayScaleDenominator = 5;
constexpr int      kSidebarWidth         = 360;   ///< 右侧信息栏宽
constexpr int      kBottomBarHeight      = 76;    ///< 底部控制栏高
constexpr int      kMinSidebarHeight     = 660;   ///< 最小窗口高

/** @brief UI 按钮可触发的动作（播放控制、步进、配色、同步、变速）。 */
enum class UiAction {
    None,
    EvsPrev, EvsTogglePlay, EvsNext, EvsStepSingle, EvsStepAps,
    ApsPrev, ApsTogglePlay, ApsNext,
    EvsColorBlueRed, EvsColorOrangeYellow, SyncOn, SyncOff,
    SpeedOneEighth, SpeedOneQuarter, SpeedOneHalf, SpeedOne, SpeedTwo,
};

/** @brief EVS 步进粒度。Single=逐帧；Aps=对齐到 APS 帧边界（每 8 个 EVS 帧）。 */
enum class EvsStepMode { Single, Aps };

/** @brief EVS 极性配色方案。 */
enum class EvsColorMode { BlueRed, OrangeYellow };

/** @brief 可点击的 UI 按钮：矩形热区 + 触发动作。 */
struct UiButton { cv::Rect rect; UiAction action = UiAction::None; };

extern std::mutex            g_ui_mutex;       ///< 保护 g_ui_buttons 的互斥锁
extern std::vector<UiButton> g_ui_buttons;     ///< 当前帧可点击按钮（composeSideBySide 写入）
extern std::atomic<int>      g_pending_action; ///< 待处理动作（鼠标命中后置位，主循环消费）

/**
 * @brief APS 视频顺序读取器。
 *
 * 基于公有 HybridReader 读 AVI（NV12+tsmp），应用层做 NV12→BGR，
 * 并提供「顺序读到目标索引」的按帧访问。
 */
class VideoReader {
public:
    bool open(const std::string& path, double fallback_fps);
    double fps() const;
    uint64_t totalFrameCount() const;
    bool readFrameAt(uint64_t target_index, cv::Mat& frame, Shimeta::EvsTimestamp* timestamp = nullptr);
private:
    std::unique_ptr<Shimeta::io::HybridReader> reader_;
    double   fps_ = 30.0, fallback_fps_ = 30.0;
    uint64_t total_frames_ = 0, current_index_ = 0;
    cv::Mat  current_frame_;
    Shimeta::EvsTimestamp current_timestamp_{};
};

/**
 * @brief APS 帧缓存。
 *
 * 在内存中缓存已解码的 BGR 帧与时间戳，供随机访问（拖动/快进）。
 */
class ApsFrameCache {
public:
    bool open(const std::string& path, double fallback_fps);
    double fps() const;
    size_t frameCount() const;
    bool frameAt(uint64_t index, cv::Mat& frame, uint64_t* actual_index = nullptr);
    Shimeta::EvsTimestamp timestampAt(uint64_t index) const;
    size_t cachedFrameCount() const;
private:
    VideoReader                   reader_;
    std::vector<cv::Mat>          frames_;
    std::vector<Shimeta::EvsTimestamp> timestamps_;
};

/**
 * @brief EVS 帧序列。
 *
 * 一次性读全 + 解码（MipiRaw8）+ 每 4 子帧合成 1 极性帧，
 * 提供按帧索引访问、时间戳查找、着色渲染。
 */
class EvsFrameSequence {
public:
    bool open(const std::string& filename);
    size_t frameCount() const;
    cv::Mat frameAt(size_t index, EvsColorMode mode) const;
    cv::Mat accumulatedFrameAt(size_t index, size_t count, EvsColorMode mode) const;
    uint64_t processedTimestampAt(size_t index) const;
    uint64_t frameIndexForTimestamp(uint64_t timestamp, EvsStepMode mode) const;
    std::pair<uint32_t, uint32_t> imageSize() const;
private:
    cv::Mat renderPolarityFrame(const cv::Mat& polarity, EvsColorMode mode) const;
    uint32_t width_ = 0, height_ = 0;
    std::vector<cv::Mat>  frames_;                   ///< 每帧极性图（0=空/1=ON/2=OFF）
    std::vector<uint64_t> processed_timestamps_;    ///< 每帧处理时间戳(us)
};

/**
 * @brief 时间戳同步映射。
 *
 * 读外挂 .timestamps.csv，把 EVS 时间戳映射到最近的 APS 帧号。
 */
class TimestampSyncMap {
public:
    bool open(const std::string& raw_path, const std::string& avi_path);
    bool enabled() const;
    uint64_t videoIndexForEvsTs(uint64_t evs_ts_us) const;
    uint64_t apsVpfTvUsForVideoIndex(uint64_t vi) const;
private:
    struct EvsEntry { uint64_t evs_ts_us = 0, vpf_tv_us = 0; };
    struct ApsEntry { uint64_t avi_frame_index = 0, vpf_tv_us = 0, evs_raw_ts = 0, evs_processed_ts_us = 0; };
    std::vector<std::string> split(const std::string& line);
    uint64_t parseU64(const std::string& s);
    bool loadEvs(const std::string& path);
    bool loadAps(const std::string& path);
    bool                 enabled_ = false, aps_has_evs_raw_ts_ = false;
    uint64_t             base_vpf_tv_us_ = 0;
    std::vector<EvsEntry> evs_;
    std::vector<ApsEntry> aps_;
};

// ---- UI 绘制 / 辅助函数 ----
cv::Mat makeBlank(int w, int h);                                                         ///< 全黑 BGR 画布
cv::Mat scaleNearest(const cv::Mat& src, int num, int den);                              ///< 最近邻缩放（整数比）
cv::Mat scaleDisplayNearest(const cv::Mat& src);                                         ///< 按 4/5 缩放
cv::Mat scaleHalfNearest(const cv::Mat& src);                                            ///< 缩放到一半
uint8_t glyphBits(char c, int row);                                                      ///< 点阵字 c 的第 row 行位图（5×7）
void drawDigitText(cv::Mat& frame, int x, int y, const std::string& text,
                   const cv::Vec3b& color = cv::Vec3b(255,255,255), int scale = 3);       ///< 在 frame 上画点阵文本
void drawPixelText(cv::Mat& frame, int x, int y, const std::string& text,
                   const cv::Vec3b& color = cv::Vec3b(255,255,255), int scale = 2);       ///< drawDigitText 小字号别名
void fillRectPixels(cv::Mat& frame, const cv::Rect& rect, const cv::Vec3b& color);       ///< 实心矩形（像素级）
void strokeRectPixels(cv::Mat& frame, const cv::Rect& rect, const cv::Vec3b& color);     ///< 矩形描边（像素级）
void drawLinePixels(cv::Mat& frame, cv::Point a, cv::Point b, const cv::Vec3b& color);   ///< 画线（Bresenham，3px 粗）
void copyToCanvas(const cv::Mat& src, cv::Mat& dst, int dx, int dy);                     ///< 把 src 拷到 dst 的 (dx,dy)
uint64_t apsPlaybackTimestampUs(uint64_t fi, double fps);                                ///< 由帧号+帧率估回放时间戳(us)
uint64_t alignEvsFrameToApsBoundary(uint64_t fi);                                       ///< EVS 帧号对齐到 APS 边界
uint64_t clampEvsFrameIndex(uint64_t fi, size_t fc, EvsStepMode mode);                   ///< 限制 EVS 帧号范围并对齐
std::chrono::steady_clock::time_point evsStartForFrame(uint64_t fi, double spd);         ///< 第 fi 帧在速度 spd 下的 EVS 播放起点
std::chrono::steady_clock::time_point apsStartForFrame(uint64_t fi, double spd);         ///< 同上，APS 侧
bool isEvsAtEnd(uint64_t fi, size_t fc, EvsStepMode mode);                              ///< EVS 是否到末尾
bool isApsAtKnownEnd(uint64_t fi, const ApsFrameCache& vc);                             ///< APS 是否到已缓存末尾
double speedForAction(UiAction a);                                                       ///< UiAction → 播放速度（非速度类返回 0）

/**
 * @brief 绘制右侧信息栏（时间戳/帧号、Sync、Speed、EVS colors 按钮区）。
 * @param canvas 目标画布；@param x 栏左边界；@param height 栏高。
 * @param aps_ts/aps_ts_src/aps_fi/evs_ts/evs_fi 显示数值；@param evs_cm 配色。
 * @param sync_en 同步开关；@param speed 当前速度；@param buttons 输出生成的按钮。
 */
void drawSidebar(cv::Mat& canvas, int x, int height,
                 uint64_t aps_ts, const std::string& aps_ts_src,
                 uint64_t aps_fi, uint64_t evs_ts, uint64_t evs_fi,
                 EvsColorMode evs_cm, bool sync_en, double speed,
                 std::vector<UiButton>& buttons);

/**
 * @brief 合成整幅画面：左 EVS、中 APS、右信息栏、底部进度条+播放按钮。
 * @return 合成画布；副作用：按钮写入 g_ui_buttons（加锁）。
 */
cv::Mat composeSideBySide(const cv::Mat& evs_frame, const cv::Mat& video_frame,
                          uint64_t aps_ts, const std::string& aps_ts_src,
                          uint64_t aps_fi, uint64_t evs_ts, uint64_t evs_fi,
                          size_t aps_fc, size_t evs_fc,
                          EvsStepMode evs_sm, EvsColorMode evs_cm,
                          bool sync_en, double speed, bool evs_play, bool aps_play);

void printGuiStartupError(const cv::Exception& e);   ///< OpenCV 窗口初始化失败时打印诊断
void printUsage(const char* prog);                   ///< 打印命令行用法
bool dumpTimestamps(const std::string& raw_path, const std::string& avi_path,
                    std::ostream& out);             ///< CSV 输出 RAW8/AVI 的全部时间戳
void mouseCallback(int event, int x, int y, int, void*);  ///< OpenCV 鼠标回调（命中按钮→g_pending_action）

} // namespace hv_player

#endif // HV_PLAYER_WIDGETS_H
