#include <iostream>
#include <opencv2/opencv.hpp>
#include <chrono>
#include <thread>
#include "hv_event_reader.h"

// 默认图像尺寸常量
const int DEFAULT_WIDTH = 640;
const int DEFAULT_HEIGHT = 512;

// 事件积累时间长度（微秒）
const std::uint32_t acc = 20000; // 20ms的事件积累为一帧

// 播放帧率（每秒帧数）
const int fps = 15; // 每秒30帧

class EventViewer {
public:
    EventViewer() : width_(DEFAULT_WIDTH), height_(DEFAULT_HEIGHT) {}
    
    bool openFile(const std::string& filename) {
        if (!reader_.open(filename)) {
            std::cerr << "Failed to open file: " << filename << std::endl;
            return false;
        }
        
        // 获取图像尺寸
        auto image_size = reader_.getImageSize();
        width_ = image_size.first;
        height_ = image_size.second;
        
        if (width_ == 0 || height_ == 0) {
            width_ = DEFAULT_WIDTH;
            height_ = DEFAULT_HEIGHT;
            std::cout << "Warning: No image size found in header, using default: " 
                      << width_ << "x" << height_ << std::endl;
        }
        
        std::cout << "Opened file: " << filename << std::endl;
        std::cout << "Image size: " << width_ << "x" << height_ << std::endl;
        
        return true;
    }
    
