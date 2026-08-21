// live_record_display: MIPI-HVS 后端实时预览 + 按键开关录制。
// 本文件只保留参数解析与采集/显示主循环；可视化/录制/解码辅助见 live_widgets.{h,cpp}。
#include "live_widgets.h"

#include <shimetapi/codec/mipi_raw8_codec.h>
#include <shimetapi/hv/camera.h>
#include <shimetapi/hv/device_config.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <sys/select.h>
#include <unistd.h>

using namespace hv_live;   // 引入 live_widgets 的全部符号，主循环代码保持原写法

// ---- 全局运行标志（主循环 + 键盘退出共用）----
std::atomic<bool> g_running(true);

int main(int argc, char** argv) {
    // ---- 解析参数 ----
    bool noDisplay = false;
    std::string evsPrefix = "live_events";
    std::string apsPrefix = "live_video";

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--no-display") == 0) {
            noDisplay = true;
        } else if (std::strcmp(argv[i], "--evs-prefix") == 0 && i + 1 < argc) {
            evsPrefix = argv[++i];
        } else if (std::strcmp(argv[i], "--aps-prefix") == 0 && i + 1 < argc) {
            apsPrefix = argv[++i];
        } else if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
            printUsage(argv[0]);
            return 0;
        }
    }

    std::printf("=== HV Toolkit live_record_display (MIPI-HVS) ===\n");

    // ---- 初始化 Camera ----
    Shimeta::hv::Camera cam;
    Shimeta::hv::DeviceConfig cfg;
    cfg.backend = Shimeta::hv::Backend::MipiHvs;
    cam.Init(cfg);
    if (!cam.StartStream()) {
        std::fprintf(stderr, "无法启动 MIPI-HVS 设备\n");
        return 1;
    }
    std::printf("MIPI-HVS 设备已启动\n");

    // ---- 组件 ----
    Shimeta::codec::MipiRaw8Decoder decoder;
    EvsVisualizer visualizer;
    RecordManager recorder;

    std::atomic<bool> displayEnabled(true);
    cv::Mat evsDisplay, apsDisplay;

    if (!noDisplay) {
        cv::namedWindow(kWindowName, cv::WINDOW_NORMAL);
        std::printf("\n按 'r' 开始/停止录制，按 'd' 开关显示，按 'q/ESC' 退出\n");
    } else {
        displayEnabled = false;
        std::printf("\n显示已禁用（--no-display）。输入 r 回车录制，q 回车退出。\n");
    }

    // ---- 主循环 ----
    auto lastDisplayTime = std::chrono::steady_clock::now();
    const auto displayInterval = std::chrono::milliseconds(1000 / kDefaultDisplayFps);

    // 边沿触发去重：GetFrame 为电平触发（谓词只看 evs.size>0），同一 EVS 包
    // 会被重复返回；按 slab 指针识别新包，重复快照只刷显示、不重复喂
    // 可视化/录制（否则事件被重复累积、RAW 重复落盘）。
    const uint8_t* lastEvsPtr = nullptr;

    while (g_running) {
        // 拉一帧
        Shimeta::Frame frame;
        if (!cam.GetFrame(frame, noDisplay ? 100 : 16)) {
            // 控制台模式：检查 stdin
            if (noDisplay) {
                fd_set fds;
                FD_ZERO(&fds);
                FD_SET(STDIN_FILENO, &fds);
                timeval tv = {0, 100000};
                if (select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv) > 0 &&
                    FD_ISSET(STDIN_FILENO, &fds)) {
                    std::string line;
                    if (std::getline(std::cin, line) && !line.empty()) {
                        char k = line[0];
                        if (k == 'q' || k == 'Q') g_running = false;
                        else if (k == 'r' || k == 'R') {
                            if (recorder.isRecording()) recorder.stop();
                            else recorder.start(evsPrefix, apsPrefix);
                        }
                    }
                }
            }
            continue;
        }

        const bool newEvsPacket = frame.evs.size > 0 && frame.evs.data != lastEvsPtr;
        if (newEvsPacket) lastEvsPtr = frame.evs.data;

        // 事件 → 解码 + 可视化。录制侧将当前 EVS 包首帧 timestamp 写入携带
        // APS 的 AVI tsmp，确保 APS 对应采集到 APS 之后的第一个 EVS 包。
        Shimeta::EvsTimestamp evs_ts;
        if (newEvsPacket) {
            evs_ts = Shimeta::codec::extractEvsTimestamp(frame.evs.data, frame.evs.size);
            std::vector<Shimeta::EventCD> events;
            decoder.Decode(frame.evs.data, frame.evs.size, events);
            if (!events.empty()) {
                visualizer.addEvents(events);
            }
        }

        // 录制（写入 RAW + AVI + tsmp chunk；重复快照跳过，防 RAW 重复包）
        if (newEvsPacket)
            recorder.writeFrame(frame, evs_ts.valid ? &evs_ts : nullptr);

        // APS 图像 → BGR 显示
        if (frame.aps.size > 0 && frame.format == Shimeta::PixelFormat::NV12) {
            apsDisplay = nv12ToBgr(frame.aps.data, frame.aps.size, frame.width, frame.height);
        } else if (frame.aps.size > 0 && frame.format == Shimeta::PixelFormat::Gray8 &&
                   frame.width > 0 && frame.height > 0 &&
                   frame.aps.size >= static_cast<size_t>(frame.width) * frame.height) {
            // X5 VIN 直读旁路：RAW10 已在 HAL 降为 Gray8，包一层 BGR 即可显示
            cv::Mat gray(frame.height, frame.width, CV_8UC1,
                         const_cast<uint8_t*>(frame.aps.data));
            cv::cvtColor(gray, apsDisplay, cv::COLOR_GRAY2BGR);
        }

        // 显示刷新
        auto now = std::chrono::steady_clock::now();
        if (displayEnabled && !noDisplay &&
            now - lastDisplayTime >= displayInterval) {
            lastDisplayTime = now;
            evsDisplay = visualizer.getFrame();

            // 拼接：左侧 EVS 可视化，右侧 APS（缩放到一半宽度）
            cv::Mat evsPanel = evsDisplay.empty()
                ? cv::Mat::zeros(kDefaultEvsHeight, kDefaultEvsWidth, CV_8UC3)
                : evsDisplay;
            cv::Mat apsPanel;
            if (!apsDisplay.empty()) {
                cv::resize(apsDisplay, apsPanel,
                           cv::Size(apsDisplay.cols / 2, apsDisplay.rows / 2));
            } else {
                apsPanel = cv::Mat::zeros(kDefaultApsHeight / 2, kDefaultApsWidth / 2, CV_8UC3);
            }

            int cw = evsPanel.cols + apsPanel.cols;
            int ch = std::max(evsPanel.rows, apsPanel.rows);
            cv::Mat combined(ch, cw, CV_8UC3, cv::Scalar(0, 0, 0));
            evsPanel.copyTo(combined(cv::Rect(0, 0, evsPanel.cols, evsPanel.rows)));
            apsPanel.copyTo(combined(cv::Rect(evsPanel.cols, 0, apsPanel.cols, apsPanel.rows)));

            // 录制状态指示
            std::string title = kWindowName;
            if (recorder.isRecording()) title += " [REC]";
            cv::setWindowTitle(kWindowName, title);
            cv::imshow(kWindowName, combined);

            int key = cv::waitKey(1) & 0xFF;
            if (key == 27 || key == 'q' || key == 'Q') {
                g_running = false;
            } else if (key == 'd' || key == 'D') {
                displayEnabled = !displayEnabled;
                std::printf("显示 %s\n", displayEnabled ? "开启" : "关闭");
            } else if (key == 'r' || key == 'R') {
                if (recorder.isRecording())
                    recorder.stop();
                else
                    recorder.start(evsPrefix, apsPrefix);
            }
        }
    }

    // ---- 清理 ----
    recorder.stop();
    cam.StopStream();
    cam.Destroy();
    if (!noDisplay) cv::destroyAllWindows();
    std::printf("\n=== 程序结束 ===\n");
    return 0;
}
