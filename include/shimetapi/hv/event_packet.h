// Copyright 2026 ShiMetaPi. Licensed under the Apache License, Version 2.0.
#ifndef SHIMETA_HV_EVENT_PACKET_H
#define SHIMETA_HV_EVENT_PACKET_H
#include <cstdint>
#include <shimetapi/core/buffer_pool.h>
namespace Shimeta::hv {

/// 一包事件原始字节（HAL 未解码）。data 指向池 slab，由 StreamSession 持有生命周期。
struct EventPacket {
    BufferView data{};
    int64_t    t_begin_ns = 0;
    int64_t    t_end_ns   = 0;
};

} // namespace Shimeta::hv
#endif // SHIMETA_HV_EVENT_PACKET_H
