// record: 把每帧的事件原始字节透传写入 RAW 文件（EventWriter.writeRaw）。
// USB 014（CF-NRS3 新格式）按 chunk 内容定档（"DVS1" 魔数）：DVS1 包流原样透传
// 落盘（EVT3 头几何 640×1024 = 设备画布；回放端 HybridReader 按几何逐包切读、
// player 分派 Evs014PacketDecoder），APS 为 Gray8（640×1024）以 8bpp 原样写 AVI；
// 003 路径不变（NV12 AVI）。两传感器均双流录制（EVS raw + APS AVI）。
// 默认 USB 后端；传 --mipi 切换到 MIPI 后端（S100/X5 板上 MIPI 相机）。
// --sensor-index N 覆盖默认 sensor 索引（S100 默认 9；X5 同配置在 49）。
// --duration S    录制秒数（默认 3；HVS 下 APS 30fps 可落 ~90 帧）。
// 双 VC 模式 tsmp chunk 写时间桥配对的 EVS sensor 时间戳（f.aps_evs_ts），
// 回放端据此做 1 APS ↔ N EVS 对齐（同旧 Demo hv_camera_live_record_timestamps）。
#include <shimetapi/hv/camera.h>
#include <shimetapi/hv/device_config.h>
#include <shimetapi/codec/evs_packet_codec.h>
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
    int sensor_index = 9;   // 默认 S100=9, X5=49（运行时 --sensor-index N 覆盖）
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
        cfg.vendor_id  = 0x1d6b;
        cfg.product_id = 0x0105;
        std::printf("record: using USB backend\n");
    }
    cam.Init(cfg);
    if (!cam.StartStream()) {
        std::printf("record: no device — start failed (expected on host w/o camera)\n");
        return 0;
    }
    Shimeta::io::HybridWriter w;
    bool writer_open = false;

    // GetFrame 只交付新派发帧；EVS/APS 各按 slab 指针去重，避免重复包/帧落盘。
    uint64_t evs_frames = 0;
    const uint8_t* last_evs_ptr = nullptr;
    const uint8_t* last_aps_ptr = nullptr;
    uint32_t aps_last_report = 0, tsmp_valid = 0;
    bool seen_aps = !use_mipi_hvs;
    bool is014_stream = false;   // 首帧定档结果（最终输出按此提示回放方式）
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

        if (!writer_open) {
            // 首帧到达后按 EVS chunk 内容定档：014 新格式包以 40B 头 "DVS1" 魔数开头
            // （变长包，≥40B）；003 为 32,768 倍数 RAW8 子帧（无该魔数）。
            const bool is014 = f.evs.size >= Shimeta::codec::kEvsHdrLen &&
                               Shimeta::codec::evsPacketLooksLikeHeader(f.evs.data);
            is014_stream = is014;
            // 014 EVS 头几何 = 设备画布 640×1024（包头事件坐标即原生坐标，非 1280 插值）；
            // 事件落盘为 DVS1 包流原样透传（EVT3 容器/编码不变；回放端 HybridReader
            // 按此几何逐包切读 + player 分派 Evs014PacketDecoder）。
            // APS 像素格式随传感器定档：014=Gray8（640×1024，8bpp AVI）、003=NV12。
            w.open("/tmp/hv_record.raw", "/tmp/hv_record.avi",
                   is014 ? 640 : 768, is014 ? 1024 : 608,
                   f.width > 0 ? unsigned(f.width) : (is014 ? 640u : 768u),
                   f.height > 0 ? unsigned(f.height) : (is014 ? 1024u : 608u),
                   Shimeta::io::RawFormat::Evt3, 30.0,
                   is014 ? Shimeta::PixelFormat::Gray8 : Shimeta::PixelFormat::NV12);
            writer_open = true;
        }

        // APS 去重：EVS 帧率远高于 APS（014 ~600:30），同一 APS slab 会随多个 EVS 帧
        // 重复派发；新 APS 帧必换池 slab（指针变化），据此跳过已写过的帧，避免 AVI
        // 虚增 N 倍重复帧（回放端 tsmp 无效时无从去重，014 USB 即此情形）。
        const uint8_t* aps_ptr = f.aps.size ? f.aps.data : nullptr;
        const bool new_aps = aps_ptr != nullptr && aps_ptr != last_aps_ptr;
        last_aps_ptr = aps_ptr;
        if (!new_aps) f.aps = {};   // 本帧 APS 已写过：仅落 EVS

        ++evs_frames;
        // HVS: APS 到达后，用当前 EVS 包首帧 timestamp 写入 APS tsmp。
        // 其他后端维持原有的时间桥优先、当前包 fallback 逻辑。
        // 014：extractEvsTimestamp 为 003 专用（014 包 ts 在 40B 包头内），USB 下
        // 时间桥与提取均无有效值 → tsmp 缺省（0/0，回放端按帧号配速）。
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
                    "(APS format=%s, APS frames=%u, EVS frames=%llu, ratio 1:%.1f, tsmp=%u)\n",
                    is014_stream ? "Gray8" : "NV12",
                    w.apsFrameCount(), (unsigned long long)evs_frames,
                    double(evs_frames) / double(w.apsFrameCount()), tsmp_valid);
        std::printf("record: replay with hv_sample_player /tmp/hv_record.raw /tmp/hv_record.avi\n");
    } else {
        std::printf("record: wrote /tmp/hv_record.raw (EVS frames=%llu, no APS on this backend)\n",
                    (unsigned long long)evs_frames);
        std::printf("record: replay with hv_sample_player /tmp/hv_record.raw\n");
    }
    return 0;
}
