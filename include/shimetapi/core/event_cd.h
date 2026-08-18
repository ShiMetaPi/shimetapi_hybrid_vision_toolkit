// Copyright 2026 ShiMetaPi. Licensed under the Apache License, Version 2.0.
#ifndef SHIMETA_CORE_EVENT_CD_H
#define SHIMETA_CORE_EVENT_CD_H
#include <cstdint>
namespace Shimeta {

/// 自有事件类型（POD）。字段语义与业界常见事件结构一一对应；
/// 核心不依赖任何第三方 SDK 头，可选 adapter 做平凡转换。
struct EventCD {
    uint16_t x;        ///< 像素 X 坐标
    uint16_t y;        ///< 像素 Y 坐标
    int64_t  t;        ///< 时间戳（微秒）
    bool     polarity; ///< 1 = CD_ON, 0 = CD_OFF
};

} // namespace Shimeta
#endif // SHIMETA_CORE_EVENT_CD_H