    void play() {
        if (!reader_.isOpen()) {
            std::cerr << "No file opened" << std::endl;
            return;
        }
        
        // std::cout << "[DEBUG] Creating OpenCV window..." << std::endl;
        cv::namedWindow("Event Viewer", cv::WINDOW_AUTOSIZE);
        // std::cout << "[DEBUG] OpenCV window created successfully" << std::endl;
        
        // 使用流式读取而不是一次性读取所有事件
        std::cout << "Starting streaming playback..." << std::endl;
        
        // std::cout << "[DEBUG] Resetting reader to file start..." << std::endl;
        reader_.reset(); // 重置到文件开始
        // std::cout << "[DEBUG] Reader reset completed" << std::endl;
        
        auto start_play_time = std::chrono::high_resolution_clock::now();
        Metavision::timestamp current_time = 0;
        
        std::vector<Metavision::EventCD> frame_events;
        bool first_frame = true;
        int frame_count = 0;
        
        // std::cout << "[DEBUG] Starting main playback loop..." << std::endl;
        
        bool end_of_file = false;
        
        while (!end_of_file) {
            frame_count++;
            
            // 按时间切片读取事件
            frame_events.clear();
            Metavision::timestamp frame_end_time = current_time + acc;
            
            // 持续读取事件直到达到时间窗口
            std::vector<Metavision::EventCD> batch_events;
            size_t total_events_read = 0;
            bool frame_complete = false;
            
            while (!frame_complete && !end_of_file) {
                batch_events.clear();
                size_t events_read = reader_.readEvents(1000, batch_events);
                
                if (events_read == 0) {
                    if (frame_events.empty()) {
                        std::cout << "End of file reached" << std::endl;
                        end_of_file = true;
                        break;
                    }
                    frame_complete = true;
                    break;
                }
                
                // 处理读取到的事件
                for (const auto& event : batch_events) {
                    if (first_frame) {
                        current_time = event.t;
                        frame_end_time = current_time + acc;
                        first_frame = false;
                        std::cout << "First event timestamp: " << current_time << " us" << std::endl;
                    }
                    
                    // 如果事件时间戳超过当前帧的结束时间，停止添加
                    if (event.t >= frame_end_time) {
                        frame_complete = true;
                        break;
                    }
                    
                    frame_events.push_back(event);
                    total_events_read++;
                }
            }
            
            // 如果到达文件末尾且没有事件，退出循环
            if (end_of_file && frame_events.empty()) {
                break;
            }
            
            // 如果当前帧没有事件，跳到下一个时间窗口
            if (frame_events.empty()) {
                current_time = frame_end_time;
                continue;
            }
            
            // std::cout << "[DEBUG] Frame " << frame_count << ": Creating image matrix (" << height_ << "x" << width_ << ")..." << std::endl;
            
            // 创建当前帧图像
            cv::Mat frame;
            try {
                frame = cv::Mat(height_, width_, CV_8UC3, cv::Scalar(0, 0, 0));
                // std::cout << "[DEBUG] Frame " << frame_count << ": Image matrix created successfully" << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "[ERROR] Failed to create image matrix: " << e.what() << std::endl;
                break;
            }
            
            // std::cout << "[DEBUG] Frame " << frame_count << ": Processing " << frame_events.size() << " events..." << std::endl;
            
            // 处理当前批次的事件
            int valid_events = 0;
            int invalid_events = 0;
            for (size_t i = 0; i < frame_events.size(); ++i) {
                const auto& event = frame_events[i];
                
                // 检查坐标是否在图像范围内
                if (event.x >= 0 && event.x < width_ && 
                    event.y >= 0 && event.y < height_) {
                    
                    valid_events++;
                    try {
                        if (event.p == 0) {
                            // OFF事件 - 蓝黄色
                            frame.at<cv::Vec3b>(event.y, event.x) = cv::Vec3b(135, 206, 235);
                        } else {
                            // ON事件 - 白色
                            frame.at<cv::Vec3b>(event.y, event.x) = cv::Vec3b(255, 255, 255);
                        }
                    } catch (const std::exception& e) {
                        std::cerr << "[ERROR] Failed to set pixel at (" << event.x << ", " << event.y << "): " << e.what() << std::endl;
                        break;
                    }
                } else {
                    invalid_events++;
                    if (invalid_events <= 5) { // 只打印前5个无效事件
                        // std::cout << "[DEBUG] Invalid event coordinates: (" << event.x << ", " << event.y << ") - out of bounds (" << width_ << "x" << height_ << ")" << std::endl;
                    }
                }
            }
            
            // std::cout << "[DEBUG] Frame " << frame_count << ": Valid events: " << valid_events << ", Invalid events: " << invalid_events << std::endl;
            
            // 显示帧信息
            std::string time_info = "Time: " + std::to_string(current_time/1000) + "ms";
            std::string event_info = "Events: " + std::to_string(total_events_read);
            try {
                cv::putText(frame, time_info, cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 
                           0.7, cv::Scalar(0, 255, 0), 2);
                cv::putText(frame, event_info, cv::Point(10, 60), cv::FONT_HERSHEY_SIMPLEX, 
                           0.7, cv::Scalar(0, 255, 0), 2);
            } catch (const std::exception& e) {
                std::cerr << "[ERROR] Failed to add text overlay: " << e.what() << std::endl;
            }
            
            // 显示图像
            // std::cout << "[DEBUG] Frame " << frame_count << ": Displaying image..." << std::endl;
            try {
                cv::imshow("Event Viewer", frame);
                // std::cout << "[DEBUG] Frame " << frame_count << ": Image displayed successfully" << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "[ERROR] Failed to display image: " << e.what() << std::endl;
                break;
            }
            
            // 检查按键
            int key = cv::waitKey(1);
            if (key == 27 || key == 'q') { // ESC或q键退出
                // std::cout << "[DEBUG] Exit key pressed" << std::endl;
                break;
            } else if (key == ' ') { // 空格键暂停
                // std::cout << "[DEBUG] Pause key pressed" << std::endl;
                cv::waitKey(0);
            }
            
            // 控制播放速度 - 根据fps变量控制帧率
            std::this_thread::sleep_for(std::chrono::milliseconds(1000 / fps));
            
            current_time = frame_end_time;
            
            // 每10帧打印一次进度
            if (frame_count % 10 == 0) {
                // std::cout << "[DEBUG] Processed " << frame_count << " frames" << std::endl;
            }
        }
        
        std::cout << "Playback finished" << std::endl;
        // std::cout << "[DEBUG] Destroying OpenCV windows..." << std::endl;
        cv::destroyAllWindows();
        // std::cout << "[DEBUG] OpenCV windows destroyed" << std::endl;
    }
    
private:
    hv::HVEventReader reader_;
    int width_;
    int height_;
};

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cout << "Usage: " << argv[0] << " <evt2_file.raw>" << std::endl;
        std::cout << "\nControls:" << std::endl;
        std::cout << "  ESC/q - Exit" << std::endl;
        std::cout << "  SPACE - Pause/Resume" << std::endl;
        return -1;
    }
    
    std::string filename = argv[1];
    
    EventViewer viewer;
    if (!viewer.openFile(filename)) {
        return -1;
    }
    
    std::cout << "Starting playback..." << std::endl;
    viewer.play();
    
    return 0;
}