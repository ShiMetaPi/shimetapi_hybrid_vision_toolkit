// Copyright 2026 ShiMetaPi. Licensed under the Apache License, Version 2.0.
// apx003 HVS RAW8 解码（clean-room，依据旧 decodeEvsRawToEventsImpl 逻辑）。无 RDK/第三方 SDK 依赖。
// APX014（CF-NRS3 新格式）不走本文件：EVS 变长包解析/聚帧见 evs_packet_codec.h 的
// Evs014PacketDecoder，APS 帧头解析见同头文件的 parseAps014Header。
#ifndef SHIMETA_CODEC_MIPI_RAW8_CODEC_H
#define SHIMETA_CODEC_MIPI_RAW8_CODEC_H
#include <cstddef>
#include <cstdint>
#include <vector>
#include <shimetapi/core/event_cd.h>
#include <shimetapi/core/evs_timestamp.h>
#include <shimetapi/codec/evs_packet_codec.h>   // kAps014FrameLen（014 APS 探测长度）
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

/// 传感器档位（按 APS 首帧长度探测，见 probeSensorByApsLen）。
enum class SensorModel { Apx003, Apx014 };

/// APS 首帧传输长度 → 传感器档位。700,424→Apx003（NV12+8B 尾）；
/// 655,440→Apx014（CF-NRS3 新格式：80B 头 + Gray8 640×1024）；未知长度返回 false。
inline bool probeSensorByApsLen(size_t aps_transferred, SensorModel& out) {
    if (aps_transferred == 700424) { out = SensorModel::Apx003; return true; }
    if (aps_transferred == kAps014FrameLen) { out = SensorModel::Apx014; return true; }
    return false;
}

/// apx003 RAW8 子帧流解码器（003 专用，无状态）。
/// chunk 为 32,768 整数倍字节：事件 t = ts/200 微秒（原语义）。
/// 014 的 EVS 包解码见 evs_packet_codec.h 的 Evs014PacketDecoder。
/// Decode 非线程安全——按流序单线程调用（与既有用法一致）。
class MipiRaw8Decoder {
public:
    MipiRaw8Decoder() = default;
    /// 解码 data[0..len)：按 32,768B 子帧切分。
    /// subframe_count<=0 → 自动档（按 buffer 实际长度解全部子帧，适配任意帧率档）；
    /// 显式传值可只解前 N 个子帧。
    size_t Decode(const uint8_t* data, size_t len, std::vector<EventCD>& out,
                  int subframe_count = 0);
};

/// 从 apx003 RAW8 子帧头部提取传感器时间戳（45-bit / 200 → 微秒）。
/// 顺序遍历子帧，取第一个头掩码匹配的子帧的时间戳；data 至少含一个完整子帧。
Shimeta::EvsTimestamp extractEvsTimestamp(const uint8_t* data, size_t len);

} // namespace Shimeta::codec
#endif // SHIMETA_CODEC_MIPI_RAW8_CODEC_H
