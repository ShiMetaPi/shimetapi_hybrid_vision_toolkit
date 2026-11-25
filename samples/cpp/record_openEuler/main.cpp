// record_openEuler: APX014BA USB 板卡录制示例；保留原 record 行为。
// 把每帧的事件原始字节透传写入 RAW 文件（EventWriter.writeRaw）。
// USB 按有效 APS 的格式和几何定档，避免首个 EVS chunk 不完整时误判型号。
// USB 014（CF-NRS3 新格式）：DVS1 包流原样透传
// 落盘（EVT3 头几何 640×1024 = 设备画布；回放端 HybridReader 按几何逐包切读、
// player 分派 Evs014PacketDecoder），APS 为 Gray8（640×1024）以 8bpp 原样写 AVI；
// 003 路径不变（NV12 AVI）。两传感器均双流录制（EVS raw + APS AVI）。
// 默认 USB 后端；传 --mipi 切换到 MIPI 后端（S100/X5 板上 MIPI 相机）。
// --sensor-index N 覆盖默认 sensor 索引（S100 默认 9；X5 同配置在 49）。
// --duration S    正式录制秒数（默认 3），不包含 USB 前 10 张 APS 的初始化时间。
// 双 VC 模式 tsmp chunk 写时间桥配对的 EVS sensor 时间戳（f.aps_evs_ts），
// 回放端据此做 1 APS ↔ N EVS 对齐（同旧 Demo hv_camera_live_record_timestamps）。
#include <shimetapi/hv/camera.h>
#include <shimetapi/hv/device_config.h>
#include <shimetapi/codec/evs_packet_codec.h>
#include <shimetapi/codec/mipi_raw8_codec.h>
#include <shimetapi/io/hybrid_writer.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <fstream>
#include <iomanip>
#include <cmath>

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    Shimeta::hv::Camera cam;
    Shimeta::hv::DeviceConfig cfg;
    // 检查是否指定 MIPI / MIPI-HVS 后端
    bool use_mipi = false;
    bool use_mipi_hvs = false;
    int sensor_index = 9;   // 默认 S100=9, X5=49（运行时 --sensor-index N 覆盖）
    double duration_s = 3.0;
    const int warmup_frames = 10;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--mipi") == 0) use_mipi = true;
        else if (std::strcmp(argv[i], "--mipi-hvs") == 0) use_mipi_hvs = true;
        else if (std::strcmp(argv[i], "--sensor-index") == 0 && i + 1 < argc)
            sensor_index = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--duration") == 0 && i + 1 < argc)
            duration_s = std::atof(argv[++i]);
    }
    if (!std::isfinite(duration_s) || duration_s <= 0) {
        std::fprintf(stderr, "record: duration must be finite and positive\n");
        return 2;
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
        cfg.vendor_id  = 0x1d6b;
        cfg.product_id = 0x0105;
        std::printf("record: using USB backend\n");
    }
    if (!cam.Init(cfg) || !cam.StartStream()) {
        std::printf("record: no device — start failed (expected on host w/o camera)\n");
        return 1;
    }
    Shimeta::io::HybridWriter w;
    bool writer_open = false;
    std::ofstream timing;
    bool timing_failed = false;

    // GetFrame 只交付新派发帧；池地址会复用，不能据此丢弃新 EVS 包。
    uint64_t evs_frames = 0;
    const uint8_t* last_aps_ptr = nullptr;
    int64_t last_aps_ts = 0;
    uint64_t aps_seen = 0, aps_candidates = 0, aps_write_failures = 0;
    uint32_t aps_last_report = 0, tsmp_valid = 0;
    const bool require_aps = !use_mipi || use_mipi_hvs;
    bool is014_stream = false;   // 首帧定档结果（最终输出按此提示回放方式）
    using Clock = std::chrono::steady_clock;
    const auto init_deadline = Clock::now() + std::chrono::seconds(15);
    Clock::time_point record_start{}, deadline{};
    bool recording = false;
    unsigned warmup_count = 0;
    const uint8_t* warmup_ptr = nullptr;
    int64_t warmup_ts = 0;
    const bool usb_warmup = cfg.backend == Shimeta::hv::Backend::Usb;
    if (usb_warmup)
        std::printf("record: initializing; discard first %d distinct APS frames; RAW/AVI saving disabled\n", warmup_frames);
    while (true) {
        if (recording && Clock::now() >= deadline) break;
        if (!recording && Clock::now() >= init_deadline) {
            std::fprintf(stderr, "record: initialization timed out before recording\n");
            break;
        }
        Shimeta::Frame f;
        if (!cam.GetFrame(f, 100)) continue;
        // GetFrame can block past the deadline. Never write that late result.
        if (recording && Clock::now() >= deadline) break;
        if (!recording && Clock::now() >= init_deadline) break;
        if (f.evs.size == 0) continue;
        const bool has_aps = f.aps.data && f.aps.size > 0 && f.width > 0 && f.height > 0 &&
            (f.format == Shimeta::PixelFormat::Gray8 || f.format == Shimeta::PixelFormat::NV12);
        if (has_aps) ++aps_seen;
        if (!recording && usb_warmup && warmup_count < unsigned(warmup_frames)) {
            if (has_aps && (f.ts.aps_ts_ns != warmup_ts ||
                           (f.ts.aps_ts_ns == 0 && f.aps.data != warmup_ptr))) {
                warmup_ts = f.ts.aps_ts_ns;
                warmup_ptr = f.aps.data;
                ++warmup_count;
                std::printf("record: initializing APS %u/%d discarded (ts=%lld)\n",
                            warmup_count, warmup_frames, (long long)warmup_ts);
            }
            continue;  // also discard EVS throughout initialization
        }
        // Wait for the first new APS after the ten discarded frames.
        if (!recording && usb_warmup && (!has_aps ||
            (f.ts.aps_ts_ns == warmup_ts &&
             (warmup_ts != 0 || f.aps.data == warmup_ptr)))) continue;


        if (!writer_open) {
            // USB/HVS 双流录制先等有效 APS；EVS 首块缺魔数不等于 APX003。
            if (require_aps && !has_aps) continue;
            const bool is014 = cfg.backend == Shimeta::hv::Backend::Usb
                ? f.format == Shimeta::PixelFormat::Gray8 && f.width == 640 && f.height == 1024
                : f.evs.size >= Shimeta::codec::kEvsHdrLen &&
                  Shimeta::codec::evsPacketLooksLikeHeader(f.evs.data);
            is014_stream = is014;
            // 014 EVS 头几何 = 设备画布 640×1024（包头事件坐标即原生坐标，非 1280 插值）；
            // 事件落盘为 DVS1 包流原样透传（EVT3 容器/编码不变；回放端 HybridReader
            // 按此几何逐包切读 + player 分派 Evs014PacketDecoder）。
            // APS 像素格式随传感器定档：014=Gray8（640×1024，8bpp AVI）、003=NV12。
            std::printf("record: profile=%s APS=%dx%d format=%s bytes=%zu EVS-header=%s\n",
                        is014 ? "APX014" : "003/HVS", f.width, f.height,
                        f.format == Shimeta::PixelFormat::Gray8 ? "Gray8" : "NV12",
                        f.aps.size, f.evs.size >= Shimeta::codec::kEvsHdrLen &&
                        Shimeta::codec::evsPacketLooksLikeHeader(f.evs.data) ? "DVS1" : "other");
            if (!w.open("/tmp/hv_record_openEuler.raw", "/tmp/hv_record_openEuler.avi",
                   is014 ? 640 : 768, is014 ? 1024 : 608,
                   f.width > 0 ? unsigned(f.width) : (is014 ? 640u : 768u),
                   f.height > 0 ? unsigned(f.height) : (is014 ? 1024u : 608u),
                   Shimeta::io::RawFormat::Evt3, 30.0,
                   is014 ? Shimeta::PixelFormat::Gray8 : Shimeta::PixelFormat::NV12)) {
                std::fprintf(stderr, "record: cannot open recording output\n");
                cam.StopStream(); cam.Destroy();
                return 1;
            }
            timing.open("/tmp/hv_record_openEuler.avi.timestamps.csv", std::ios::trunc);
            timing << "frame,aps_ts_ns,fnv1a64\n";
            if (!timing) {
                std::fprintf(stderr, "record: cannot create APS timestamp sidecar\n");
                cam.StopStream(); cam.Destroy(); return 1;
            }
            writer_open = true;
            record_start = Clock::now();
            deadline = record_start + std::chrono::duration_cast<Clock::duration>(
                std::chrono::duration<double>(duration_s));
            recording = true;
            std::printf("record: initialization complete; recording %.6f seconds (host monotonic window)\n", duration_s);
        }

        // APS 去重：EVS 帧率远高于 APS（014 ~600:30），同一 APS slab 会随多个 EVS 帧
        // 重复派发；新 APS 帧必换池 slab（指针变化），据此跳过已写过的帧，避免 AVI
        // 虚增 N 倍重复帧（回放端 tsmp 无效时无从去重，014 USB 即此情形）。
        const uint8_t* aps_ptr = f.aps.size ? f.aps.data : nullptr;
        const bool new_aps = aps_ptr != nullptr &&
            (aps_ptr != last_aps_ptr || f.ts.aps_ts_ns != last_aps_ts);
        if (new_aps) {
            last_aps_ptr = aps_ptr;
            last_aps_ts = f.ts.aps_ts_ns;
            ++aps_candidates;
        }
        if (!new_aps) f.aps = {};   // 本帧 APS 已写过：仅落 EVS

        // Hashing/writing the previous frame may have consumed the remaining time.
        if (Clock::now() >= deadline) break;
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
        const auto aps_before = w.apsFrameCount();
        w.writeFrame(f, has_ts ? &evs_ts : nullptr);
        if (w.apsFrameCount() > aps_before) {
            // Hash the exact payload passed to AVI, not the reusable USB buffer.
            uint64_t hash = 14695981039346656037ULL;
            const size_t bytes = size_t(f.width) * size_t(f.height) *
                (f.format == Shimeta::PixelFormat::NV12 ? 3 : 2) / 2;
            for (size_t i = 0; i < bytes; ++i) { hash ^= f.aps.data[i]; hash *= 1099511628211ULL; }
            timing << aps_before << ',' << f.ts.aps_ts_ns << ',' << std::hex << hash << std::dec << '\n';
            if (!timing) { timing_failed = true; break; }
        }
        if (new_aps && w.apsFrameCount() == aps_before) {
            if (++aps_write_failures <= 3)
                std::fprintf(stderr, "record: APS not written: format=%d bytes=%zu geometry=%dx%d (check format, size and disk space)\n",
                             int(f.format), f.aps.size, f.width, f.height);
        }
        if (w.apsFrameCount() > aps_last_report) {
            aps_last_report = w.apsFrameCount();
            if (aps_last_report % 30 == 0)
                std::printf("record: %u APS frames, %llu EVS frames\n",
                            aps_last_report, (unsigned long long)evs_frames);
        }
    }
    if (recording)
        std::printf("record: window closed; requested=%.6fs elapsed=%.6fs; no further RAW/AVI writes\n",
                    duration_s, std::chrono::duration<double>(Clock::now()-record_start).count());
    timing.close();
    if (timing.fail()) timing_failed = true;
    if (recording && w.apsFrameCount() > 0) {
        const double saved_seconds = std::min(duration_s,
            std::chrono::duration<double>(Clock::now() - record_start).count());
        if (!w.setApsPlaybackDuration(saved_seconds)) {
            std::fprintf(stderr, "record: cannot finalize AVI playback duration\n");
            timing_failed = true;
        }
    }
    w.close();
    cam.StopStream();
    cam.Destroy();
    std::printf("record: APS snapshots=%llu unique-candidates=%llu write-failures=%llu\n",
                (unsigned long long)aps_seen, (unsigned long long)aps_candidates,
                (unsigned long long)aps_write_failures);
    if (!writer_open) {
        std::fprintf(stderr, "record: no recording created: no usable %s frames received\n",
                     require_aps ? "EVS+APS" : "EVS");
        return 1;
    }
    if (w.apsFrameCount() > 0) {
        std::printf("record: wrote /tmp/hv_record_openEuler.raw and /tmp/hv_record_openEuler.avi "
                    "(APS format=%s, APS frames=%u, EVS frames=%llu, ratio 1:%.1f, tsmp=%u)\n",
                    is014_stream ? "Gray8" : "NV12",
                    w.apsFrameCount(), (unsigned long long)evs_frames,
                    double(evs_frames) / double(w.apsFrameCount()), tsmp_valid);
        std::printf("record: replay with hv_sample_player /tmp/hv_record_openEuler.raw /tmp/hv_record_openEuler.avi\n");
    } else {
        std::printf("record: wrote /tmp/hv_record_openEuler.raw (EVS frames=%llu, no APS frames written)\n",
                    (unsigned long long)evs_frames);
        std::printf("record: replay with hv_sample_player /tmp/hv_record_openEuler.raw\n");
    }
    if (timing_failed) std::fprintf(stderr, "record: timestamp sidecar write failed\n");
    std::printf("record: APS timing: /tmp/hv_record_openEuler.avi.timestamps.csv (use recording_player)\n");
    return (timing_failed || aps_write_failures || (require_aps && w.apsFrameCount() == 0)) ? 1 : 0;
}
