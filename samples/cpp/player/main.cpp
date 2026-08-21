// player: 离线回放 live_record_display 录制的 .raw + .avi 文件。
// 本文件只保留信号处理与播放编排；数据/回放/UI 辅助代码见 player_widgets.{h,cpp}。
#include "player_widgets.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <signal.h>
#include <thread>
#include <unistd.h>

#include <opencv2/opencv.hpp>

// execinfo.h / backtrace 在某些交叉工具链中不可用
#if __has_include(<execinfo.h>)
#include <execinfo.h>
#else
// 模拟 backtrace 以避免链接错误
static int backtrace(void**, int) { return 0; }
static void backtrace_symbols_fd(void*, int, int) {}
#endif

using namespace hv_player;   // 引入 player_widgets 的全部符号，编排代码保持原写法

// ---- 全局运行标志（信号处理 + 主循环共用；UI 状态在 hv_player 命名空间内）----
std::atomic<bool> g_running(true);

// ---- 信号处理 ----
static void signalHandler(int) { g_running = false; }

static void crashSignalHandler(int signum, siginfo_t*, void*) {
    constexpr char header[] = "\nFatal signal received, backtrace:\n";
    if (write(STDERR_FILENO, header, sizeof(header) - 1) > 0) {}
    void* frames[64];
    int count = backtrace(frames, 64);
    backtrace_symbols_fd(frames, count, STDERR_FILENO);
    signal(signum, SIG_DFL);
    raise(signum);
}

static void installSignalHandlers() {
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    void* frames[1]; backtrace(frames, 1);
    struct sigaction action;
    std::memset(&action, 0, sizeof(action));
    action.sa_sigaction = crashSignalHandler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_SIGINFO | SA_RESETHAND;
    sigaction(SIGSEGV, &action, nullptr);
    sigaction(SIGABRT, &action, nullptr);
    sigaction(SIGBUS, &action, nullptr);
    sigaction(SIGFPE, &action, nullptr);
    sigaction(SIGILL, &action, nullptr);
}

