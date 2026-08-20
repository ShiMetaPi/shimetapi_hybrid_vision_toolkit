// callback: 注册 Event/Image 双回调（真实硬件下由 dispatch 线程触发）。
// 默认 USB 后端；传 --mipi 切换到 MIPI 后端（S100/X5 板上 MIPI 相机）。
// 注意：MIPI 后端 APS 未实现，--mipi 下 images 恒为 0（仅事件回调会被触发）。
// --sensor-index N 覆盖默认 sensor 索引（S100 默认 9；X5 同配置在 49）。
#include <shimetapi/hv/camera.h>
#include <shimetapi/hv/device_config.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
int main(int argc, char** argv) {
    Shimeta::hv::Camera cam;
    Shimeta::hv::DeviceConfig cfg;
    // 检查是否指定 MIPI / MIPI-HVS 后端
    bool use_mipi = false;
    bool use_mipi_hvs = false;
    int sensor_index = 9;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--mipi") == 0) use_mipi = true;
        else if (std::strcmp(argv[i], "--mipi-hvs") == 0) use_mipi_hvs = true;
        else if (std::strcmp(argv[i], "--sensor-index") == 0 && i + 1 < argc)
            sensor_index = std::atoi(argv[++i]);
    }
    if (use_mipi_hvs) {
        cfg.backend = Shimeta::hv::Backend::MipiHvs;
        std::printf("callback: using MIPI-HVS backend (APS+EVS)\n");
    } else if (use_mipi) {
        cfg.backend = Shimeta::hv::Backend::Mipi;
        cfg.sensor_index = sensor_index;
        std::printf("callback: using MIPI backend (sensor_index=%d)\n", sensor_index);
    } else {
        cfg.backend = Shimeta::hv::Backend::Usb;
        cfg.vendor_id  = (argc > 1) ? uint16_t(strtoul(argv[1], nullptr, 0)) : 0x1d6b;
        cfg.product_id = (argc > 2) ? uint16_t(strtoul(argv[2], nullptr, 0)) : 0x0105;
        std::printf("callback: using USB backend\n");
    }
    cam.Init(cfg);
    std::atomic<int> events{0}, images{0};
    cam.SetEventCallback([&](const Shimeta::hv::EventPacket&) { events++; });
    cam.SetImageCallback([&](const Shimeta::hv::ImageData&)   { images++; });
    if (!cam.StartStream()) {
        std::printf("callback: no device — start failed (expected on host w/o camera)\n");
        return 0;
    }
    // 等待 2 秒接收回调（真机 ~60 帧事件 + ~2 帧 APS @27fps）
    std::this_thread::sleep_for(std::chrono::seconds(2));
    cam.StopStream();
    cam.Destroy();
    std::printf("callback: events=%d images=%d\n", events.load(), images.load());
    return 0;
}
