// Copyright 2026 ShiMetaPi. Licensed under the Apache License, Version 2.0.
#ifndef SHIMETA_CORE_FRAME_H
#define SHIMETA_CORE_FRAME_H
#include <cstddef>
#include <cstdint>
#include <memory>
#include <shimetapi/core/buffer_pool.h>
#include <shimetapi/core/evs_timestamp.h>
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

    /// 与该 APS 帧 vpf 配对的 EVS 包 sensor 时间戳（StreamSession 时间桥按
    /// VPF 到达时间戳精确匹配后从配对包提取；移植自旧 Demo
    /// hv_camera_live_record_timestamps 的 EvsApsTimestampBridge）。录制时
    /// 写入 AVI tsmp chunk，回放端据此做 1 APS ↔ N EVS 对齐。无配对（如
    /// USB 后端 / EVS-only）时 valid=false。字段追加在结构末尾：旧版 .so
    /// 构造的 Frame 经引用传递时本字段保持调用方的零初始化值，向后兼容。
    EvsTimestamp  aps_evs_ts{};
};

} // namespace Shimeta
#endif // SHIMETA_CORE_FRAME_H