int main(int argc, char** argv) {
    installSignalHandlers();
    if (argc < 3) { printUsage(argv[0]); return 1; }

    std::string raw_path = argv[1];
    std::string avi_path = argv[2];
    bool dump_timestamps = false;
    double fallback_fps = 30.0, speed = 1.0;
    int numeric_arg = 0;
    for (int i = 3; i < argc; ++i) {
        if (std::strcmp(argv[i], "--dump-timestamps") == 0) {
            dump_timestamps = true;
        } else if (numeric_arg++ == 0) {
            fallback_fps = std::atof(argv[i]);
        } else if (numeric_arg == 2) {
            speed = std::atof(argv[i]);
        } else {
            printUsage(argv[0]);
            return 1;
        }
    }
    if (fallback_fps <= 0.0 || speed <= 0.0) { printUsage(argv[0]); return 1; }
    if (dump_timestamps) return dumpTimestamps(raw_path, avi_path, std::cout) ? 0 : 1;

    // ---- 加载 EVS ----
    EvsFrameSequence evs_seq;
    if (!evs_seq.open(raw_path)) return 1;

    // ---- 加载 APS ----
    ApsFrameCache video_cache;
    if (!video_cache.open(avi_path, fallback_fps)) {
        std::cerr << "Failed to open AVI: " << avi_path << std::endl;
        return 1;
    }
    TimestampSyncMap ts_sync;
    bool has_ts_sync = ts_sync.open(raw_path, avi_path);

    auto img_size = evs_seq.imageSize();
    std::cout << "EVS raw: " << raw_path << " (" << img_size.first << "x" << img_size.second << ")\n";
    std::cout << "APS AVI: " << avi_path << ", fps=" << video_cache.fps() << "\n";
    std::cout << "Speed: " << speed << "x\n";
    std::cout << "Keys: q/ESC quit, Space pause, a/d step, click buttons\n";

    // ---- GUI 窗口 ----
    cv::Mat last_video_frame;
    const std::string win_name = "HV Player (toolkit)";
    try {
        cv::namedWindow(win_name, cv::WINDOW_AUTOSIZE);
        cv::setMouseCallback(win_name, mouseCallback);
    } catch (const cv::Exception& e) { printGuiStartupError(e); return 1; }

    // ---- 播放状态 ----
    auto evs_play_start = std::chrono::steady_clock::now();
    auto aps_play_start = std::chrono::steady_clock::now();
    bool evs_playing = true, aps_playing = true;
    uint64_t evs_frame_index = 0, aps_frame_index = 0;
    EvsStepMode evs_step_mode = EvsStepMode::Aps;
    EvsColorMode evs_color_mode = EvsColorMode::BlueRed;
    bool sync_enabled = false;
    bool sync_initialized = false;

    // ---- 主循环 ----
    while (g_running) {
        UiAction action = UiAction(g_pending_action.exchange(int(UiAction::None)));

        // 处理按钮指令
        if (action != UiAction::None) {
            if (action == UiAction::EvsTogglePlay) {
                if (sync_enabled) { bool np = !(evs_playing || aps_playing); evs_playing = np; aps_playing = np; }
                else evs_playing = !evs_playing;
                if (evs_playing) {
                    if (isEvsAtEnd(evs_frame_index, evs_seq.frameCount(), evs_step_mode)) evs_frame_index = 0;
                    evs_play_start = evsStartForFrame(evs_frame_index, speed);
                }
                if (sync_enabled && aps_playing) {
                    if (isApsAtKnownEnd(aps_frame_index, video_cache)) aps_frame_index = 0;
                    aps_play_start = apsStartForFrame(aps_frame_index, speed);
                }
            } else if (action == UiAction::ApsTogglePlay) {
                if (sync_enabled) { bool np = !(evs_playing || aps_playing); evs_playing = np; aps_playing = np; }
                else aps_playing = !aps_playing;
                if (aps_playing) {
                    if (isApsAtKnownEnd(aps_frame_index, video_cache)) aps_frame_index = 0;
                    aps_play_start = apsStartForFrame(aps_frame_index, speed);
                }
                if (sync_enabled && evs_playing) {
                    if (isEvsAtEnd(evs_frame_index, evs_seq.frameCount(), evs_step_mode)) evs_frame_index = 0;
                    evs_play_start = evsStartForFrame(evs_frame_index, speed);
                }
            } else if (action == UiAction::EvsStepSingle) {
                evs_step_mode = EvsStepMode::Single;
            } else if (action == UiAction::EvsStepAps) {
                evs_step_mode = EvsStepMode::Aps;
                evs_frame_index = alignEvsFrameToApsBoundary(evs_frame_index);
                if (evs_playing) evs_play_start = evsStartForFrame(evs_frame_index, speed);
            } else if (action == UiAction::EvsPrev && evs_frame_index > 0) {
                evs_playing = false;
                uint64_t step = evs_step_mode == EvsStepMode::Aps ? kEvsPerApsFrame : 1;
                evs_frame_index = clampEvsFrameIndex(evs_frame_index > step ? evs_frame_index - step : 0, evs_seq.frameCount(), evs_step_mode);
            } else if (action == UiAction::EvsNext) {
                evs_playing = false;
                uint64_t step = evs_step_mode == EvsStepMode::Aps ? kEvsPerApsFrame : 1;
                if (evs_frame_index + step < evs_seq.frameCount())
                    evs_frame_index = clampEvsFrameIndex(evs_frame_index + step, evs_seq.frameCount(), evs_step_mode);
            } else if (action == UiAction::ApsPrev && aps_frame_index > 0) { aps_playing = false; --aps_frame_index; }
            else if (action == UiAction::ApsNext) { aps_playing = false; ++aps_frame_index; }
            else if (action == UiAction::EvsColorBlueRed) evs_color_mode = EvsColorMode::BlueRed;
            else if (action == UiAction::EvsColorOrangeYellow) evs_color_mode = EvsColorMode::OrangeYellow;
            else if (action == UiAction::SyncOn) {
                sync_enabled = true;
                bool np = evs_playing || aps_playing;
                evs_playing = np; aps_playing = np;
            } else if (action == UiAction::SyncOff) sync_enabled = false;
            else {
                double sp = speedForAction(action);
                if (sp > 0.0) { speed = sp; evs_play_start = evsStartForFrame(evs_frame_index, speed); aps_play_start = apsStartForFrame(aps_frame_index, speed); }
            }
        }

        // 播放进度计算
        if (evs_playing && !sync_enabled) {
            auto wall_us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - evs_play_start).count();
            auto target_us = int64_t(double(wall_us) * speed);
            if (evs_step_mode == EvsStepMode::Aps)
                evs_frame_index = alignEvsFrameToApsBoundary(uint64_t(double(target_us) * kEvsFps / 1000000.0));
            else
                evs_frame_index = uint64_t(double(target_us) * kEvsFps / 1000000.0);
            if (evs_frame_index >= evs_seq.frameCount()) {
                evs_frame_index = clampEvsFrameIndex(evs_seq.frameCount() - 1, evs_seq.frameCount(), evs_step_mode);
                evs_playing = false;
            }
        }
        if (aps_playing) {
            auto wall_us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - aps_play_start).count();
            aps_frame_index = uint64_t(double(double(wall_us) * speed) * kApsFps / 1000000.0);
        }

        // 读取 APS 帧
        cv::Mat video_frame;
        uint64_t actual_aps_fi = aps_frame_index;
        if (video_cache.frameAt(aps_frame_index, video_frame, &actual_aps_fi)) {
            if (actual_aps_fi != aps_frame_index) { aps_frame_index = actual_aps_fi; aps_playing = false; if (sync_enabled) evs_playing = false; }
            last_video_frame = video_frame;
        }

        // 时间戳源
        uint64_t aps_timestamp = 0;
        std::string aps_ts_src;
        Shimeta::EvsTimestamp aps_ft = video_cache.timestampAt(aps_frame_index);
        if (aps_ft.valid && (aps_ft.processed_timestamp != 0 || aps_ft.raw_timestamp != 0)) {
            aps_timestamp = aps_ft.processed_timestamp != 0 ? aps_ft.processed_timestamp : aps_ft.raw_timestamp / 200;
            aps_ts_src = "avi metadata /200";
        } else if (has_ts_sync) {
            aps_timestamp = ts_sync.apsVpfTvUsForVideoIndex(aps_frame_index);
            aps_ts_src = "timestamps.csv";
        } else {
            aps_timestamp = apsPlaybackTimestampUs(aps_frame_index, video_cache.fps());
            aps_ts_src = "frame/fps";
        }
        // New recordings embed the paired EVS timestamp in each AVI frame.
        // Enable sync automatically for those files; files without tsmp retain
        // the previous independent-playback behavior.
        if (!sync_initialized) {
            sync_enabled = aps_ft.valid &&
                           (aps_ft.processed_timestamp != 0 || aps_ft.raw_timestamp != 0);
            sync_initialized = true;
        }
        if (sync_enabled && aps_timestamp != 0)
            // APS tsmp stores one exact EVS sensor timestamp. Keep the matched
            // EVS frame for timestamp reporting; accumulated rendering below
            // still spans one APS interval.
            evs_frame_index = evs_seq.frameIndexForTimestamp(aps_timestamp, EvsStepMode::Single);

        // 合成画面
        cv::Mat evs_render = evs_step_mode == EvsStepMode::Aps
            ? evs_seq.accumulatedFrameAt(evs_frame_index, kEvsPerApsFrame, evs_color_mode)
            : evs_seq.frameAt(evs_frame_index, evs_color_mode);
        uint64_t evs_ts = evs_seq.processedTimestampAt(evs_frame_index);

        cv::imshow(win_name, composeSideBySide(evs_render, last_video_frame,
            aps_timestamp, aps_ts_src, aps_frame_index, evs_ts, evs_frame_index,
            video_cache.frameCount(), evs_seq.frameCount(),
            evs_step_mode, evs_color_mode, sync_enabled, speed,
            sync_enabled ? aps_playing : evs_playing, aps_playing));

        // 键盘
        int key = cv::waitKey(1) & 0xFF;
        if (key == 27 || key == 'q' || key == 'Q') g_running = false;
        else if (key == ' ') {
            bool np = !(evs_playing || aps_playing);
            evs_playing = np; aps_playing = np;
            if (np) {
                if (evs_step_mode == EvsStepMode::Aps) evs_frame_index = alignEvsFrameToApsBoundary(evs_frame_index);
                if (isEvsAtEnd(evs_frame_index, evs_seq.frameCount(), evs_step_mode)) evs_frame_index = 0;
                if (isApsAtKnownEnd(aps_frame_index, video_cache)) aps_frame_index = 0;
                evs_play_start = evsStartForFrame(evs_frame_index, speed);
                aps_play_start = apsStartForFrame(aps_frame_index, speed);
            }
        } else if (key == 'a' || key == 'A') {
            evs_playing = false;
            uint64_t step = evs_step_mode == EvsStepMode::Aps ? kEvsPerApsFrame : 1;
            if (evs_frame_index > 0)
                evs_frame_index = clampEvsFrameIndex(evs_frame_index > step ? evs_frame_index - step : 0, evs_seq.frameCount(), evs_step_mode);
        } else if (key == 'd' || key == 'D') {
            evs_playing = false;
            uint64_t step = evs_step_mode == EvsStepMode::Aps ? kEvsPerApsFrame : 1;
            if (evs_frame_index + step < evs_seq.frameCount())
                evs_frame_index = clampEvsFrameIndex(evs_frame_index + step, evs_seq.frameCount(), evs_step_mode);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    cv::destroyAllWindows();
    return 0;
}
