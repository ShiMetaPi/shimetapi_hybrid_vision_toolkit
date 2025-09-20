#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <signal.h>
#include <iomanip>
#include <sstream>
#include <opencv2/opencv.hpp>
#include "hv_camera.h"
#include "hv_event_writer.h"

// 全局控制标志
std::atomic<bool> g_running(true);
std::atomic<bool> g_recording(false);
std::atomic<bool> g_display_enabled(true);

// 显示数据结构
struct EVSDisplayData {
    cv::Mat evs_frame;
    std::chrono::steady_clock::time_point timestamp;
    
    EVSDisplayData() : timestamp(std::chrono::steady_clock::now()) {}
};

struct APSDisplayData {
    cv::Mat aps_frame;
    std::chrono::steady_clock::time_point timestamp;
    
    APSDisplayData() : timestamp(std::chrono::steady_clock::now()) {}
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
using APSDisplayQueue = DisplayQueue<APSDisplayData>;

// 生成基于时间戳的文件名
std::string generateTimestampFilename(const std::string& prefix, const std::string& extension) {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::stringstream ss;
    ss << prefix << "_" 
       << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S")
       << "_" << std::setfill('0') << std::setw(3) << ms.count()
       << "." << extension;
    
    return ss.str();
}

// 信号处理函数
void signalHandler(int signum) {
    std::cout << "\n接收到停止信号，正在退出..." << std::endl;
    g_running = false;
    g_recording = false;
}

class EventRecorder {
public:
    EventRecorder() : event_writer_(std::make_unique<hv::HVEventWriter>()),
                     total_events_(0), is_recording_(false) {}
    
    ~EventRecorder() {
        stopRecording();
    }
    
    bool startRecording(const std::string& output_filename) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (is_recording_) {
            return true; // 已在录制中
        }
        
        if (!event_writer_->open(output_filename, HV_EVS_WIDTH, HV_EVS_HEIGHT)) {
            std::cerr << "无法创建事件输出文件: " << output_filename << std::endl;
            return false;
        }
        
        output_filename_ = output_filename;
        is_recording_ = true;
        total_events_ = 0;
        std::cout << "开始录制事件到文件: " << output_filename << std::endl;
        return true;
    }
    
    void stopRecording() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!is_recording_) {
            return;
        }
        
        if (event_writer_ && event_writer_->isOpen()) {
            event_writer_->flush();
            std::cout << "总共录制了 " << total_events_ << " 个事件" << std::endl;
            std::cout << "事件文件大小: " << event_writer_->getFileSize() << " 字节" << std::endl;
            event_writer_->close();
            std::cout << "事件文件已保存: " << output_filename_ << std::endl;
        }
        is_recording_ = false;
    }
    
    bool isRecording() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return is_recording_;
    }
    
    // 事件回调函数
    void onEventReceived(const std::vector<Metavision::EventCD>& events) {
        // 录制控制：只有在g_recording为true且正在录制时才写入文件
        if (g_recording && is_recording_) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (event_writer_ && event_writer_->isOpen()) {
                size_t written = event_writer_->writeEvents(events);
                total_events_ += written;
                
                // 定期刷新缓冲区
                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - last_flush_time_).count() > 1000) {
                    event_writer_->flush();
                    last_flush_time_ = now;
                }
            }
        }
    }
    
    uint64_t getTotalEvents() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return total_events_;
    }
    
private:
    std::unique_ptr<hv::HVEventWriter> event_writer_;
    std::string output_filename_;
    std::atomic<uint64_t> total_events_;
    std::atomic<bool> is_recording_;
    std::chrono::steady_clock::time_point last_flush_time_;
    mutable std::mutex mutex_;
};

class VideoRecorder {
public:
    VideoRecorder() : total_frames_(0), fps_(30.0), is_recording_(false) {}
    
    ~VideoRecorder() {
        stopRecording();
    }
    
    bool startRecording(const std::string& output_filename, double fps = 30.0) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (is_recording_) {
            return true; // 已在录制中
        }
        
        fps_ = fps;
        output_filename_ = output_filename;
        
        int fourcc = cv::VideoWriter::fourcc('M', 'J', 'P', 'G');
        video_writer_.open(output_filename, fourcc, fps, cv::Size(HV_APS_WIDTH, HV_APS_HEIGHT));
        
        if (!video_writer_.isOpened()) {
            std::cerr << "无法创建视频文件: " << output_filename << std::endl;
            return false;
        }
        
