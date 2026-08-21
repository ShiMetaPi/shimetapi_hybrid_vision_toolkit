// record: 把每帧的事件原始字节透传写入 RAW 文件（EventWriter.writeRaw）。
// 默认 USB 后端；传 --mipi 切换到 MIPI 后端（S100/X5 板上 MIPI 相机）。
// --sensor-index N 覆盖默认 sensor 索引（S100 默认 9；X5 同配置在 49）。
// --duration S    录制秒数（默认 3；HVS 下 APS 30fps 可落 ~90 帧）。
// 双 VC 模式 tsmp chunk 写时间桥配对的 EVS sensor 时间戳（f.aps_evs_ts），
// 回放端据此做 1 APS ↔ N EVS 对齐（同旧 Demo hv_camera_live_record_timestamps）。
#include <shimetapi/hv/camera.h>
#include <shimetapi/hv/device_config.h>
#include <shimetapi/codec/mipi_raw8_codec.h>
#include <shimetapi/io/hybrid_writer.h>
#include <chrono>
#include <cstdint>
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
    int sensor_index = HV_DEFAULT_SENSOR_INDEX;   // 由 CMake 按架构注入（S100=9, X5=49）
    double duration_s = 3.0;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--mipi") == 0) use_mipi = true;
        else if (std::strcmp(argv[i], "--mipi-hvs") == 0) use_mipi_hvs = true;
        else if (std::strcmp(argv[i], "--sensor-index") == 0 && i + 1 < argc)
            sensor_index = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--duration") == 0 && i + 1 < argc)
            duration_s = std::atof(argv[++i]);
    }
    if (duration_s <= 0) duration_s = 3.0;
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

    // 边沿触发消费：latest_frame_ 只在 dispLoop 弹出新事件包时刷新（HVS 下
    // 包到达节奏 ~4ms）。GetFrame 是电平触发（谓词只看 evs.size>0），必须按
    // EVS slab 指针自行去重，否则同一包会被重复落盘。
    uint64_t evs_frames = 0;
    const uint8_t* last_evs_ptr = nullptr;
    uint32_t aps_last_report = 0, tsmp_valid = 0;
    bool seen_aps = !use_mipi_hvs;
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::duration<double>(duration_s);
    while (std::chrono::steady_clock::now() < deadline) {
        Shimeta::Frame f;
        if (!cam.GetFrame(f, 100)) continue;
        if (f.evs.size == 0 || f.evs.data == last_evs_ptr) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;                       // 同一事件包的重复快照，不重复落盘
        }
        last_evs_ptr = f.evs.data;

        if (use_mipi_hvs && !seen_aps) {
            const bool has_aps = f.aps.data != nullptr && f.aps.size > 0 &&
                                 f.format == Shimeta::PixelFormat::NV12;
            if (!has_aps) continue;
            seen_aps = true;
        }

        ++evs_frames;
        // HVS: APS 到达后，用当前 EVS 包首帧 timestamp 写入 APS tsmp。
        // 其他后端维持原有的时间桥优先、当前包 fallback 逻辑。
        Shimeta::EvsTimestamp evs_ts = use_mipi_hvs
            ? Shimeta::codec::extractEvsTimestamp(f.evs.data, f.evs.size)
            : f.aps_evs_ts;
        if (!evs_ts.valid && f.evs.size > 0)
            evs_ts = Shimeta::codec::extractEvsTimestamp(f.evs.data, f.evs.size);
        const bool has_ts = evs_ts.valid;
        if (has_ts) ++tsmp_valid;
        w.writeFrame(f, has_ts ? &evs_ts : nullptr);
        if (w.apsFrameCount() > aps_last_report) {
            aps_last_report = w.apsFrameCount();
            if (aps_last_report % 30 == 0)
                std::printf("record: %u APS frames, %llu EVS frames\n",
                            aps_last_report, (unsigned long long)evs_frames);
        }
    }
    w.close();
    cam.StopStream();
    cam.Destroy();
    if (w.apsFrameCount() > 0) {
        std::printf("record: wrote /tmp/hv_record.raw and /tmp/hv_record.avi "
                    "(APS frames=%u, EVS frames=%llu, ratio 1:%.1f, tsmp=%u)\n",
                    w.apsFrameCount(), (unsigned long long)evs_frames,
                    double(evs_frames) / double(w.apsFrameCount()), tsmp_valid);
    } else {
        std::printf("record: wrote /tmp/hv_record.raw (EVS frames=%llu, no APS on this backend)\n",
                    (unsigned long long)evs_frames);
    }
    return 0;
}
