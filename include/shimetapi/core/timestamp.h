// Copyright 2026 ShiMetaPi. Licensed under the Apache License, Version 2.0.
#ifndef SHIMETA_CORE_TIMESTAMP_H
#define SHIMETA_CORE_TIMESTAMP_H
#include <cstdint>
namespace Shimeta {
struct TimestampInfo {
    int64_t evs_ts_ns  = 0; ///< EVS 事件参考时间戳（纳秒）
    int64_t aps_ts_ns  = 0; ///< APS 尾时间戳（对齐句柄：组帧时设备注入的最新 EVS ts；003=ns、014=原始 tick（非 ns），0=无效）
    bool    ptp_locked = false; ///< Ethernet 后端 PTP 是否锁定
};
} // namespace Shimeta
#endif // SHIMETA_CORE_TIMESTAMP_H