        is_recording_ = true;
        total_frames_ = 0;
        std::cout << "开始录制视频到文件: " << output_filename << std::endl;
        return true;
    }
    
    void stopRecording() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!is_recording_) {
            return;
        }
        
        if (video_writer_.isOpened()) {
            video_writer_.release();
            std::cout << "总共录制了 " << total_frames_ << " 帧视频" << std::endl;
            std::cout << "视频文件已保存: " << output_filename_ << std::endl;
        }
        is_recording_ = false;
    }
    
    bool isRecording() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return is_recording_;
    }
    
    // 图像回调函数
    void onImageReceived(const cv::Mat& image) {
        // 录制控制：只有在g_recording为true且正在录制时才写入文件
        if (g_recording && is_recording_) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (video_writer_.isOpened()) {
                video_writer_.write(image);
                total_frames_++;
            }
        }
    }
    
    uint64_t getTotalFrames() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return total_frames_;
    }
    
private:
    cv::VideoWriter video_writer_;
    std::string output_filename_;
    std::atomic<uint64_t> total_frames_;
    std::atomic<bool> is_recording_;
    double fps_;
    mutable std::mutex mutex_;
};

// EVS帧生成器（简化版）
class EVSFrameGenerator {
public:
    EVSFrameGenerator(int width = HV_EVS_WIDTH, int height = HV_EVS_HEIGHT) 
        : width_(width), height_(height) {
        frame_ = cv::Mat::zeros(height, width, CV_8UC3);
    }
    
