// fps_check: 板端 evs_fps 档位验证小工具（临时，不入仓）
#include <cstdio>
#include <cstdlib>
#include <shimetapi/hv/camera.h>
#include <shimetapi/hv/device_config.h>
int main(int argc, char** argv) {
    Shimeta::hv::DeviceConfig cfg;
    cfg.backend = Shimeta::hv::Backend::MipiHvs;
    cfg.evs_fps = (argc > 1) ? unsigned(atoi(argv[1])) : 500;   // 档位参数
    printf("fps_check: evs_fps=%u\n", cfg.evs_fps);
    Shimeta::hv::Camera cam;
    cam.Init(cfg);
    if (!cam.StartStream()) { printf("fps_check: start FAILED\n"); return 1; }
    for (int i = 0; i < 5; ++i) {
        Shimeta::Frame f;
        if (cam.GetFrame(f, 3000))
            printf("fps_check: frame %d evs=%zu bytes aps=%zu bytes\n", i, f.evs.size, f.aps.size);
    }
    cam.StopStream(); cam.Destroy();
    return 0;
}
