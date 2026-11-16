// viewer: 取每帧事件字节并解码、计数。
// MIPI 后端的 EVS 端点为 RAW8 子帧流（003）→ MipiRaw8Decoder；USB 后端按 chunk
// 内容分派（与 bench_hw 一致）：014 新格式变长包（40B 头 "DVS1" 魔数，载荷 EVT2）
// → Evs014PacketDecoder，003 RAW8 子帧 → MipiRaw8Decoder。
// --sensor-index N 覆盖默认 sensor 索引（S100 默认 9；X5 同配置在 49）。
#include <shimetapi/hv/camera.h>
#include <shimetapi/hv/device_config.h>
#include <shimetapi/codec/evs_packet_codec.h>  // 014 变长包（"DVS1"）
#include <shimetapi/codec/mipi_raw8_codec.h>   // 003 RAW8 子帧
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
    int sensor_index = HV_DEFAULT_SENSOR_INDEX;   // 由 CMake 按架构注入（S100=9, X5=49）
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--mipi") == 0) use_mipi = true;
        else if (std::strcmp(argv[i], "--mipi-hvs") == 0) use_mipi_hvs = true;
        else if (std::strcmp(argv[i], "--sensor-index") == 0 && i + 1 < argc)
            sensor_index = std::atoi(argv[++i]);
    }
    if (use_mipi_hvs) {
        cfg.backend = Shimeta::hv::Backend::MipiHvs;
        std::printf("viewer: using MIPI-HVS backend (APS+EVS)\n");
    } else if (use_mipi) {
        cfg.backend = Shimeta::hv::Backend::Mipi;
        cfg.sensor_index = sensor_index;
        std::printf("viewer: using MIPI backend (sensor_index=%d)\n", sensor_index);
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
    // USB 路径：解码器提到循环外复用——014 包聚帧是跨包有状态（同帧累计、
    // 帧号变化才冲刷上一帧），每轮新建会永远等不到冲刷、恒交付 0 事件。
    // 003 MipiRaw8Decoder 无状态，提到外面同样正确。
    // --mipi 路径保持每轮新建（003 无状态，沿用原样）。
    Shimeta::codec::MipiRaw8Decoder usb_dec;
    Shimeta::codec::Evs014PacketDecoder dec014;
    // 取 200 帧：014 聚帧在帧号变化时输出（最多 1 帧延迟），200 帧足够覆盖
    // 输出段；003 无聚合、行为不变（多帧只是多解码）。
    for (int i = 0; i < 200; ++i) {
        Shimeta::Frame f;
        if (cam.GetFrame(f, 1000) && f.evs.size > 0) {
            std::vector<Shimeta::EventCD> evs;
            if (use_mipi) {
                Shimeta::codec::MipiRaw8Decoder dec;
                dec.Decode(f.evs.data, f.evs.size, evs);
            } else if (f.evs.size >= Shimeta::codec::kEvsHdrLen &&
                       Shimeta::codec::evsPacketLooksLikeHeader(f.evs.data)) {
                // 014 新格式变长包（≥40B 且 "DVS1" 魔数）
                dec014.Decode(f.evs.data, f.evs.size, evs);
            } else {
                usb_dec.Decode(f.evs.data, f.evs.size, evs);
            }
            total += long(evs.size());
        }
    }
    // 014 聚帧尾冲刷：流结束输出最后一帧累计事件（003 无状态，Flush 恒 0）
    {
        std::vector<Shimeta::EventCD> evs;
        dec014.Flush(evs);
        total += long(evs.size());
    }
    cam.StopStream();
    cam.Destroy();
    std::printf("viewer: decoded %ld events\n", total);
    return 0;
}
