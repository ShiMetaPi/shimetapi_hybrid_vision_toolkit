// Copyright 2026 ShiMetaPi. Licensed under the Apache License, Version 2.0.
// apx003 HVS RAW8 解码（clean-room，依据旧 decodeEvsRawToEventsImpl 逻辑）。无 RDK/第三方 SDK 依赖。
#ifndef SHIMETA_CODEC_MIPI_RAW8_CODEC_H
#define SHIMETA_CODEC_MIPI_RAW8_CODEC_H
#include <cstddef>
#include <cstdint>
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

/// 解码 apx003 RAW8 子帧流 → EventCD。无状态（无跨包时间戳状态）。
class MipiRaw8Decoder {
public:
    MipiRaw8Decoder() = default;
    /// 解码 data[0..len)；subframe_count 指定子帧数（默认整包 32）。返回解码事件数。
    size_t Decode(const uint8_t* data, size_t len, std::vector<EventCD>& out,
                  int subframe_count = MipiRaw8Layout::kTotalSubframes);
    void Reset() {}  // 无状态
};

/// 从 apx003 RAW8 子帧头部提取传感器时间戳（45-bit / 200 → 微秒）。
/// 顺序遍历子帧，取第一个头掩码匹配的子帧的时间戳；data 至少含一个完整子帧。
Shimeta::EvsTimestamp extractEvsTimestamp(const uint8_t* data, size_t len);

} // namespace Shimeta::codec
#endif // SHIMETA_CODEC_MIPI_RAW8_CODEC_H
