// ts_sync: EVS↔APS 同源时间戳配对教学示例（GUI；无显示器自动回退纯文本）。
//   ./hv_sample_ts_sync [vid] [pid]    (默认 0x1d6b 0x0105)
// 核心机制：APS 帧时间戳与事件 t 同源同域。收到 APS 帧 → 在 EVS 帧序列中二分查找
// 相等/最近 ts 的帧配对显示。
//   003：APS 尾 8B（µs×1000=ns）与事件 t(µs) 配对；
//   014（CF-NRS3 新格式）：APS 80B 帧头墙钟 ns 与包事件 t（包头 sec×1e6+usec，µs）配对。
// 架构（教学点⑥）：环形缓存必须由事件回调满速喂——GetFrame 的 latest 语义会跳帧，
//   稀疏环会让配对退化为稀疏网格最近邻（014@600fps 实测 1/10 稀疏 → 1375µs 量化残差）；
//   故 EVS 解码+喂环在事件回调线程满速进行，主循环只取"最新 APS"+加锁快照做配对与渲染。
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>
#include <opencv2/opencv.hpp>
#include <shimetapi/codec/evs_packet_codec.h>
#include <shimetapi/codec/mipi_raw8_codec.h>
#include <shimetapi/hv/camera.h>
#include <shimetapi/hv/device_config.h>

namespace {

constexpr int kMaxEvsFrames = 900;   // 滚动缓存 ≈1.5s @600fps

struct EvsFrame {
    uint64_t ts = 0;                 // 事件 µs（003=子帧 ts/200；014=包头墙钟 sec×1e6+usec，无校准）
    cv::Mat  polarity;               // 单通道极性图（1=ON, 2=OFF）
};

/// 事件按 t 分组成帧并渲染极性图（同一 t 的全部事件为一帧，run-length 分组对 003/014 均成立）。
static void appendEvsFrames(std::deque<EvsFrame>& ring, int w, int h,
                            const std::vector<Shimeta::EventCD>& events) {
    size_t i = 0;
    while (i < events.size()) {
        const uint64_t t0 = uint64_t(events[i].t);
        EvsFrame f;
        f.ts = t0;
        f.polarity = cv::Mat::zeros(h, w, CV_8UC1);
        size_t j = i;
        while (j < events.size() && uint64_t(events[j].t) == t0) {
            const auto& e = events[j];
            if (e.x < w && e.y < h) f.polarity.at<uint8_t>(int(e.y), int(e.x)) = e.polarity ? 1 : 2;
            ++j;
        }
        ring.push_back(std::move(f));
        if (ring.size() > kMaxEvsFrames) ring.pop_front();
        i = j;
    }
}

/// 二分：ts ≤ target 的最大帧索引（ring 按 ts 升序）。
static int nearestFrame(const std::deque<EvsFrame>& ring, uint64_t target) {
    int lo = 0, hi = int(ring.size()) - 1;
    if (hi < 0) return -1;
    if (ring[lo].ts >= target) return lo;
    if (ring[hi].ts <= target) return hi;
    while (lo + 1 < hi) {
        const int mid = (lo + hi) / 2;
        if (ring[size_t(mid)].ts <= target) lo = mid; else hi = mid;
    }
    const uint64_t dlo = target - ring[size_t(lo)].ts;
    const uint64_t dhi = ring[size_t(hi)].ts - target;
    return dlo <= dhi ? lo : hi;
}

static cv::Mat renderColor(const cv::Mat& polarity) {
    cv::Mat dst(polarity.rows, polarity.cols, CV_8UC3, cv::Scalar(0, 0, 0));
    for (int y = 0; y < polarity.rows; ++y) {
        const uint8_t* s = polarity.ptr<uint8_t>(y);
        cv::Vec3b* d = dst.ptr<cv::Vec3b>(y);
        for (int x = 0; x < polarity.cols; ++x)
            if (s[x] == 1) d[x] = cv::Vec3b(255, 0, 0);        // ON 蓝
            else if (s[x] == 2) d[x] = cv::Vec3b(0, 0, 255);   // OFF 红
    }
    return dst;
}

static cv::Mat nv12ToBgr(const uint8_t* data, int w, int h) {
    const cv::Mat y(h, w, CV_8UC1, const_cast<uint8_t*>(data));
    const cv::Mat uv(h / 2, w / 2, CV_8UC2, const_cast<uint8_t*>(data) + size_t(w) * h);
    cv::Mat bgr;
    cv::cvtColorTwoPlane(y, uv, bgr, cv::COLOR_YUV2BGR_NV12);
    return bgr;
}

/// 事件回调线程与主循环共享的环形缓存（回调侧 append 持锁；主循环配对读持锁，拷贝命中帧后释放再渲染）。
struct SharedState {
    std::mutex           mu;
    std::deque<EvsFrame> ring;             // 事件回调满速喂入的 EVS 帧序列
    int                  evs_w = 0, evs_h = 0;   // 事件网格几何（首包按 chunk 长度定档）
    uint64_t             last_ev_ts = 0;
    long                 mono_violation = 0;
};

/// 图像回调线程发布的"最新 APS 帧"（字节拷贝——回调返回后 slab 可能被池回收）。
struct LatestAps {
    std::mutex           mu;
    uint64_t             seq = 0;     // 每帧 +1：主循环据此识别新 APS 帧
    uint64_t             ts_raw = 0;  // APS ts 原值（003 尾=ns、014 帧头墙钟=ns、0=无效）
    int                  width = 0, height = 0;
    Shimeta::PixelFormat fmt = Shimeta::PixelFormat::NV12;   // 003=NV12 / 014=Gray8
    std::vector<uint8_t> nv12;                      // 原始像素字节（按 fmt 解释）
};

} // namespace

