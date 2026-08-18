// bench_hw: timed real-HW USB bench.
//   ./hv_sample_bench_hw [vid] [pid] [duration_s]    (default 0x1d6b 0x0105 5)
// Opens the USB camera (vid:pid), streams for <duration_s> seconds, decodes EVT2
// events and counts APS frames, then prints Mev/s + APS fps.
// Needs the camera online + permissions (sudo or the udev rule from the README).
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include <shimetapi/core/event_cd.h>
#include <shimetapi/codec/evt2_codec.h>
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
    cfg.event_fmt  = hv::EventFormat::Evt2;   // USB event endpoint pushes EVT2
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
    codec::Evt2Decoder dec;
    std::vector<EventCD> out;
    auto t0 = Clock::now();
    while (true) {
        if (std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - t0).count() >= dur) break;
        Frame f;
        if (!cam.GetFrame(f, 1000)) continue;
        ++frames;
        if (f.evs.size > 0) {
            out.clear();
            total_ev += dec.Decode(f.evs.data, f.evs.size, out);
        }
        if (f.aps.size > 0) ++aps_frames;
    }
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
