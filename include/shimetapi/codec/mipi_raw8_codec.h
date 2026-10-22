// Copyright 2026 ShiMetaPi. Licensed under the Apache License, Version 2.0.
// apx003 HVS RAW8 解码（clean-room，依据旧 decodeEvsRawToEventsImpl 逻辑）。无 RDK/第三方 SDK 依赖。
#ifndef SHIMETA_CODEC_MIPI_RAW8_CODEC_H
#define SHIMETA_CODEC_MIPI_RAW8_CODEC_H
#include <cstddef>
#include <cstdint>
#include <deque>
#include <utility>
#include <vector>
#include <shimetapi/core/event_cd.h>
#include <shimetapi/core/evs_timestamp.h>
namespace Shimeta::codec {

struct MipiRaw8Layout {
    static constexpr int    kSubWidth       = 384;
    static constexpr int    kSubHeight      = 304;
    static constexpr int    kEvsWidth       = 768;
    static constexpr int    kEvsHeight      = 608;
    static constexpr int    kSubFrameNum    = 4;      // 每帧 4 空间子帧
    static constexpr int    kRawMergeNum    = 8;      // 每个 MIPI 包 8 帧
    static constexpr int    kTotalSubframes = kSubFrameNum * kRawMergeNum; // 32
    static constexpr size_t kSubframeBytes  = 32768;  // HV_SUB_FULL_BYTE_SIZE
    static constexpr uint32_t kHeaderMask   = 0x0000FFFFu;
};

/// APX014 线上布局（2026-10 固件）：一帧 164,864B = 32 子帧 × 5,152B。
/// 长度与 APX003 chunk（32,768 整数倍）天然无歧义，可按 chunk 长度分派。
struct MipiRaw014Layout {
    static constexpr size_t kSubframeBytes = 5152;   // 24B 头 + 5120B 载荷 + 8B 尾
    static constexpr int    kSubHeaderLen  = 24;
    static constexpr int    kSubFrameNum   = 32;     // 2 横 × 16 纵
    static constexpr int    kSubWidth      = 320;    // 每子帧 320×64 @2bit
    static constexpr int    kSubHeight     = 64;
    static constexpr int    kPackWidth     = 640;    // 打包事件网格
    static constexpr int    kPackHeight    = 1024;
    static constexpr int    kEvsWidth      = 1280;   // 插值后输出网格
    static constexpr int    kEvsHeight     = 1024;
    static constexpr size_t kFrameBytes    = kSubframeBytes * kSubFrameNum;  // 164,864
    static constexpr uint32_t kHeaderMask  = 0x00FFFFFFu;
    static constexpr uint32_t kMagic       = 0x0000FFFFu;
    // 014 APS 帧（NV12 + 尾部 8B 时间戳）线上长度：640*1024*3/2 + 8B = 983,048B。
    // HAL 读长与档位探测共用（单一事实源，替代散落的 983048 字面量）。
    static constexpr size_t kApsFrameBytes = 983048;
};

/// 传感器档位（按 APS 首帧长度探测，见 probeSensorByApsLen）。
enum class SensorModel { Apx003, Apx014 };

/// chunk 长度判别：014 整帧（164,864B，非 32,768 倍数）。
constexpr bool isApx014Chunk(size_t len) { return len == MipiRaw014Layout::kFrameBytes; }

/// APS 首帧传输长度 → 传感器档位。700,424→Apx003；983,048→Apx014；未知长度返回 false。
inline bool probeSensorByApsLen(size_t aps_transferred, SensorModel& out) {
    if (aps_transferred == 700424) { out = SensorModel::Apx003; return true; }
    if (aps_transferred == MipiRaw014Layout::kApsFrameBytes) { out = SensorModel::Apx014; return true; }
    return false;
}

/// 解码一个 APX014 EVS 帧（164,864B）→ EventCD。纯函数、无跨帧状态。
/// 事件 t 为【原始传感器 tick】（单位未知，换算由 MipiRaw8Decoder 的自校准负责）。
/// 任一子帧魔数非法或 FrameNum 极差 >1 → 整帧丢弃（清空 out 返回 0）。
/// frame_raw_ts 非空时回填帧时间戳（首子帧 +4 u32）。
size_t DecodeApx014Frame(const uint8_t* data, size_t len, std::vector<EventCD>& out,
                         uint32_t* frame_raw_ts = nullptr);

/// 双格式 RAW8 子帧流解码器。
/// - APX003 chunk（32,768 整数倍字节）：无状态解码，事件 t = ts/200 微秒（原语义）。
/// - APX014 chunk（恒 164,864B = 1 帧）：内置时间戳自校准——前 kSamples 帧记录
///   (原始 tick, 主机单调钟) 并缓冲事件；tick_hz = median(Δts mod 2^32) × 实测帧率；
///   校准完成后一次性补发缓冲帧，其后逐帧换算 t=(Δts mod 2^32)×10^6/tick_hz 微秒。
///   校准重试语义：仅当 tick_hz 推算失败（median=0 或样本 <2 → tick_hz≤0）时重开一个
///   采样窗（最多两窗，仍失败则强制交付）；实测帧率越界 [60,2000] 的异常不重试——
///   立即按 fallback_fps 回退并在本窗交付。
/// Decode 非线程安全——按流序单线程调用（与既有用法一致）。
class MipiRaw8Decoder {
public:
    MipiRaw8Decoder() = default;
    /// 解码 data[0..len)，按 chunk 长度自动分派格式。
    /// subframe_count 仅对 003 生效（<=0 自动档）；014 校准期本调用可能交付 0 事件。
    size_t Decode(const uint8_t* data, size_t len, std::vector<EventCD>& out,
                  int subframe_count = 0);
    void Reset() {
        cal_samples_.clear();
        cal_pending_.clear();
        pending_events_ = 0;
        cal_done_ = false;
        tick_hz_ = 0.0;
        raw0_ = 0;
        cal_windows_ = 0;
        reject_streak_ = 0;
    }
    bool   calibrated() const { return cal_done_; }
    double tickHz() const { return tick_hz_; }
    /// 014 校准基准：首 ever 帧的原始 tick（u32）。014 的 APS 尾时间戳与事件 t 同源同域，
    /// 消费方用 (tick − rawTickBase()) × 1e6 / tickHz() 换算到与事件 t 相同的 µs 轴。
    uint32_t rawTickBase() const { return raw0_; }
    /// 014 校准采样进度（0~kSamples；完成/未开始为 0）——供 UI 显示"校准中 N/64"。
    int calibSamples() const { return (int)cal_samples_.size(); }

