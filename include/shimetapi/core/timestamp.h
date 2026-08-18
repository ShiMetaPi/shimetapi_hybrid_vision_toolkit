// Copyright 2026 ShiMetaPi. Licensed under the Apache License, Version 2.0.
#ifndef SHIMETA_CORE_TIMESTAMP_H
#define SHIMETA_CORE_TIMESTAMP_H
#include <cstdint>
namespace Shimeta {
struct TimestampInfo {
    int64_t evs_ts_ns  = 0; ///< EVS 事件参考时间戳（纳秒）
    int64_t aps_ts_ns  = 0; ///< 被复制的 EVS 时间戳（APS 曝光时刻，纳秒）
    bool    ptp_locked = false; ///< Ethernet 后端 PTP 是否锁定
};
} // namespace Shimeta
#endif // SHIMETA_CORE_TIMESTAMP_H
