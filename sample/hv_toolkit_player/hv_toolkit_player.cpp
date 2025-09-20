/**
 * @file hv_toolkit_player.cpp
 * @brief HV事件数据播放器示例 - 基于Metavision SDK
 * 功能：读取二进制raw文件，支持暂停和重新播放，使用frame_gen进行切片显示
 */

#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <signal.h>
#include <opencv2/opencv.hpp>
#include "hv_event_reader.h"
#include <metavision/sdk/core/algorithms/periodic_frame_generation_algorithm.h>
std::atomic<bool> g_running(true);
void signalHandler(int signum) {
    g_running = false;
}

// 播放器类
class EventPlayer {
public:
    EventPlayer() : 
        reader_(std::make_unique<hv::HVEventReader>()),
        is_playing_(false),
        current_time_(0),
        playback_speed_(1.0),
        batch_size_(10000),
        last_frame_time_(0) {
    }
    
    ~EventPlayer() {
        frame_gen_.reset(); // 先销毁回调，避免悬挂this
        if (is_playing_) {
            stop();
        }
        reader_.reset();
    }
    
    bool open(const std::string& filename) {
        if (!reader_->open(filename)) {
            std::cerr << "无法打开事件文件: " << filename << std::endl;
            return false;
        }
        
        // 获取文件信息
        auto size = reader_->getImageSize();
        width_ = size.first;
        height_ = size.second;
        total_events_ = 0; // 无法预先获取事件总数，设为0
        const auto& header = reader_->getHeader();
        start_time_ = header.start_timestamp;
        end_time_ = start_time_; // 初始化为开始时间，实际结束时间需要在读取过程中更新
        duration_us_ = 0; // 初始化为0，实际时长需要在读取过程中计算
        
        std::cout << "文件信息:" << std::endl;
        std::cout << "  分辨率: " << width_ << "x" << height_ << std::endl;
        std::cout << "  事件数量: " << total_events_ << std::endl;
        std::cout << "  时长: " << duration_us_ / 1000000.0 << " 秒" << std::endl;
        
        // 创建帧生成器
        const std::uint32_t accumulation_time_us = 50000; // 50ms
        double fps = 20.0;
        frame_gen_ = std::make_unique<Metavision::PeriodicFrameGenerationAlgorithm>(
            width_, height_, accumulation_time_us, fps);
        
        // 设置帧生成器回调
        frame_gen_->set_output_callback([this](Metavision::timestamp ts, cv::Mat& frame) {
            try {
                std::lock_guard<std::mutex> lock(frame_mutex_);
                if (!frame.empty()) {
                    frame.copyTo(display_frame_);
                    current_time_ = ts + start_time_;
                    frame_ready_ = true;
                }
            } catch (const std::exception& e) {
                std::cerr << "帧生成器回调中发生异常: " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "帧生成器回调中发生未知异常" << std::endl;
            }
        });
        
        return true;
    }
    
    void start() {
        if (is_playing_) return;
        
        is_playing_ = true;
        
        // 启动播放线程
        play_thread_ = std::thread(&EventPlayer::playbackLoop, this);
    }
    
    void stop() {
        if (!is_playing_) return;
        is_playing_ = false;
        if (play_thread_.joinable()) {
            play_thread_.join();
        }
    }
    
    bool isPlaying() const {
        return is_playing_;
    }
    
    bool hasFrame() const {
        std::lock_guard<std::mutex> lock(frame_mutex_);
        return frame_ready_ && !display_frame_.empty();
    }
    
    cv::Mat getFrame() const {
        std::lock_guard<std::mutex> lock(frame_mutex_);
        if (display_frame_.empty()) {
            return cv::Mat();
        }
        return display_frame_.clone();
    }
    

    
private:
    void playbackLoop() {
        std::vector<Metavision::EventCD> events;
        while (is_playing_ && g_running) {
            size_t read_count = reader_->readEvents(batch_size_, events);
            if (read_count == 0) {
                break;
            }
            frame_gen_->process_events(events.begin(), events.end());
            auto current_event_time = events.back().t;
            
            // 动态更新结束时间和时长
            end_time_ = current_event_time + start_time_;
            duration_us_ = end_time_ - start_time_;
            
            if (last_frame_time_ > 0) {
                auto time_diff = current_event_time - last_frame_time_;
                if (time_diff > 0) {
                    auto wait_time_us = static_cast<long>(static_cast<double>(time_diff) / playback_speed_);
                    if (wait_time_us > 100000) {
                        wait_time_us = 100000;
                    }
                    std::this_thread::sleep_for(std::chrono::microseconds(wait_time_us));
                }
            }
            last_frame_time_ = current_event_time;
        }
        is_playing_ = false;
    }
    
private:
    std::unique_ptr<hv::HVEventReader> reader_;
    std::unique_ptr<Metavision::PeriodicFrameGenerationAlgorithm> frame_gen_;
    std::thread play_thread_;
    std::atomic<bool> is_playing_;
    uint32_t width_;
    uint32_t height_;
    uint64_t total_events_;
    Metavision::timestamp start_time_;
    Metavision::timestamp end_time_;
    Metavision::timestamp duration_us_;
    std::atomic<Metavision::timestamp> current_time_;
    Metavision::timestamp last_frame_time_;
    double playback_speed_;
    size_t batch_size_;
    mutable std::mutex frame_mutex_;
    cv::Mat display_frame_;
    bool frame_ready_ = false;
};



int main(int argc, char** argv) {
    // 注册信号处理函数
    signal(SIGINT, signalHandler);
    
    // 解析命令行参数
    if (argc < 2) {
        std::cerr << "用法: " << argv[0] << " <事件文件.raw>" << std::endl;
        return -1;
    }
    
    std::string input_file = argv[1];
    
    // 创建播放器实例
    EventPlayer player;
    
    // 打开事件文件
    if (!player.open(input_file)) {
        return -1;
    }
    
    // 创建OpenCV窗口
    std::string window_name = "HV Event Player - " + input_file;
    cv::namedWindow(window_name, cv::WINDOW_AUTOSIZE);
    
    // 启动播放
    player.start();
    
    // 主循环
    while (g_running) {
        if (player.hasFrame()) {
            cv::Mat event_frame = player.getFrame();
            if (!event_frame.empty()) {
                cv::imshow(window_name, event_frame);
            }
        }
        int key = cv::waitKey(1) & 0xFF;
        if (key == 27 || key == 'q' || key == 'Q') {
            g_running = false;
        }
        if (!player.isPlaying()) {
            cv::Mat last_frame = player.getFrame();
            if (!last_frame.empty()) {
                cv::putText(last_frame, "press the Q key to exit", cv::Point(20, 30),
                          cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(255, 255, 255), 2);
                cv::imshow(window_name, last_frame);
            }
            int key = cv::waitKey(30) & 0xFF;
            if (key == 27 || key == 'q' || key == 'Q') {
                g_running = false;
            }
        }
    }
    
    player.stop();
    cv::destroyAllWindows();
    std::cout << "程序已退出" << std::endl;
    return 0;
}