int main(int argc, char** argv) {
    using namespace Shimeta;
    hv::DeviceConfig cfg;
    cfg.backend    = hv::Backend::Usb;
    cfg.vendor_id  = (argc > 1) ? uint16_t(strtoul(argv[1], nullptr, 0)) : 0x1d6b;
    cfg.product_id = (argc > 2) ? uint16_t(strtoul(argv[2], nullptr, 0)) : 0x0105;
    std::printf("ts_sync: using USB backend\n");

    // 声明序保证回调捕获的对象存活到 cam.Destroy() 之后（析构逆序：cam 先亡）
    codec::MipiRaw8Decoder dec;          // 003 RAW8 子帧（仅事件回调线程 Decode）
    codec::Evs014PacketDecoder dec014;   // 014 变长包聚帧（仅事件回调线程 Decode）
    SharedState shared;
    LatestAps latest;
    hv::Camera cam;
    if (!cam.Init(cfg)) {
        std::printf("ts_sync: no device — start failed\n");
        return 0;
    }

    // 事件回调（dispatch 线程，每派发帧一次）：满速解码 + 喂环。
    // 教学点⑥：环形缓存必须由事件回调满速喂——GetFrame 的 latest 语义每秒只消费 ~60 帧
    // （解码+渲染代价），环只被喂 1/10 的帧，配对落在稀疏网格上（014@600fps 实测 1375µs 量化残差）。
    cam.SetEventCallback([&](const hv::EventPacket& pkt) {
        std::vector<EventCD> ev;
        // 按内容分派：014 新格式变长包以 40B 头 "DVS1" 魔数开头；003 为 RAW8 子帧流
        const bool is014 = pkt.data.size >= codec::kEvsHdrLen &&
                           codec::evsPacketLooksLikeHeader(pkt.data.data);
        if (is014) dec014.Decode(pkt.data.data, pkt.data.size, ev);
        else       dec.Decode(pkt.data.data, pkt.data.size, ev);
        std::lock_guard<std::mutex> lk(shared.mu);
        if (shared.evs_w == 0) {     // 几何定档：同上按包内容分派
            if (is014) {
                shared.evs_w = 640; shared.evs_h = 1024;              // 014：640×1024（设备画布，无插值）
            } else {
                shared.evs_w = codec::MipiRaw8Layout::kEvsWidth;      // 003：768×608
                shared.evs_h = codec::MipiRaw8Layout::kEvsHeight;
            }
        }
        appendEvsFrames(shared.ring, shared.evs_w, shared.evs_h, ev);
        // 单调性检查（教学点④：EVS ts 应单调；回绕/跳变即计数）
        if (!shared.ring.empty() && shared.ring.back().ts < shared.last_ev_ts) ++shared.mono_violation;
        if (!shared.ring.empty()) shared.last_ev_ts = shared.ring.back().ts;
    });

    // 图像回调：存"最新 APS 帧"（像素拷贝 + ts 原值），主循环直接消费
    cam.SetImageCallback([&](const hv::ImageData& img) {
        std::lock_guard<std::mutex> lk(latest.mu);
        ++latest.seq;
        latest.ts_raw = uint64_t(img.ts.aps_ts_ns);
        latest.width  = img.width;
        latest.height = img.height;
        latest.fmt    = img.format;
        latest.nv12.assign(img.pixels.data, img.pixels.data + img.pixels.size);
    });

    if (!cam.StartStream()) {
        std::printf("ts_sync: no device — start failed\n");
        return 0;
    }

    // GUI（失败回退纯文本）
    const std::string win = "ts_sync (q quit)";
    bool gui = true;
    try { cv::namedWindow(win, cv::WINDOW_AUTOSIZE); }
    catch (const cv::Exception&) {
        gui = false;
        std::printf("无显示环境，回退纯文本模式（每秒一行）\n");
    }

    long skipped_tail0 = 0, matched = 0, residual_warn = 0;
    uint64_t pending_aps_us = 0;   // 迟到补配：上一 APS 帧未精确命中的尾 ts（µs 域，两型号通用）
    uint64_t last_seq = 0;         // 已配对过的 APS 帧序号
    int idx = -1;                  // 最近一轮最终配对结果（供两次 APS 帧之间持续渲染）
    uint64_t aps_us = 0, residual_us = 0, hit_ts = 0;
    cv::Mat hit_polarity;
    std::vector<uint8_t> aps_bytes;
    int aps_w = 0, aps_h = 0;
    PixelFormat aps_fmt = PixelFormat::NV12;
    auto lastPrint = std::chrono::steady_clock::now();

    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(15));   // 显示节流 ~60fps（环由回调满速喂）

        // 事件几何未就绪（首包事件前）无法配对/渲染，整轮跳过（防 0×0 渲染崩溃）
        {
            std::lock_guard<std::mutex> lk(shared.mu);
            if (shared.evs_w == 0) continue;
        }

        // 最新 APS 帧快照（fresh = 自上轮配对后又到了新 APS 帧）
        uint64_t aps_ts_raw = 0;
        bool fresh = false;
        {
            std::lock_guard<std::mutex> lk(latest.mu);
            if (latest.seq != last_seq && latest.width > 0) {
                fresh = true;
                last_seq = latest.seq;
                aps_bytes = latest.nv12;
                aps_w = latest.width;
                aps_h = latest.height;
                aps_ts_raw = latest.ts_raw;
                aps_fmt = latest.fmt;
            }
        }

        if (fresh) {
            // 加锁快照环形缓存做配对（拷贝命中帧极性图，避免持锁渲染）。
            std::lock_guard<std::mutex> lk(shared.mu);
            idx = -1;
            aps_us = 0;
            residual_us = 0;
            hit_ts = 0;
            hit_polarity.release();

            // 迟到补配（教学点⑤：配对应等待被引用帧到达——尾时间戳指向组帧时刻的最新子帧，
            // APS 常先到一拍（~1 子帧间隔），下一轮补配即精确命中）
            if (pending_aps_us != 0) {
                const int p_idx = nearestFrame(shared.ring, pending_aps_us);
                if (p_idx >= 0) {
                    const uint64_t p_ts = shared.ring[size_t(p_idx)].ts;
                    const uint64_t p_res = (p_ts > pending_aps_us) ? p_ts - pending_aps_us : pending_aps_us - p_ts;
                    if (p_res == 0 || shared.ring.back().ts > pending_aps_us) {   // 精确命中，或引用帧已过（不会再命中）→ 最近邻收尾
                        idx = p_idx;              // 该结果作为本轮显示/计数的配对
                        aps_us = pending_aps_us;
                        residual_us = p_res;
                        pending_aps_us = 0;
                    }
                }
            }
            const bool repaired = (idx >= 0);      // 本轮已有补配终态：本帧配对仅用于维护 pending

            if (aps_ts_raw == 0) {
                ++skipped_tail0;                                    // 侧信道超时/未启动（规格 §8）
            } else {
                // 双型号 ts 均为 ns（003=尾 µs×1000、014=帧头墙钟 sec×1e9+usec×1000）
                // → µs 后与事件 t（003=子帧 ts/200；014=包头 sec×1e6+usec）同轴直接配对。
                const uint64_t us = aps_ts_raw / 1000;
                const int t_idx = nearestFrame(shared.ring, us);
                uint64_t t_res = 0;
                if (t_idx >= 0) t_res = (shared.ring[size_t(t_idx)].ts > us) ? shared.ring[size_t(t_idx)].ts - us : us - shared.ring[size_t(t_idx)].ts;
                if (!repaired) {               // 本轮无补配终态 → 本帧配对即显示/计数
                    idx = t_idx;
                    aps_us = us;
                    residual_us = t_res;
                }
                // pending 维护：最佳命中早于 aps 且未精确（引用子帧可能还在路上）→ 存入待下轮补配；否则清零
                if (t_idx >= 0 && shared.ring[size_t(t_idx)].ts < us && t_res > 0) pending_aps_us = us;
                else pending_aps_us = 0;
            }
            if (idx >= 0) {                        // 最终配对计数（补配成功计 0 残差）
                ++matched;
                hit_ts = shared.ring[size_t(idx)].ts;
                hit_polarity = shared.ring[size_t(idx)].polarity.clone();
            }
            // 帧间隔估计（残差告警阈值 = 3×间隔）
            uint64_t frame_gap_us = 1700;   // 600fps 默认
            if (shared.ring.size() > 10)
                frame_gap_us = (shared.ring.back().ts - shared.ring.front().ts) / (shared.ring.size() - 1);
            if (residual_us > frame_gap_us * 3) ++residual_warn;
        }

        long mono_now = 0;
        {
            std::lock_guard<std::mutex> lk(shared.mu);
            mono_now = shared.mono_violation;
        }

        // 呈现（003=NV12 / 014=Gray8）
        cv::Mat aps_img;
        if (aps_bytes.size() > 0) {
            if (aps_fmt == PixelFormat::Gray8) {
                const cv::Mat gray(aps_h, aps_w, CV_8UC1, aps_bytes.data());
                cv::cvtColor(gray, aps_img, cv::COLOR_GRAY2BGR);
            } else {
                aps_img = nv12ToBgr(aps_bytes.data(), aps_w, aps_h);
            }
        }
        char status[512];
        char evs_note[32];   // 命中帧 ts 原值；未命中（尾/ts 0）显示 --
        if (idx >= 0)
            std::snprintf(evs_note, sizeof(evs_note), "%lluus", (unsigned long long)hit_ts);
        else
            std::snprintf(evs_note, sizeof(evs_note), "--");
        std::snprintf(status, sizeof(status),
            "pair: aps=%lluus evs=%s -> EVS#%d  residual: %llu us  | mono:%s | tail0=%ld warn=%ld matched=%ld | q quit",
            (unsigned long long)aps_us, evs_note, idx, (unsigned long long)residual_us,
            mono_now == 0 ? "OK" : "VIOL", skipped_tail0, residual_warn, matched);
        if (gui) {
            int ew = 0, eh = 0;
            { std::lock_guard<std::mutex> lk(shared.mu); ew = shared.evs_w; eh = shared.evs_h; }
            cv::Mat evs_img = (idx >= 0 && !hit_polarity.empty())
                ? renderColor(hit_polarity) : cv::Mat(eh, ew, CV_8UC3, cv::Scalar(0, 0, 0));
            cv::dilate(evs_img, evs_img, cv::getStructuringElement(cv::MORPH_RECT, {2, 2}));
            cv::Mat canvas;
            const cv::Mat blank = cv::Mat(evs_img.rows, evs_img.cols, CV_8UC3, cv::Scalar(0, 0, 0));
            const cv::Mat& right = aps_img.empty() ? blank : aps_img;
            cv::hconcat(evs_img, right, canvas);
            cv::Mat small;
            cv::resize(canvas, small, cv::Size(canvas.cols / 2, canvas.rows / 2), 0, 0, cv::INTER_NEAREST);
            cv::putText(small, status, {10, small.rows - 12}, cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(200, 200, 200), 1);
            cv::imshow(win, small);
            if ((cv::waitKey(1) & 0xFF) == 'q') break;
        }
        // 终端行：GUI 与文本回退共用 1s 节流（GUI 模式下终端同步滚动配对行）
        if (std::chrono::steady_clock::now() - lastPrint >= std::chrono::seconds(1)) {
            std::printf("%s\n", status);
            lastPrint = std::chrono::steady_clock::now();
        }
    }
    cam.StopStream();
    cam.Destroy();
    if (gui) cv::destroyAllWindows();
    return 0;
}
