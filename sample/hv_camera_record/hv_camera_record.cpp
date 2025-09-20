#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <atomic>
#include <signal.h>
#include <opencv2/opencv.hpp>
#include "hv_camera.h"
#include "hv_event_writer.h"

// 全局标志位用于控制录制停止
std::atomic<bool> g_recording(false);

// 信号处理函数，用于优雅地停止录制
void signalHandler(int signum) {
    std::cout << "\n接收到停止信号，正在停止录制..." << std::endl;
    g_recording = false;
}

class EventRecorder {
public:
    EventRecorder() : event_writer_(std::make_unique<hv::HVEventWriter>()),
                     total_events_(0) {}
    
    ~EventRecorder() {
        stopRecording();
    }
    
    bool startRecording(const std::string& output_filename) {
        // 创建并打开writer，使用标准EV相机分辨率
        if (!event_writer_->open(output_filename, HV_EVS_WIDTH, HV_EVS_HEIGHT)) {
            std::cerr << "无法创建输出文件: " << output_filename << std::endl;
            return false;
        }
        
        output_filename_ = output_filename;
        std::cout << "开始录制事件到文件: " << output_filename << std::endl;
        return true;
    }
    
    void stopRecording() {
        if (event_writer_ && event_writer_->isOpen()) {
            event_writer_->flush();
            std::cout << "总共录制了 " << total_events_ << " 个事件" << std::endl;
            std::cout << "事件文件大小: " << event_writer_->getFileSize() << " 字节" << std::endl;
            event_writer_->close();
            std::cout << "事件文件已保存: " << output_filename_ << std::endl;
        }
    }
    
    // 事件回调函数
    void onEventReceived(const std::vector<Metavision::EventCD>& events) {
        if (!g_recording || !event_writer_->isOpen()) {
            return;
        }
        
        // 批量写入事件
        size_t written = event_writer_->writeEvents(events);
        total_events_ += written;
        
        // 定期刷新缓冲区，避免内存积累
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_flush_time_).count() > 1000) {  // 每秒刷新一次
            event_writer_->flush();
            last_flush_time_ = now;
        }
    }
    
    uint64_t getTotalEvents() const {
        return total_events_;
    }
    
private:
    std::unique_ptr<hv::HVEventWriter> event_writer_;
    std::string output_filename_;
    std::atomic<uint64_t> total_events_;
    std::chrono::steady_clock::time_point last_flush_time_;
};

class VideoRecorder {
public:
    VideoRecorder() : total_frames_(0), fps_(30.0) {}
    
    ~VideoRecorder() {
        stopRecording();
    }
    
    bool startRecording(const std::string& output_filename, double fps = 30.0) {
        fps_ = fps;
        output_filename_ = output_filename;
        
        // 初始化VideoWriter，使用MJPG编码器
        int fourcc = cv::VideoWriter::fourcc('M', 'J', 'P', 'G');
        video_writer_.open(output_filename, fourcc, fps, cv::Size(HV_APS_WIDTH, HV_APS_HEIGHT));
        
        if (!video_writer_.isOpened()) {
            std::cerr << "无法创建视频文件: " << output_filename << std::endl;
            return false;
        }
        
        std::cout << "开始录制视频到文件: " << output_filename << std::endl;
        return true;
    }
    
    void stopRecording() {
        if (video_writer_.isOpened()) {
            video_writer_.release();
            std::cout << "总共录制了 " << total_frames_ << " 帧视频" << std::endl;
            std::cout << "视频文件已保存: " << output_filename_ << std::endl;
        }
    }
    
    // 图像回调函数
    void onImageReceived(const cv::Mat& image) {
        if (!g_recording || !video_writer_.isOpened()) {
            return;
        }
        
        // 写入视频帧
        video_writer_.write(image);
        total_frames_++;
        
    }
    
    uint64_t getTotalFrames() const {
        return total_frames_;
    }
    
private:
    cv::VideoWriter video_writer_;
    std::string output_filename_;
    std::atomic<uint64_t> total_frames_;
    double fps_;
};

