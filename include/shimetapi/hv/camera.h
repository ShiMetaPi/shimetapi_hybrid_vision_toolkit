// Copyright 2026 ShiMetaPi. Licensed under the Apache License, Version 2.0.
#ifndef SHIMETA_HV_CAMERA_H
#define SHIMETA_HV_CAMERA_H
#include <functional>
#include <memory>
#include <shimetapi/core/frame.h>
#include <shimetapi/hv/device_config.h>
#include <shimetapi/hv/event_packet.h>
#include <shimetapi/hv/image_data.h>
namespace Shimeta::hv {

class Camera {
public:
    Camera();
    ~Camera();
    Camera(const Camera&) = delete;
    Camera& operator=(const Camera&) = delete;

    bool Init(const DeviceConfig& cfg);
    bool StartStream();
    void StopStream();
    void Destroy();

    bool GetFrame(Frame& frame, int timeout_ms = 1000);

    using FrameCallback  = std::function<void(const Frame&)>;
    using EventCallback  = std::function<void(const EventPacket&)>;
    using ImageCallback  = std::function<void(const ImageData&)>;
    void SetFrameCallback(FrameCallback cb);
    void SetEventCallback(EventCallback cb);
    void SetImageCallback(ImageCallback cb);

    bool SetExposure(int value);
    bool SetFrameRate(unsigned fps);
    bool GetFrameRate(unsigned& fps);
    bool SyncClock();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace Shimeta::hv
#endif // SHIMETA_HV_CAMERA_H
