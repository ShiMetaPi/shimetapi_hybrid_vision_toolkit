#include <iostream>
#include <hv_camera.h>
#include <chrono>
#include <iostream>
#include <atomic>

// 放在类外或类内 static，确保所有回调共享
static std::atomic<int>   g_counter{0};                           // 本周期内的回调次数
static std::chrono::steady_clock::time_point g_lastPrint =
        std::chrono::steady_clock::now();                          // 上一次打印时间

int main() {
    std::cout << "HV Toolkit Example" << std::endl;
    
    // 创建相机实例
    hv::HV_Camera camera(0x1d6b, 0x0105);  
    
    // 尝试打开相机
    if (camera.open()) {
        std::cout << "Camera opened successfully!" << std::endl;
        
        // 定义事件回调函数
        auto eventCallback = [](const std::vector<Metavision::EventCD>& events) {
            std::cout << "Received " << events.size() << " events" << std::endl;

             // ---------------- 新增计数逻辑 ----------------
            ++g_counter;

            auto now = std::chrono::steady_clock::now();
            if (now - g_lastPrint >= std::chrono::seconds(1)) {
            // 把 g_lastPrint 和 g_counter 一起更新，防止并发重复打印
            int fps = g_counter.exchange(0);          // 取出并清零
            g_lastPrint = now;

            std::cout << "[FPS] " << fps << " callback(s)/second" << std::endl;
            }
        };
        
        // 定义图像回调函数
        auto imageCallback = [](const cv::Mat& image) {
            std::cout << "Received image: " << image.cols << "x" << image.rows << std::endl;
        };
        
        // 启动事件采集和图像采集（传入回调函数）
        camera.startEventCapture(eventCallback);
        camera.startImageCapture(imageCallback);
        
        std::cout << "Press Enter to stop..." << std::endl;
        std::cin.get();
        
        // 停止采集
        camera.stopEventCapture();
        camera.stopImageCapture();
        camera.close();
        
        std::cout << "Camera closed." << std::endl;
    } else {
        std::cout << "Failed to open camera." << std::endl;
        return -1;
    }
    
    return 0;
}