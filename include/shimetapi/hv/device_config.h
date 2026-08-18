// Copyright 2026 ShiMetaPi. Licensed under the Apache License, Version 2.0.
#ifndef SHIMETA_HV_DEVICE_CONFIG_H
#define SHIMETA_HV_DEVICE_CONFIG_H
#include <cstdint>
#include <string>
#include <shimetapi/hv/event_format.h>
namespace Shimeta::hv {

enum class Backend { Auto, Usb, Mipi, MipiHvs, Ethernet };

struct DeviceConfig {
    Backend     backend      = Backend::Auto;
    std::string device_node;                 ///< MIPI: "/dev/video0"
    std::string ip;                          ///< Ethernet
    uint16_t    data_port    = 8000;
    uint16_t    ctrl_port    = 8001;
    EventFormat event_fmt    = EventFormat::Evt3;
    int         buffer_count = 8;
    uint16_t    vendor_id = 0, product_id = 0;     ///< USB VID/PID
    enum class QueuePolicy { DropOldest, Block };
    QueuePolicy queue_policy = QueuePolicy::DropOldest;
    int         event_urbs   = 4;                  ///< USB 事件端点在途 URB 数
    uint16_t    evs_fps      = 0;                  ///< 0=不设置（用设备默认）；非 0=Init 时自动下发
    int         sensor_index = 0;                   ///< MIPI 传感器索引
    uint8_t     i2c_bus      = 1;                   ///< MIPI 安全芯片认证 I2C 总线
    uint16_t    listen_port = 8888;                 ///< Ethernet: TCP 监听端口（相机协议默认 8888）
    std::string bind_ip;                            ///< Ethernet: 本地绑定 IP（空=INADDR_ANY）
};

} // namespace Shimeta::hv
#endif // SHIMETA_HV_DEVICE_CONFIG_H