int main(int argc, char* argv[]) {
    std::cout << "=== HV相机事件和视频录制示例程序 ===" << std::endl;
    
    // 解析命令行参数
    std::string event_output_file = "recorded_events.raw";
    std::string video_output_file = "recorded_video.avi";
    int duration_seconds = 10;
    double fps = 30.0;
    
    if (argc >= 2) {
        event_output_file = argv[1];
    }
    if (argc >= 3) {
        video_output_file = argv[2];
    }
    if (argc >= 4) {
        duration_seconds = std::atoi(argv[3]);
    }
    if (argc >= 5) {
        fps = std::atof(argv[4]);
    }
    
    std::cout << "事件输出文件: " << event_output_file << std::endl;
    std::cout << "视频输出文件: " << video_output_file << std::endl;
    std::cout << "录制时长: " << duration_seconds << " 秒" << std::endl;
    std::cout << "视频帧率: " << fps << " FPS" << std::endl;
    
    // 注册信号处理函数
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    // 创建相机实例
    const uint16_t VENDOR_ID = 0x1d6b;   
    const uint16_t PRODUCT_ID = 0x0105; 
    
    hv::HV_Camera camera(VENDOR_ID, PRODUCT_ID);
    EventRecorder event_recorder;
    VideoRecorder video_recorder;
    
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
    
    // 开始录制
    if (!event_recorder.startRecording(event_output_file)) {
        camera.close();
        return -1;
    }
    
    if (!video_recorder.startRecording(video_output_file, fps)) {
        event_recorder.stopRecording();
        camera.close();
        return -1;
    }
    
    // 启动事件和图像采集
    std::cout << "\n开始采集事件和图像数据..." << std::endl;
    std::cout << "按Ctrl+C停止录制" << std::endl;
    
    g_recording = true;
    
    // 绑定事件回调函数
    auto event_callback = [&event_recorder](const std::vector<Metavision::EventCD>& events) {
        try {
            event_recorder.onEventReceived(events);
        } catch (const std::exception& e) {
            std::cerr << "事件处理错误: " << e.what() << std::endl;
        }
    };
    
    // 绑定图像回调函数
    auto image_callback = [&video_recorder](const cv::Mat& image) {
        try {
            video_recorder.onImageReceived(image);
        } catch (const std::exception& e) {
            std::cerr << "图像处理错误: " << e.what() << std::endl;
        }
    };
    
    // 启动事件采集
    std::cout << "正在启动事件采集..." << std::endl;
    if (!camera.startEventCapture(event_callback)) {
        std::cerr << "错误: 无法启动事件采集" << std::endl;
        event_recorder.stopRecording();
        video_recorder.stopRecording();
        camera.close();
        return -1;
    }
    
    // 启动图像采集
    std::cout << "正在启动图像采集..." << std::endl;
    if (!camera.startImageCapture(image_callback)) {
        std::cerr << "错误: 无法启动图像采集" << std::endl;
        camera.stopEventCapture();
        event_recorder.stopRecording();
        video_recorder.stopRecording();
        camera.close();
        return -1;
    }
    
    // 等待一小段时间确保采集已启动
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // 录制指定时长或直到用户中断
    auto start_time = std::chrono::steady_clock::now();
    
    while (g_recording) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        auto current_time = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            current_time - start_time).count();
        
        if (elapsed >= duration_seconds) {
            std::cout << "\n录制时间已达到 " << duration_seconds << " 秒，停止录制" << std::endl;
            g_recording = false;
            break;
        }
    }
    
    // 停止采集
    std::cout << "\n正在停止事件和图像采集..." << std::endl;
    camera.stopEventCapture();
    camera.stopImageCapture();
    
    // 等待一小段时间确保所有数据都已处理
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // 停止录制并保存文件
    event_recorder.stopRecording();
    video_recorder.stopRecording();
    
    // 关闭相机
    camera.close();
    std::cout << "相机已关闭" << std::endl;
    
    std::cout << "\n=== 录制完成 ===" << std::endl;
    std::cout << "事件文件: " << event_output_file << std::endl;
    std::cout << "视频文件: " << video_output_file << std::endl;
    std::cout << "总事件数: " << event_recorder.getTotalEvents() << std::endl;
    std::cout << "总视频帧数: " << video_recorder.getTotalFrames() << std::endl;
    
    return 0;
}