// bench_hw: timed real-HW USB bench.
//   ./hv_sample_bench_hw [vid] [pid] [duration_s]    (default 0x1d6b 0x0105 5)
// Opens the USB camera (vid:pid), streams for <duration_s> seconds, decodes events
// (by chunk content: 014 new-format variable packet "DVS1" -> Evs014PacketDecoder;
// otherwise 003 RAW8 subframe -> MipiRaw8Decoder) and counts APS frames,
// then prints Mev/s + APS fps.
// Needs the camera online + permissions (sudo or the udev rule from the README).
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include <shimetapi/core/event_cd.h>
#include <shimetapi/codec/evs_packet_codec.h>
#include <shimetapi/codec/mipi_raw8_codec.h>
#include <shimetapi/hv/camera.h>
#include <shimetapi/hv/device_config.h>

using Clock = std::chrono::steady_clock;

int main(int argc, char** argv) {
    using namespace Shimeta;
    unsigned vid = 0x1d6b, pid = 0x0105;
    int dur = 5;
    if (argc > 1) vid = unsigned(std::strtoul(argv[1], nullptr, 0));
    if (argc > 2) pid = unsigned(std::strtoul(argv[2], nullptr, 0));
    if (argc > 3) dur = std::atoi(argv[3]);
    if (dur <= 0) dur = 5;
    std::printf("bench_hw: USB 0x%04x:0x%04x for %ds\n", vid, pid, dur);

    hv::Camera cam;
    hv::DeviceConfig cfg;
    cfg.backend    = hv::Backend::Usb;
    cfg.vendor_id  = uint16_t(vid);
    cfg.product_id = uint16_t(pid);
    if (!cam.Init(cfg)) {
        std::fprintf(stderr, "bench_hw: Init failed (0x%04x:0x%04x not found / no permission).\n", vid, pid);
        return 1;
    }
    if (!cam.StartStream()) {
        std::fprintf(stderr, "bench_hw: StartStream failed.\n");
        cam.Destroy();
        return 1;
    }

    uint64_t total_ev = 0;
    int frames = 0, aps_frames = 0;
    codec::MipiRaw8Decoder dec;        // 003 RAW8 子帧（无状态）
    codec::Evs014PacketDecoder dec014; // 014 变长包聚帧（有状态：跨包累计同帧事件）
    std::vector<EventCD> out;
    auto t0 = Clock::now();
    while (true) {
        if (std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - t0).count() >= dur) break;
        Frame f;
        if (!cam.GetFrame(f, 1000)) continue;
        ++frames;
        if (f.evs.size > 0) {
            out.clear();
            // 按 chunk 内容分派：≥40B 且 40B 头 "DVS1" 魔数 → 014 变长包；
            // 否则 003 RAW8 子帧流（32,768 倍数）。同型号全流一致，首帧即定档。
            if (f.evs.size >= codec::kEvsHdrLen && codec::evsPacketLooksLikeHeader(f.evs.data))
                total_ev += dec014.Decode(f.evs.data, f.evs.size, out);
            else
                total_ev += dec.Decode(f.evs.data, f.evs.size, out);
        }
        if (f.aps.size > 0) ++aps_frames;
    }
    // 014 聚帧尾：流结束时冲刷最后一帧累计事件（帧号变化才输出的那半拍）
    out.clear();
    total_ev += dec014.Flush(out);
    auto t1 = Clock::now();
    cam.StopStream();
    cam.Destroy();

    double el  = std::chrono::duration<double>(t1 - t0).count();
    double mev = el > 0 ? double(total_ev) / el / 1e6 : 0.0;
    double fps = el > 0 ? double(aps_frames) / el : 0.0;
    std::printf("bench_hw: %.2f Mev/s (%llu events) | APS %.1f fps (%d APS frames / %d total) over %.2fs\n",
                mev, (unsigned long long)total_ev, fps, aps_frames, frames, el);
    return 0;
}
