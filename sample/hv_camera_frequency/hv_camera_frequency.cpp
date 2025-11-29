#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <deque>
#include <signal.h>
#include <iomanip>
#include <sstream>
#include <cmath>
#include <limits>
#include <algorithm>
#include <opencv2/opencv.hpp>
#include "hv_camera.h"

// 全局控制标志
std::atomic<bool> g_running(true);
std::atomic<bool> g_display_enabled(true);

// 显示数据结构
struct EVSDisplayData {
    cv::Mat evs_frame;
    std::chrono::steady_clock::time_point timestamp;
    
    EVSDisplayData() : timestamp(std::chrono::steady_clock::now()) {}
};


// EVS显示队列类
template<typename T>
class DisplayQueue {
public:
    DisplayQueue() = default;
    
    void push(const T& data) {
        std::lock_guard<std::mutex> lock(mutex_);
        // 容量为1，新数据直接替换旧数据
        if (!queue_.empty()) {
            queue_.pop();
        }
        queue_.push(data);
        cv_.notify_one();
    }
    
    bool pop(T& data, std::chrono::milliseconds timeout = std::chrono::milliseconds(100)) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (cv_.wait_for(lock, timeout, [this] { return !queue_.empty() || !g_running; })) {
            if (!queue_.empty()) {
                data = queue_.front();
                queue_.pop();
                return true;
            }
        }
        return false;
    }
    
private:
    std::queue<T> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
};

using EVSDisplayQueue = DisplayQueue<EVSDisplayData>;

class EventBatchQueue {
public:
    EventBatchQueue(size_t max_size = 256) : max_size_(max_size) {}
    void push(const std::vector<Metavision::EventCD>& batch) {
        std::lock_guard<std::mutex> lock(mtx_);
        if (q_.size() >= max_size_) {
            q_.pop_front();
        }
        q_.push_back(batch);
        cv_.notify_one();
    }
    bool pop(std::vector<Metavision::EventCD>& out, std::chrono::milliseconds timeout = std::chrono::milliseconds(10)) {
        std::unique_lock<std::mutex> lock(mtx_);
        if (!cv_.wait_for(lock, timeout, [&]{ return !q_.empty() || !g_running; })) return false;
        if (q_.empty()) return false;
        out = std::move(q_.front());
        q_.pop_front();
        return true;
    }
private:
    std::deque<std::vector<Metavision::EventCD>> q_;
    std::mutex mtx_;
    std::condition_variable cv_;
    size_t max_size_;
};

