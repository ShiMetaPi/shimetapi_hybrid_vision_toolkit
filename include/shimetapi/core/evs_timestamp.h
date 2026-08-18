// Copyright 2026 ShiMetaPi. Licensed under the Apache License, Version 2.0.
#ifndef SHIMETA_CORE_EVS_TIMESTAMP_H
#define SHIMETA_CORE_EVS_TIMESTAMP_H
#include <cstdint>
namespace Shimeta {

/// EVS 传感器内部时间戳，从 MIPI RAW8 子帧头部提取。
/// 格式与旧 Demo hv_camera_live_record_timestamps_mipi 的 tsmp chunk 兼容。
struct EvsTimestamp {
    uint64_t raw_timestamp = 0;         ///< 传感器 45-bit 原始时间戳
    uint64_t processed_timestamp = 0;   ///< raw_timestamp / 200（微秒）
    bool     valid = false;
};

} // namespace Shimeta
#endif // SHIMETA_CORE_EVS_TIMESTAMP_H