    void addEvents(const std::vector<Metavision::EventCD>& events) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // 简单的事件可视化：正事件为白色，负事件为红色
        for (const auto& event : events) {
            if (event.x >= 0 && event.x < width_ && event.y >= 0 && event.y < height_) {
                if (event.p > 0) {
                    frame_.at<cv::Vec3b>(event.y, event.x) = cv::Vec3b(255, 255, 255); // 白色
                } else {
                    frame_.at<cv::Vec3b>(event.y, event.x) = cv::Vec3b(0, 0, 255); // 红色
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
    DisplayManager(EVSDisplayQueue& evs_queue, APSDisplayQueue& aps_queue) 
        : evs_display_queue_(evs_queue), 
          aps_display_queue_(aps_queue),
          evs_generator_(HV_EVS_WIDTH, HV_EVS_HEIGHT),
          last_evs_push_(std::chrono::steady_clock::now()),
          last_aps_push_(std::chrono::steady_clock::now()),
          display_fps_(30) {}
    
    void addEvents(const std::vector<Metavision::EventCD>& events) {
        if (g_display_enabled) {
            // 始终处理最新EVS事件
            evs_generator_.addEvents(events);
            
            // 频率控制：只有时间间隔足够时才推入队列，确保推入的是时间上最新的一帧
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(
                now - last_evs_push_).count() >= (1000 / display_fps_)) {
                
                std::lock_guard<std::mutex> lock(evs_data_mutex_);
                EVSDisplayData evs_data;
                evs_data.evs_frame = evs_generator_.getFrame();
                evs_data.timestamp = now;
                evs_display_queue_.push(evs_data);
                last_evs_push_ = now;
            }
        }
    }
    
    void addImage(const cv::Mat& image) {
        if (g_display_enabled) {
            // APS也采用频率控制，确保推入的是时间上最新的图像
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(
                now - last_aps_push_).count() >= (1000 / display_fps_)) {
                
                std::lock_guard<std::mutex> lock(aps_data_mutex_);
                APSDisplayData aps_data;
                aps_data.aps_frame = image.clone();
                aps_data.timestamp = now;
                aps_display_queue_.push(aps_data);
                last_aps_push_ = now;
            }
        }
    }
    

    
    void setDisplayFPS(int fps) {
        display_fps_ = fps;
    }
    
private:
    EVSDisplayQueue& evs_display_queue_;
    APSDisplayQueue& aps_display_queue_;
    EVSFrameGenerator evs_generator_;
    std::mutex evs_data_mutex_;
    std::mutex aps_data_mutex_;
    std::chrono::steady_clock::time_point last_evs_push_;
    std::chrono::steady_clock::time_point last_aps_push_;
    int display_fps_;
};

// 显示线程函数
void displayWorkerThread(EVSDisplayQueue& evs_display_queue,
                        APSDisplayQueue& aps_display_queue,
                        EventRecorder& event_recorder,
                        VideoRecorder& video_recorder,
                        hv::HV_Camera& camera,
                        const std::string& event_file,
                        const std::string& video_file,
                        double fps) {
    // 创建两个独立的窗口
    cv::namedWindow("EVS Events", cv::WINDOW_AUTOSIZE);
    cv::namedWindow("APS Images", cv::WINDOW_AUTOSIZE);
    
    // 设置窗口位置，避免重叠
    cv::moveWindow("EVS Events", 100, 100);
    cv::moveWindow("APS Images", 750, 100);
    
    EVSDisplayData evs_data;
    APSDisplayData aps_data;
    bool evs_updated = false;
    bool aps_updated = false;
    bool has_evs_data = false;
    bool has_aps_data = false;
    
    // 显示帧率控制
    const auto frame_duration = std::chrono::milliseconds(33); // 约30FPS
    auto last_display_time = std::chrono::steady_clock::now();
    
    while (g_running) {
        auto loop_start = std::chrono::steady_clock::now();
        
        // 分别从两个队列获取数据，使用较短的超时时间
        evs_updated = evs_display_queue.pop(evs_data, std::chrono::milliseconds(16));
        aps_updated = aps_display_queue.pop(aps_data, std::chrono::milliseconds(16));
        
        // 记录是否有数据
        if (evs_updated) has_evs_data = true;
        if (aps_updated) has_aps_data = true;
        
        // 处理EVS显示
        if (evs_updated && !evs_data.evs_frame.empty()) {
            cv::Mat evs_display = evs_data.evs_frame.clone();
            
            // 在EVS窗口添加状态信息
            std::string status = g_recording ? "Recording" : "Stopped";
            cv::Scalar color = g_recording ? cv::Scalar(0, 255, 0) : cv::Scalar(0, 0, 255);
            cv::putText(evs_display, "EVS - " + status, cv::Point(10, 30), 
                       cv::FONT_HERSHEY_SIMPLEX, 0.7, color, 2);
            
            cv::imshow("EVS Events", evs_display);
        } else if (!has_evs_data) {
            // 只有在从未收到过EVS数据时才显示无信号提示
            cv::Mat evs_no_signal = cv::Mat::zeros(HV_EVS_HEIGHT, HV_EVS_WIDTH, CV_8UC3);
            cv::putText(evs_no_signal, "EVS No Signal", cv::Point(50, HV_EVS_HEIGHT/2), 
                       cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(0, 0, 255), 2);
            cv::imshow("EVS Events", evs_no_signal);
        }
        
        // 处理APS显示
        if (aps_updated && !aps_data.aps_frame.empty()) {
            cv::Mat aps_display = aps_data.aps_frame.clone();
            
            // 在APS窗口添加状态信息
            std::string status = g_recording ? "Recording" : "Stopped";
            cv::Scalar color = g_recording ? cv::Scalar(0, 255, 0) : cv::Scalar(0, 0, 255);
            cv::putText(aps_display, "APS - " + status, cv::Point(10, 30), 
                       cv::FONT_HERSHEY_SIMPLEX, 0.7, color, 2);
            
            cv::imshow("APS Images", aps_display);
        } else if (!has_aps_data) {
            // 只有在从未收到过APS数据时才显示无信号提示
            cv::Mat aps_no_signal = cv::Mat::zeros(HV_APS_HEIGHT, HV_APS_WIDTH, CV_8UC3);
            cv::putText(aps_no_signal, "APS No Signal", cv::Point(200, HV_APS_HEIGHT/2), 
                       cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(0, 0, 255), 2);
            cv::imshow("APS Images", aps_no_signal);
        }
        
        // 处理键盘输入
        int key = cv::waitKey(1) & 0xFF;
        if (key == 27 || key == 'q') { // ESC或q键退出
            g_running = false;
            break;
        } else if (key == 'r') { // r键切换录制状态
            if (g_recording) {
                std::cout << "\n停止录制..." << std::endl;
                g_recording = false;
                event_recorder.stopRecording();
                video_recorder.stopRecording();
            } else {
                std::cout << "\n开始录制..." << std::endl;
                // 生成基于时间戳的文件名
                std::string timestamped_event_file = generateTimestampFilename("live_events", "raw");
                std::string timestamped_video_file = generateTimestampFilename("live_video", "avi");
                
                std::cout << "事件文件: " << timestamped_event_file << std::endl;
                std::cout << "视频文件: " << timestamped_video_file << std::endl;
                
                // 在开始录制前清空USB队列
                camera.clearEventQueue();
                if (event_recorder.startRecording(timestamped_event_file) && 
                    video_recorder.startRecording(timestamped_video_file, fps)) {
                    g_recording = true;
                } else {
                    std::cerr << "录制启动失败" << std::endl;
                }
            }
        } else if (key == 'd') { // d键切换显示
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

int main(int argc, char* argv[]) {
    std::cout << "=== HV相机实时显示和录制程序 ===" << std::endl;
    std::cout << "控制说明:" << std::endl;
    std::cout << "  r - 开始/停止录制" << std::endl;
    std::cout << "  d - 开启/关闭显示" << std::endl;
    std::cout << "  q/ESC - 退出程序" << std::endl;
    std::cout << "  Ctrl+C - 强制退出" << std::endl;
    
    // 解析命令行参数
    std::string event_output_file = "live_events.raw";
    std::string video_output_file = "live_video.avi";
    double fps = 30.0;
    int display_fps = 30;
    
    if (argc >= 2) {
        event_output_file = argv[1];
    }
    if (argc >= 3) {
        video_output_file = argv[2];
    }
    if (argc >= 4) {
        fps = std::atof(argv[3]);
    }
    if (argc >= 5) {
        display_fps = std::atoi(argv[4]);
    }
    
    std::cout << "\n配置信息:" << std::endl;
    std::cout << "事件输出文件前缀: live_events (实际文件名将基于录制时间戳生成)" << std::endl;
    std::cout << "视频输出文件前缀: live_video (实际文件名将基于录制时间戳生成)" << std::endl;
    std::cout << "录制帧率: " << fps << " FPS" << std::endl;
    std::cout << "显示帧率: " << display_fps << " FPS" << std::endl;
    std::cout << "注意: 按 'r' 键开始录制时，将自动生成带时间戳的文件名以防止覆盖" << std::endl;
    
    // 注册信号处理函数
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    // 创建组件实例
    const uint16_t VENDOR_ID = 0x1d6b;
    const uint16_t PRODUCT_ID = 0x0105;
    
    hv::HV_Camera camera(VENDOR_ID, PRODUCT_ID);
    EventRecorder event_recorder;
    VideoRecorder video_recorder;
    EVSDisplayQueue evs_display_queue;
    APSDisplayQueue aps_display_queue;
    DisplayManager display_manager(evs_display_queue, aps_display_queue);
    
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
    
    // 启动显示线程
    std::thread display_thread(displayWorkerThread, 
                              std::ref(evs_display_queue),
                              std::ref(aps_display_queue),
                              std::ref(event_recorder),
                              std::ref(video_recorder),
                              std::ref(camera),
                              event_output_file,
                              video_output_file,
                              fps);
    
    // 绑定事件回调函数
    auto event_callback = [&](const std::vector<Metavision::EventCD>& events) {
        try {
            // 录制处理
            event_recorder.onEventReceived(events);
            // 显示处理
            display_manager.addEvents(events);
        } catch (const std::exception& e) {
            std::cerr << "事件处理错误: " << e.what() << std::endl;
        }
    };
    
    // 绑定图像回调函数
    auto image_callback = [&](const cv::Mat& image) {
        try {
            // 录制处理
            video_recorder.onImageReceived(image);
            // 显示处理
            display_manager.addImage(image);
        } catch (const std::exception& e) {
            std::cerr << "图像处理错误: " << e.what() << std::endl;
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
    
    // 启动图像采集
    std::cout << "正在启动图像采集..." << std::endl;
    if (!camera.startImageCapture(image_callback)) {
        std::cerr << "错误: 无法启动图像采集" << std::endl;
        camera.stopEventCapture();
        g_running = false;
        display_thread.join();
        camera.close();
        return -1;
    }
    
    std::cout << "\n系统启动完成！按 'r' 开始录制，按 'q' 退出" << std::endl;
    
    // 主循环：等待用户操作或程序退出
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // 清理资源
    std::cout << "\n正在停止采集..." << std::endl;
    camera.stopEventCapture();
    camera.stopImageCapture();
    
    // 停止录制
    if (g_recording) {
        event_recorder.stopRecording();
        video_recorder.stopRecording();
    }
    
    // 等待显示线程结束
    display_thread.join();
    
    // 关闭相机
    camera.close();
    std::cout << "相机已关闭" << std::endl;
    
    std::cout << "\n=== 程序结束 ===" << std::endl;
    std::cout << "总事件数: " << event_recorder.getTotalEvents() << std::endl;
    std::cout << "总视频帧数: " << video_recorder.getTotalFrames() << std::endl;
    
    return 0;
}