class FrequencyAnalyzer {
public:
    FrequencyAnalyzer(uint64_t window_us = 2000000, uint64_t bin_us = 2000, double f_min = 10.0, double f_max = 200.0, double alpha = 0.2)
        : bin_us_(bin_us), f_min_(f_min), f_max_(f_max), smooth_alpha_(alpha), initialized_(false), last_bin_index_(0), last_estimate_hz_(std::numeric_limits<double>::quiet_NaN()), total_count_(0), roi_active_(false), roi_x0_(0), roi_y0_(0), roi_x1_(0), roi_y1_(0) {
        N_ = window_us / bin_us_;
        if (N_ < 32) N_ = 32;
        bins_.assign(N_, 0.0);
    }
    void push(const std::vector<Metavision::EventCD>& events) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (events.empty()) return;
        for (const auto& ev : events) {
            uint64_t k = static_cast<uint64_t>(ev.t) / bin_us_;
            if (!initialized_) {
                initialized_ = true;
                last_bin_index_ = k;
            }
            if (k > last_bin_index_) {
                uint64_t delta = k - last_bin_index_;
                if (delta >= N_) {
                    std::fill(bins_.begin(), bins_.end(), 0.0);
                    total_count_ = 0;
                } else {
                    for (uint64_t i = 1; i <= delta; ++i) {
                        size_t idx_clear = (last_bin_index_ + i) % N_;
                        total_count_ -= static_cast<uint64_t>(bins_[idx_clear]);
                        bins_[idx_clear] = 0.0;
                    }
                }
                last_bin_index_ = k;
            }
            if (ev.p > 0) {
                if (roi_active_) {
                    int ex = static_cast<int>(ev.x);
                    int ey = static_cast<int>(ev.y);
                    int x0 = std::min(roi_x0_, roi_x1_);
                    int x1 = std::max(roi_x0_, roi_x1_);
                    int y0 = std::min(roi_y0_, roi_y1_);
                    int y1 = std::max(roi_y0_, roi_y1_);
                    if (ex < x0 || ex > x1 || ey < y0 || ey > y1) continue;
                }
                size_t idx = k % N_;
                bins_[idx] += 1.0;
                total_count_ += 1;
            }
        }
    }
    double estimateHz() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_ || total_count_ < 500) return last_estimate_hz_;
        std::vector<double> x(N_);
        for (size_t i = 0; i < N_; ++i) {
            size_t idx = (last_bin_index_ + 1 + i) % N_;
            x[i] = bins_[idx];
        }
        double mean = 0.0;
        for (double v : x) mean += v;
        mean /= static_cast<double>(N_);
        for (double &v : x) v -= mean;
        for (size_t i = 0; i < N_; ++i) x[i] *= 0.5 - 0.5 * std::cos(2.0 * M_PI * i / static_cast<double>(N_ - 1));
        double fs = 1e6 / static_cast<double>(bin_us_);
        int k_min = std::max(1, static_cast<int>(std::floor(f_min_ * N_ / fs)));
        int k_max = std::min(static_cast<int>(N_ / 2) - 1, static_cast<int>(std::ceil(f_max_ * N_ / fs)));
        if (k_max <= k_min) return last_estimate_hz_;
        double best_energy = -1.0;
        int best_k = -1;
        for (int k = k_min; k <= k_max; ++k) {
            double w = 2.0 * M_PI * k / static_cast<double>(N_);
            double coeff = 2.0 * std::cos(w);
            double s_prev = 0.0, s_prev2 = 0.0;
            for (size_t n = 0; n < N_; ++n) {
                double s = x[n] + coeff * s_prev - s_prev2;
                s_prev2 = s_prev;
                s_prev = s;
            }
            double power = s_prev2 * s_prev2 + s_prev * s_prev - coeff * s_prev * s_prev2;
            if (power > best_energy) {
                best_energy = power;
                best_k = k;
            }
        }
        if (best_k < 0) return last_estimate_hz_;
        double f_new = static_cast<double>(best_k) * fs / static_cast<double>(N_);
        if (std::isnan(last_estimate_hz_)) last_estimate_hz_ = f_new;
        else last_estimate_hz_ = (1.0 - smooth_alpha_) * last_estimate_hz_ + smooth_alpha_ * f_new;
        return last_estimate_hz_;
    }
    void setROI(int x0, int y0, int x1, int y1) {
        std::lock_guard<std::mutex> lock(mutex_);
        roi_active_ = true;
        roi_x0_ = std::clamp(x0, 0, HV_EVS_WIDTH - 1);
        roi_y0_ = std::clamp(y0, 0, HV_EVS_HEIGHT - 1);
        roi_x1_ = std::clamp(x1, 0, HV_EVS_WIDTH - 1);
        roi_y1_ = std::clamp(y1, 0, HV_EVS_HEIGHT - 1);
        std::fill(bins_.begin(), bins_.end(), 0.0);
        total_count_ = 0;
    }
    void clearROI() {
        std::lock_guard<std::mutex> lock(mutex_);
        roi_active_ = false;
        std::fill(bins_.begin(), bins_.end(), 0.0);
        total_count_ = 0;
    }
    bool getROI(cv::Rect& rect) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!roi_active_) return false;
        int x0 = std::min(roi_x0_, roi_x1_);
        int x1 = std::max(roi_x0_, roi_x1_);
        int y0 = std::min(roi_y0_, roi_y1_);
        int y1 = std::max(roi_y0_, roi_y1_);
        rect = cv::Rect(x0, y0, std::max(0, x1 - x0 + 1), std::max(0, y1 - y0 + 1));
        return true;
    }
