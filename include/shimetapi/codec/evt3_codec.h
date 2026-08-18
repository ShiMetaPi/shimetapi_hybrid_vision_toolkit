// Copyright 2026 ShiMetaPi. Licensed under the Apache License, Version 2.0.
// Clean-room EVT3 实现，依据公开 EVT3 规范（见总 spec §3.4）。纯自洽，无第三方 SDK 依赖。
#ifndef SHIMETA_CODEC_EVT3_CODEC_H
#define SHIMETA_CODEC_EVT3_CODEC_H
#include <cstddef>
#include <cstdint>
#include <vector>
#include <shimetapi/core/event_cd.h>
namespace Shimeta::codec {

enum class Evt3Type : uint8_t {
    AddrY       = 0x0,
    AddrX       = 0x1,
    VectBaseX   = 0x2,
    Vect12      = 0x3,
    Vect8       = 0x4,
    TimeLow     = 0x6,
    TimeHigh    = 0x7,
    ExtTrigger  = 0x8,
};

/// EVT3 解码器（有状态：跨包维护 time-high/low + 翻转计数器）。
class Evt3Decoder {
public:
    Evt3Decoder();
    /// 解码 16-bit word 流（字节长度 len 必须为 2 的倍数）。返回解码事件数。
    size_t Decode(const uint8_t* buf, size_t len, std::vector<EventCD>& out);
    void Reset();
private:
    uint16_t cur_y_ = 0;
    uint16_t base_x_ = 0;
    bool     base_x_set_ = false;
    bool     y_set_ = false;
    uint32_t ts24_ = 0;          // 当前 24-bit 时间戳（time_high<<12 | time_low）
    uint16_t last_time_high_ = 0;
    bool     time_high_set_ = false;
    uint64_t overflow_ = 0;      // 24-bit 翻转计数
    void handleWord(uint16_t w, std::vector<EventCD>& out);
    int64_t fullTs() const { return int64_t((overflow_ << 24) | ts24_); }
};

/// EVT3 编码器（逆过程）。
class Evt3Encoder {
public:
    Evt3Encoder();
    void Encode(const EventCD* events, size_t count, std::vector<uint8_t>& out);
    void Reset();
private:
    uint16_t last_y_ = 0xFFFF;
    uint32_t last_ts24_ = 0xFFFFFFFF;
    uint64_t rollovers_emitted_ = 0;   // 已驱动解码器完成的 2^24 翻转数
    void emitWord(std::vector<uint8_t>& out, uint16_t w);
    void emitTimeHigh(std::vector<uint8_t>& out, uint32_t ts24);
};

} // namespace Shimeta::codec
#endif // SHIMETA_CODEC_EVT3_CODEC_H
