// record: 把每帧的事件原始字节透传写入 RAW 文件（EventWriter.writeRaw）。
// 默认 USB 后端；传 --mipi 切换到 MIPI 后端（S100/X5 板上 MIPI 相机）。
// --sensor-index N 覆盖默认 sensor 索引（S100 默认 9；X5 同配置在 49）。
#include <shimetapi/hv/camera.h>
#include <shimetapi/hv/device_config.h>
#include <shimetapi/io/hybrid_writer.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
int main(int argc, char** argv) {
    Shimeta::hv::Camera cam;
    Shimeta::hv::DeviceConfig cfg;
    // 检查是否指定 MIPI / MIPI-HVS 后端
    bool use_mipi = false;
    bool use_mipi_hvs = false;
    int sensor_index = HV_DEFAULT_SENSOR_INDEX;   // 由 CMake 按架构注入（S100=9, X5=49）
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--mipi") == 0) use_mipi = true;
        else if (std::strcmp(argv[i], "--mipi-hvs") == 0) use_mipi_hvs = true;
        else if (std::strcmp(argv[i], "--sensor-index") == 0 && i + 1 < argc)
            sensor_index = std::atoi(argv[++i]);
    }
    if (use_mipi_hvs) {
        cfg.backend = Shimeta::hv::Backend::MipiHvs;
        std::printf("record: using MIPI-HVS backend (APS+EVS)\n");
    } else if (use_mipi) {
        cfg.backend = Shimeta::hv::Backend::Mipi;
        cfg.sensor_index = sensor_index;
        std::printf("record: using MIPI backend (sensor_index=%d)\n", sensor_index);
    } else {
        cfg.backend = Shimeta::hv::Backend::Usb;
        cfg.vendor_id  = (argc > 1) ? uint16_t(strtoul(argv[1], nullptr, 0)) : 0x1d6b;
        cfg.product_id = (argc > 2) ? uint16_t(strtoul(argv[2], nullptr, 0)) : 0x0105;
        std::printf("record: using USB backend\n");
    }
    cam.Init(cfg);
    if (!cam.StartStream()) {
        std::printf("record: no device — start failed (expected on host w/o camera)\n");
        return 0;
    }
    Shimeta::io::HybridWriter w;
    w.open("/tmp/hv_record.raw", "/tmp/hv_record.avi", 768, 608);
    // 双 VC 模式下 APS(~30fps)晚于 EVS(~240fps)就绪：先等首张 APS 到达再录，
    // 否则开头 ~50ms 的事件帧都还没有 APS 分量（X5 上 10 帧窗口全撞在预热期）。
    bool saw_aps = false;
    for (int i = 0; i < 100; ++i) {          // 最多 ~1s 等待 APS 预热
        Shimeta::Frame f;
        if (!cam.GetFrame(f, 100)) continue;
        if (f.aps.size > 0) { saw_aps = true; break; }
    }
    for (int i = 0; i < 10; ++i) {
        Shimeta::Frame f;
        if (!cam.GetFrame(f, 1000)) continue;
        w.writeFrame(f);
    }
    w.close();
    (void)saw_aps;
    cam.StopStream();
    cam.Destroy();
    if (w.apsFrameCount() > 0) {
        std::printf("record: wrote /tmp/hv_record.raw and /tmp/hv_record.avi (APS frames=%u)\n", w.apsFrameCount());
    } else {
        std::printf("record: wrote /tmp/hv_record.raw (no APS frames on this backend)\n");
    }
    return 0;
}
