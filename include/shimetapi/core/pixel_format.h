// Copyright 2026 ShiMetaPi. Licensed under the Apache License, Version 2.0.
#ifndef SHIMETA_CORE_PIXEL_FORMAT_H
#define SHIMETA_CORE_PIXEL_FORMAT_H
#include <cstdint>
namespace Shimeta {
enum class PixelFormat : uint8_t {
    BayerRG8 = 0,
    RGB888   = 1,
    Gray8    = 2,
    RAW8     = 3,
    RAW10    = 4,
    NV12     = 5,
};
} // namespace Shimeta
#endif // SHIMETA_CORE_PIXEL_FORMAT_H