    static constexpr int    kSamples    = 64;   // 校准采样窗
    static constexpr double kFallbackFps = 600.0;
    // 校准期 pending 缓冲的事件总量上界。最坏情形（重试窗最多 128 帧 × 每帧
    // 655,360 事件 × ~24B/事件）≈ 1GB+ 无界增长——限 ~4M 事件（≈96MB）封顶；
    // 超界丢最旧 pending 帧（被丢帧不参与补发，时间轴留空隙，t 相对 raw0_ 仍单调）。
    static constexpr size_t kMaxPendingEvents = 4 * 1024 * 1024;
    // 连续解码拒绝（子帧魔数/帧号校验失败）的限频诊断：每 N 次连续拒绝打一行
    // stderr（成功帧复位），避免失步被上层 0 事件静默吞掉。
    static constexpr int kRejectLogEvery = 100;

private:
    size_t Decode014Calibrated(const uint8_t* data, size_t len, std::vector<EventCD>& out);
    void noteRejectedFrame();   // 连续拒绝计数 + 限频日志

    std::vector<std::pair<uint32_t, int64_t>> cal_samples_;   // (raw_ts, host_us)
    // deque（而非 vector）：超 kMaxPendingEvents 时需从头部高效丢最旧帧。
    std::deque<std::vector<EventCD>> cal_pending_;            // 校准期缓冲帧
    size_t pending_events_ = 0;   // cal_pending_ 事件总数（对 kMaxPendingEvents 封顶）
    bool   cal_done_ = false;
    double tick_hz_  = 0.0;
    uint32_t raw0_   = 0;
    int     cal_windows_ = 0;   // 已失败的采样窗数
    int     reject_streak_  = 0;   // 连续解码拒绝计数（任一头部校验通过的帧复位）
};

/// 从 apx003 RAW8 子帧头部提取传感器时间戳（45-bit / 200 → 微秒）。
/// 顺序遍历子帧，取第一个头掩码匹配的子帧的时间戳；data 至少含一个完整子帧。
Shimeta::EvsTimestamp extractEvsTimestamp(const uint8_t* data, size_t len);

/// u32 中位数（拷贝排序；空返回 0；偶数个取上中位 values[size/2]）。
uint32_t medianU32(std::vector<uint32_t> values);

/// 014 时间戳自校准：tick_hz = median(模 2^32 delta) × fps，
/// fps = (N-1)/主机间隔秒；median=0 或 fps∉[60,2000] 时按 fallback_fps 回退并告警。
/// 样本 <2 返回 0.0。
double calibrateTickHz(const std::vector<std::pair<uint32_t, int64_t>>& ts_host_us_samples,
                       double fallback_fps);

} // namespace Shimeta::codec
#endif // SHIMETA_CODEC_MIPI_RAW8_CODEC_H
