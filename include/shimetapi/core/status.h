// Copyright 2026 ShiMetaPi. Licensed under the Apache License, Version 2.0.
#ifndef SHIMETA_CORE_STATUS_H
#define SHIMETA_CORE_STATUS_H
#include <cstdint>
namespace Shimeta {
enum class Status : int32_t {
    Ok                   = 0,
    ErrDeviceNotFound    = -1,
    ErrPermissionDenied  = -2,
    ErrUsbTransfer       = -3,
    ErrV4l2Ioctl         = -4,
    ErrNetworkTimeout    = -5,
    ErrInvalidParam      = -6,
    ErrBufferFull        = -7,
    ErrDecodeFailure     = -8,
    ErrUnsupportedFormat = -9,
};
const char* statusToString(Status s);
} // namespace Shimeta
#endif // SHIMETA_CORE_STATUS_H
