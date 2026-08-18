// Copyright 2026 ShiMetaPi. Licensed under the Apache License, Version 2.0.
#ifndef SHIMETA_HV_IMAGE_DATA_H
#define SHIMETA_HV_IMAGE_DATA_H
#include <shimetapi/core/buffer_pool.h>
#include <shimetapi/core/pixel_format.h>
#include <shimetapi/core/timestamp.h>
namespace Shimeta::hv {

/// 一帧 APS 图像原始字节 + 元信息。
struct ImageData {
    BufferView    pixels{};
    int           width = 0, height = 0;
    PixelFormat   format{};
    TimestampInfo ts{};
};

} // namespace Shimeta::hv
#endif // SHIMETA_HV_IMAGE_DATA_H
