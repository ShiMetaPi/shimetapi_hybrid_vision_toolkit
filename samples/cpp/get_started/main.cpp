// get_started: 最小 Camera 流程（Init → StartStream → GetFrame 循环）。
// 默认 USB 后端；传 --mipi 切换到 MIPI 后端（S100/X5 板上 MIPI 相机）。
// --sensor-index N 覆盖默认 sensor 索引（S100 默认 9 = apx003cc linear_4096x256_raw8；
// X5 SDK 列表更长，同一配置在索引 49）。
#include <shimetapi/hv/camera.h>
#include <shimetapi/hv/device_config.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
int main(int argc, char** argv) {
    Shimeta::hv::Camera cam;
    Shimeta::hv::DeviceConfig cfg;
    // 检查是否指定 MIPI 后端
    bool use_mipi = false;
    int sensor_index = 9;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--mipi") == 0) use_mipi = true;
        else if (std::strcmp(argv[i], "--sensor-index") == 0 && i + 1 < argc)
            sensor_index = std::atoi(argv[++i]);
    }
    if (use_mipi) {
        cfg.backend = Shimeta::hv::Backend::Mipi;
        cfg.sensor_index = sensor_index;
        std::printf("get_started: using MIPI backend (sensor_index=%d)\n", sensor_index);
    } else {
        cfg.backend = Shimeta::hv::Backend::Usb;
        cfg.vendor_id  = (argc > 1) ? uint16_t(strtoul(argv[1], nullptr, 0)) : 0x1d6b;
        cfg.product_id = (argc > 2) ? uint16_t(strtoul(argv[2], nullptr, 0)) : 0x0105;
        std::printf("get_started: using USB backend\n");
    }
    cam.Init(cfg);
    if (!cam.StartStream()) {
        std::printf("get_started: no device — start failed (expected on host w/o camera)\n");
        return 0;
    }
    for (int i = 0; i < 10; ++i) {
        Shimeta::Frame f;
        if (cam.GetFrame(f, 1000)) std::printf("get_started: frame %d evs=%zu bytes\n", i, f.evs.size);
    }
    cam.StopStream();
    cam.Destroy();
    return 0;
}
