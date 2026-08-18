// viewer: 取每帧事件字节并解码、计数。
// 默认 USB 后端（EVT2）；传 --mipi 切换到 MIPI 后端，用 MipiRaw8Decoder 解 apx003 RAW8。
#include <shimetapi/hv/camera.h>
#include <shimetapi/hv/device_config.h>
#include <shimetapi/codec/evt2_codec.h>     // USB 相机发 EVT2
#include <shimetapi/codec/mipi_raw8_codec.h> // MIPI s100 (apx003) 发 RAW8
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
int main(int argc, char** argv) {
    Shimeta::hv::Camera cam;
    Shimeta::hv::DeviceConfig cfg;
    // 检查是否指定 MIPI / MIPI-HVS 后端
    bool use_mipi = false;
    bool use_mipi_hvs = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--mipi") == 0) use_mipi = true;
        else if (std::strcmp(argv[i], "--mipi-hvs") == 0) use_mipi_hvs = true;
    }
    if (use_mipi_hvs) {
        cfg.backend = Shimeta::hv::Backend::MipiHvs;
        std::printf("viewer: using MIPI-HVS backend (APS+EVS)\n");
    } else if (use_mipi) {
        cfg.backend = Shimeta::hv::Backend::Mipi;
        cfg.sensor_index = 9;
        std::printf("viewer: using MIPI backend (sensor_index=9)\n");
    } else {
        cfg.backend = Shimeta::hv::Backend::Usb;
        cfg.vendor_id  = (argc > 1) ? uint16_t(strtoul(argv[1], nullptr, 0)) : 0x1d6b;
        cfg.product_id = (argc > 2) ? uint16_t(strtoul(argv[2], nullptr, 0)) : 0x0105;
        std::printf("viewer: using USB backend\n");
    }
    cam.Init(cfg);
    if (!cam.StartStream()) {
        std::printf("viewer: no device — start failed (expected on host w/o camera)\n");
        return 0;
    }
    long total = 0;
    for (int i = 0; i < 10; ++i) {
        Shimeta::Frame f;
        if (cam.GetFrame(f, 1000) && f.evs.size > 0) {
            std::vector<Shimeta::EventCD> evs;
            if (use_mipi) {
                Shimeta::codec::MipiRaw8Decoder dec;
                dec.Decode(f.evs.data, f.evs.size, evs);
            } else {
                Shimeta::codec::Evt2Decoder dec;
                dec.Decode(f.evs.data, f.evs.size, evs);
            }
            total += long(evs.size());
        }
    }
    cam.StopStream();
    cam.Destroy();
    std::printf("viewer: decoded %ld events\n", total);
    return 0;
}
