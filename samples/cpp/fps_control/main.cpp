// fps_control: 运行时帧率控制教学——首帧几何判别 → 事件率基线 → 切档（设备重启）→ 重开回读验证 → 事件率对比。
//   ./hv_sample_fps_control [vid] [pid]    (默认 0x1d6b 0x0105)
// 教学点：Camera::GetFrameRate/SetFrameRate 的完整套路；合法档切档会触发设备端
//   流水线重启（USB 重新枚举、主机句柄失效）——切档后必须重开相机，而非沿用旧句柄。
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>
#include <shimetapi/codec/mipi_raw8_codec.h>
#include <shimetapi/hv/camera.h>
#include <shimetapi/hv/device_config.h>

using Clock = std::chrono::steady_clock;

/// 收流 seconds 秒并解码，返回事件总数（教学点：帧率档变化直接反映在事件密度上）。
static uint64_t countEvents(Shimeta::hv::Camera& cam, Shimeta::codec::MipiRaw8Decoder& dec, int seconds) {
    uint64_t total = 0;
    std::vector<Shimeta::EventCD> ev;
    auto t0 = Clock::now();
    while (std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - t0).count() < seconds) {
        Shimeta::Frame f;
        if (!cam.GetFrame(f, 1000)) continue;
        ev.clear();
        total += dec.Decode(f.evs.data, f.evs.size, ev);
    }
    return total;
}

/// 切档后设备端流水线重启（USB 重新枚举），旧句柄已失效——在重启窗口内循环重开。
/// 教学点：复用 Camera 对象前先 StopStream()+Destroy()（幂等安全）；每轮失败等 2s、
/// 默认 30 轮（总窗口 ≥60s，覆盖 014 流水线重启含 3A 预热的 ~30s）。
static bool reopenCamera(Shimeta::hv::Camera& cam, const Shimeta::hv::DeviceConfig& cfg, int attempts = 30) {
    for (int i = 1; i <= attempts; ++i) {
        cam.StopStream();
        cam.Destroy();
        if (cam.Init(cfg) && cam.StartStream()) return true;
        if (i % 5 == 0) std::printf("  设备重启中…重开相机（%d/%d）\n", i, attempts);
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    return false;
}

/// 切档并验证（正确姿势 = 确保相机可用 → 同档跳过 → SetFrameRate → 释放旧会话
/// → 重开相机 → 回读验证）。同档重设同样触发设备重启，故已是目标档时直接跳过。
static bool switchFps(Shimeta::hv::Camera& cam, const Shimeta::hv::DeviceConfig& cfg, uint16_t want) {
    // 确保相机可用：上一轮重开失败后处于关闭态（session 已释放），先救回来。
    // 探测：session 为空时 GetFrameRate 必返回 false。
    unsigned cur = 0;
    if (!cam.GetFrameRate(cur) && !reopenCamera(cam, cfg)) {
        std::printf("  重开相机失败\n");
        return false;
    }
    cam.GetFrameRate(cur);
    if (cur == want) {
        std::printf("  已是目标档 %u，跳过切档（避免无谓的设备重启）\n", want);
        return true;
    }
    if (!cam.SetFrameRate(want)) { std::printf("  SetFrameRate(%u) 失败\n", want); return false; }
    cam.StopStream();
    cam.Destroy();
    if (!reopenCamera(cam, cfg)) { std::printf("  重开相机失败\n"); return false; }
    unsigned got = 0;
    cam.GetFrameRate(got);
    std::printf("  SetFrameRate(%u) → 重开回读 %u %s\n", want, got, got == want ? "✓" : "✗");
    return got == want;
}

int main(int argc, char** argv) {
    using namespace Shimeta;
    hv::DeviceConfig cfg;
    cfg.backend    = hv::Backend::Usb;
    cfg.vendor_id  = (argc > 1) ? uint16_t(strtoul(argv[1], nullptr, 0)) : 0x1d6b;
    cfg.product_id = (argc > 2) ? uint16_t(strtoul(argv[2], nullptr, 0)) : 0x0105;
    std::printf("fps_control: using USB backend\n");

    hv::Camera cam;
    if (!cam.Init(cfg) || !cam.StartStream()) {
        std::printf("fps_control: no device — start failed\n");
        return 0;
    }
    codec::MipiRaw8Decoder dec;

    // 1) 型号判别：首帧几何（非破坏性）——014 帧宽 640，003 为 768。
    //    不用 SetFrameRate(700) 探测：合法档切档同样触发设备端流水线重启，
    //    而 003 上 700 是合法档，重启后回读失败会把 003 误判成 014。
    //    防护：dispLoop 在 imgLoop 首个 APS 帧就绪前派发的 Frame 不带几何（width==0），
    //    故循环取帧直到 width 就绪（预算 ~3s）；取帧失败或超预算 → 无法识别，优雅退出。
    Frame f0;
    const auto deadline = Clock::now() + std::chrono::seconds(3);
    while (f0.width == 0) {
        if (Clock::now() >= deadline || !cam.GetFrame(f0, 500)) {
            std::printf("fps_control: 无法识别传感器型号（3s 内未取得带几何的帧）\n");
            cam.StopStream();
            cam.Destroy();
            return 0;
        }
    }
    const bool is014 = (f0.width == 640);
    std::printf("传感器判别: %s（首帧几何 %dx%d）\n", is014 ? "APX014" : "APX003", f0.width, f0.height);
    std::printf("合法档位: %s\n", is014 ? "240 / 300 / 480 / 600" : "120 ~ 1000 任意值");

    // 2) 当前档 + 事件率基线（3 秒）
    unsigned cur = 0;
    cam.GetFrameRate(cur);
    std::printf("当前帧率档: GetFrameRate() = %u\n", cur);
    std::printf("\n-- 事件率基线（3s @ %u fps）--\n", cur);
    const uint64_t before = countEvents(cam, dec, 3);
    std::printf("  事件率: %.2f Mev/s\n", double(before) / 3.0 / 1e6);

    // 3) 切一档并验证（设备将重启流水线，主机重开相机）
    const uint16_t target = is014 ? 300 : 500;
    std::printf("\n-- 切换到 %u fps（设备将重启流水线，主机重开相机）--\n", target);
    switchFps(cam, cfg, target);
    const uint64_t after = countEvents(cam, dec, 3);
    std::printf("  事件率: %.2f Mev/s（切换前 %.2f）\n",
                double(after) / 3.0 / 1e6, double(before) / 3.0 / 1e6);

    // 4) 恢复默认档（同法：切档 → 重开 → 回读）
    std::printf("\n-- 恢复默认档 %u fps --\n", is014 ? 600 : 1000);
    switchFps(cam, cfg, is014 ? 600 : 1000);

    cam.StopStream();
    cam.Destroy();
    return 0;
}
