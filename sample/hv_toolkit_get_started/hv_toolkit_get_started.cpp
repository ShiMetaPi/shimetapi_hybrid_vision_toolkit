#include <iostream>
#include <hv_camera.h>

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