private:
    std::vector<double> bins_;
    uint64_t bin_us_;
    size_t N_;
    double f_min_;
    double f_max_;
    double smooth_alpha_;
    bool initialized_;
    uint64_t last_bin_index_;
    double last_estimate_hz_;
    uint64_t total_count_;
    bool roi_active_;
    int roi_x0_;
    int roi_y0_;
    int roi_x1_;
    int roi_y1_;
    std::mutex mutex_;
};

// 信号处理函数
void signalHandler(int signum) {
    std::cout << "\n接收到停止信号，正在退出..." << std::endl;
    g_running = false;
}


// EVS帧生成器（简化版）
class EVSFrameGenerator {
public:
    EVSFrameGenerator(int width = HV_EVS_WIDTH, int height = HV_EVS_HEIGHT) 
        : width_(width), height_(height) {
        frame_ = cv::Mat::zeros(height, width, CV_8UC3);
    }
    
    void addEvents(const std::vector<Metavision::EventCD>& events) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        for (const auto& event : events) {
            int x = static_cast<int>(event.x);
            int y = static_cast<int>(event.y);
            if (x >= 0 && x < width_ && y >= 0 && y < height_) {
                if (event.p > 0) {
                    frame_.at<cv::Vec3b>(y, x) = cv::Vec3b(255, 255, 255);
                } else {
                    frame_.at<cv::Vec3b>(y, x) = cv::Vec3b(0, 0, 255);
                }
            }
        }
    }
    
    cv::Mat getFrame() {
        std::lock_guard<std::mutex> lock(mutex_);
        cv::Mat result = frame_.clone();
        
        // 逐渐衰减显示
        frame_ *= 0.95;
        
        return result;
    }
    
private:
    cv::Mat frame_;
    int width_, height_;
    std::mutex mutex_;
};

// 显示管理器
class DisplayManager {
public:
    DisplayManager(EVSDisplayQueue& evs_queue, FrequencyAnalyzer& analyzer, FrequencyAnalyzer& roi_analyzer, EventBatchQueue& batch_queue) 
        : evs_display_queue_(evs_queue), 
          freq_analyzer_(analyzer),
          roi_analyzer_(roi_analyzer),
          batch_queue_(batch_queue),
          evs_generator_(HV_EVS_WIDTH, HV_EVS_HEIGHT),
          last_evs_push_(std::chrono::steady_clock::now()),
          display_fps_(30) {}
    
    void setDisplayFPS(int fps) { display_fps_ = fps; }
    void addEvents(const std::vector<Metavision::EventCD>& events) {
        if (g_display_enabled) {
            evs_generator_.addEvents(events);
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_evs_push_).count() >= (1000 / display_fps_)) {
                std::lock_guard<std::mutex> lock(evs_data_mutex_);
                EVSDisplayData evs_data;
                evs_data.evs_frame = evs_generator_.getFrame();
                evs_data.timestamp = now;
                evs_display_queue_.push(evs_data);
                last_evs_push_ = now;
            }
        }
    }
    
private:
    EVSDisplayQueue& evs_display_queue_;
    FrequencyAnalyzer& freq_analyzer_;
    FrequencyAnalyzer& roi_analyzer_;
    EventBatchQueue& batch_queue_;
    EVSFrameGenerator evs_generator_;
    std::mutex evs_data_mutex_;
    std::chrono::steady_clock::time_point last_evs_push_;
    int display_fps_;
};

// 显示线程函数
struct MouseContext {
    FrequencyAnalyzer* roi_analyzer;
    bool selecting;
    int x0;
    int y0;
    cv::Rect current_rect;
};

