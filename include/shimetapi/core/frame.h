// Copyright 2026 ShiMetaPi. Licensed under the Apache License, Version 2.0.
#ifndef SHIMETA_CORE_FRAME_H
#define SHIMETA_CORE_FRAME_H
#include <cstddef>
#include <cstdint>
#include <memory>
#include <shimetapi/core/buffer_pool.h>
#include <shimetapi/core/pixel_format.h>
#include <shimetapi/core/timestamp.h>
namespace Shimeta {

/// 统一帧。aps/evs 为池内存的只读视图；aps_owner/evs_owner 持有 slab
/// 引用以保证视图在 Frame 存活期间有效（零拷贝、池托管生命周期）。
/// 由 FrameDispatcher 构造，用户不应手动修改 owner 字段。
struct Frame {
    BufferView    aps{};
    BufferView    evs{};
    TimestampInfo ts{};
    int           width{0};
    int           height{0};
    int           frame_id{0};
    PixelFormat   format{};

    std::shared_ptr<uint8_t[]> aps_owner{};
    std::shared_ptr<uint8_t[]> evs_owner{};
};

} // namespace Shimeta
#endif // SHIMETA_CORE_FRAME_H
