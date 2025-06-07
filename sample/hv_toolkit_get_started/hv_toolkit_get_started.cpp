/*
 * Copyright 2025 ShiMetaPi
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0 
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
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