static void evsMouseCallback(int event, int x, int y, int flags, void* userdata) {
    MouseContext* ctx = static_cast<MouseContext*>(userdata);
    if (!ctx || !ctx->roi_analyzer) return;
    if (event == cv::EVENT_LBUTTONDOWN) {
        ctx->selecting = true;
        ctx->x0 = x;
        ctx->y0 = y;
        ctx->current_rect = cv::Rect(x, y, 0, 0);
    } else if (event == cv::EVENT_MOUSEMOVE) {
        if (ctx->selecting) {
            int x1 = x;
            int y1 = y;
            int rx0 = std::clamp(std::min(ctx->x0, x1), 0, HV_EVS_WIDTH - 1);
            int ry0 = std::clamp(std::min(ctx->y0, y1), 0, HV_EVS_HEIGHT - 1);
            int rx1 = std::clamp(std::max(ctx->x0, x1), 0, HV_EVS_WIDTH - 1);
            int ry1 = std::clamp(std::max(ctx->y0, y1), 0, HV_EVS_HEIGHT - 1);
            ctx->current_rect = cv::Rect(rx0, ry0, std::max(0, rx1 - rx0 + 1), std::max(0, ry1 - ry0 + 1));
        }
    } else if (event == cv::EVENT_LBUTTONUP) {
        ctx->selecting = false;
        int x1 = x;
        int y1 = y;
        ctx->roi_analyzer->setROI(ctx->x0, ctx->y0, x1, y1);
    } else if (event == cv::EVENT_RBUTTONDOWN) {
        ctx->selecting = false;
        ctx->roi_analyzer->clearROI();
        ctx->current_rect = cv::Rect();
    }
}

void displayWorkerThread(EVSDisplayQueue& evs_display_queue, FrequencyAnalyzer& analyzer, FrequencyAnalyzer& roi_analyzer) {
    cv::namedWindow("EVS Events", cv::WINDOW_AUTOSIZE);
    cv::moveWindow("EVS Events", 100, 100);
    MouseContext mc{&roi_analyzer, false, 0, 0, cv::Rect()};
    cv::setMouseCallback("EVS Events", evsMouseCallback, &mc);
    EVSDisplayData evs_data;
    bool evs_updated = false;
    bool has_evs_data = false;
    
    // 显示帧率控制
    const auto frame_duration = std::chrono::milliseconds(33); // 约30FPS
    
    while (g_running) {
        auto loop_start = std::chrono::steady_clock::now();
        
        evs_updated = evs_display_queue.pop(evs_data, std::chrono::milliseconds(4));
        if (evs_updated) has_evs_data = true;
        
        // 处理EVS显示
        if (evs_updated && !evs_data.evs_frame.empty()) {
            cv::Mat evs_display = evs_data.evs_frame.clone();
            
            double f = analyzer.estimateHz();
            double fr = roi_analyzer.estimateHz();
            std::ostringstream oss;
            if (std::isnan(f)) oss << "Freq: --"; else oss << "Freq: " << std::fixed << std::setprecision(1) << f << " Hz";
            std::ostringstream ossr;
            if (std::isnan(fr)) ossr << "ROI: --"; else ossr << "ROI: " << std::fixed << std::setprecision(1) << fr << " Hz";
            cv::putText(evs_display, oss.str(), cv::Point(10, 30), 
                       cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0), 2);
            cv::putText(evs_display, ossr.str(), cv::Point(10, 60), 
                       cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(255, 255, 0), 2);
            cv::Rect rect;
            if (roi_analyzer.getROI(rect) && rect.width > 0 && rect.height > 0) {
                cv::rectangle(evs_display, rect, cv::Scalar(0, 255, 255), 2);
            } else if (mc.selecting && mc.current_rect.width > 0 && mc.current_rect.height > 0) {
                cv::rectangle(evs_display, mc.current_rect, cv::Scalar(0, 255, 255), 2);
            }
            
            cv::imshow("EVS Events", evs_display);
        } else if (!has_evs_data) {
            // 只有在从未收到过EVS数据时才显示无信号提示
            cv::Mat evs_no_signal = cv::Mat::zeros(HV_EVS_HEIGHT, HV_EVS_WIDTH, CV_8UC3);
            cv::putText(evs_no_signal, "EVS No Signal", cv::Point(50, HV_EVS_HEIGHT/2), 
                       cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(0, 0, 255), 2);
            cv::imshow("EVS Events", evs_no_signal);
        }
        
        
        // 处理键盘输入
        int key = cv::waitKey(1) & 0xFF;
        if (key == 27 || key == 'q') { // ESC或q键退出
            g_running = false;
            break;
        } else if (key == 'd') {
            g_display_enabled = !g_display_enabled;
            std::cout << "显示 " << (g_display_enabled ? "开启" : "关闭") << std::endl;
        }
        
        // 帧率控制：确保显示线程以稳定的30FPS运行
        auto loop_end = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(loop_end - loop_start);
        if (elapsed < frame_duration) {
            std::this_thread::sleep_for(frame_duration - elapsed);
        }
    }
    
    cv::destroyAllWindows();
}

void processingWorkerThread(EventBatchQueue& batch_queue, DisplayManager& display_manager, FrequencyAnalyzer& analyzer, FrequencyAnalyzer& roi_analyzer) {
    while (g_running) {
        std::vector<Metavision::EventCD> batch;
        if (!batch_queue.pop(batch, std::chrono::milliseconds(2))) {
            continue;
        }
        if (!batch.empty()) {
            analyzer.push(batch);
            roi_analyzer.push(batch);
            display_manager.addEvents(batch);
        }
    }
}

int main(int argc, char* argv[]) {
    std::cout << "=== HV相机实时EVS显示程序 ===" << std::endl;
    std::cout << "控制说明:" << std::endl;
    std::cout << "  d - 开启/关闭显示" << std::endl;
    std::cout << "  q/ESC - 退出程序" << std::endl;
    std::cout << "  Ctrl+C - 强制退出" << std::endl;
    
    int display_fps = 30;
    
    if (argc >= 2) {
        display_fps = std::atoi(argv[1]);
    }
    
    std::cout << "\n配置信息:" << std::endl;
    std::cout << "显示帧率: " << display_fps << " FPS" << std::endl;
    
    // 注册信号处理函数
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    // 创建组件实例
    const uint16_t VENDOR_ID = 0x1d6b;
    const uint16_t PRODUCT_ID = 0x0105;
    
    hv::HV_Camera camera(VENDOR_ID, PRODUCT_ID);
    EVSDisplayQueue evs_display_queue;
    FrequencyAnalyzer analyzer;
    FrequencyAnalyzer roi_analyzer;
    EventBatchQueue batch_queue(512);
    DisplayManager display_manager(evs_display_queue, analyzer, roi_analyzer, batch_queue);
    
    display_manager.setDisplayFPS(display_fps);
    
    // 打开相机
    std::cout << "\n正在打开相机..." << std::endl;
    if (!camera.open()) {
        std::cerr << "错误: 无法打开HV相机" << std::endl;
        std::cerr << "请确保：" << std::endl;
        std::cerr << "1. 相机已正确连接到USB端口" << std::endl;
        std::cerr << "2. 相机驱动已正确安装" << std::endl;
        std::cerr << "3. 相机没有被其他程序占用" << std::endl;
        return -1;
    }
    
    std::cout << "相机打开成功！" << std::endl;
    
    std::thread display_thread(displayWorkerThread, 
                              std::ref(evs_display_queue),
                              std::ref(analyzer),
                              std::ref(roi_analyzer));
    std::thread processing_thread(processingWorkerThread,
                                  std::ref(batch_queue),
                                  std::ref(display_manager),
                                  std::ref(analyzer),
                                  std::ref(roi_analyzer));
    
    // 绑定事件回调函数
    auto event_callback = [&](const std::vector<Metavision::EventCD>& events) {
        try {
            batch_queue.push(events);
        } catch (const std::exception& e) {
            std::cerr << "事件处理错误: " << e.what() << std::endl;
        }
    };
    
    // 启动事件采集
    std::cout << "正在启动事件采集..." << std::endl;
    if (!camera.startEventCapture(event_callback)) {
        std::cerr << "错误: 无法启动事件采集" << std::endl;
        g_running = false;
        display_thread.join();
        camera.close();
        return -1;
    }
    
    std::cout << "\n系统启动完成！按 'q' 退出" << std::endl;
    
    // 主循环：等待用户操作或程序退出
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // 清理资源
    std::cout << "\n正在停止采集..." << std::endl;
    camera.stopEventCapture();
    
    
    // 等待显示线程结束
    display_thread.join();
    processing_thread.join();
    
    // 关闭相机
    camera.close();
    std::cout << "相机已关闭" << std::endl;
    
    std::cout << "\n=== 程序结束 ===" << std::endl;
    
    return 0;
}
