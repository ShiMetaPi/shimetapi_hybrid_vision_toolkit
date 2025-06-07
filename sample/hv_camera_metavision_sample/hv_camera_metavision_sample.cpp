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
/**
 * @file hv_camera_metavision_sample.cpp
 * @brief HV相机SDK使用示例 - 基于Metavision SDK的功能
 */

#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>
#include <signal.h>
#include <mutex>
#include <opencv2/opencv.hpp>
#include "hv_camera.h"

// 引入Metavision SDK的相关头文件
#include <metavision/sdk/core/algorithms/periodic_frame_generation_algorithm.h>
#include <metavision/sdk/core/algorithms/flip_x_algorithm.h>

// 全局变量用于控制程序运行
std::atomic<bool> running(true);

// 信号处理函数，用于优雅地退出程序
void signalHandler(int signum) {
    std::cout << "中断信号 (" << signum << ") 已接收，准备退出..." << std::endl;
    running = false;
}

// 全局变量用于OpenCV显示
cv::Mat evs_frame;
cv::Mat aps_frame;
std::mutex evs_frame_mutex;
std::mutex aps_frame_mutex;
bool evs_frame_ready = false;
bool aps_frame_ready = false;

// 图像数据回调函数 - 处理APS数据
void onImageData(const cv::Mat& image) {
    std::lock_guard<std::mutex> lock(aps_frame_mutex);
    image.copyTo(aps_frame);
    aps_frame_ready = true;
}

int main(int argc, char** argv) {
    // 注册信号处理函数
    signal(SIGINT, signalHandler);
    
    // 设置相机的USB厂商ID和产品ID
    uint16_t vendor_id = 0x1d6b; 
    uint16_t product_id = 0x0105;
    
    if (argc >= 3) {
        // 如果命令行提供了ID，则使用命令行参数
        vendor_id = static_cast<uint16_t>(std::stoi(argv[1], nullptr, 16));
        product_id = static_cast<uint16_t>(std::stoi(argv[2], nullptr, 16));
    }
    
    std::cout << "使用 USB 设备 ID: vendor=0x" << std::hex << vendor_id 
              << ", product=0x" << product_id << std::dec << std::endl;
    
    // 创建相机对象
    hv::HV_Camera camera(vendor_id, product_id);
    
    // 打开相机
    std::cout << "正在打开相机..." << std::endl;
    if (!camera.open()) {
        std::cerr << "无法打开相机设备，请检查连接和设备ID" << std::endl;
        return -1;
    }
    std::cout << "相机已成功打开" << std::endl;
    
    // 相机分辨率
    int camera_width = HV_EVS_WIDTH;
    int camera_height = HV_EVS_HEIGHT;
    
    // 设置累积时间和帧率（20ms和50fps）
    const std::uint32_t acc = 50000;
    double fps = 20;
    
    // 创建帧生成器
    auto frame_gen = Metavision::PeriodicFrameGenerationAlgorithm(camera_width, camera_height, acc, fps);
    
    // 设置帧生成器的回调函数 - 处理EVS数据
    frame_gen.set_output_callback([&](Metavision::timestamp ts, cv::Mat& frame) {
        std::lock_guard<std::mutex> lock(evs_frame_mutex);
        frame.copyTo(evs_frame);
        evs_frame_ready = true;
    });
    
    // 创建X轴翻转算法
    auto flip_algo = Metavision::FlipXAlgorithm(camera_width - 1);
    
    // 启动事件数据采集
    std::cout << "启动事件数据采集..." << std::endl;
    if (!camera.startEventCapture([&](const std::vector<hv::EventCD>& events) {
        // 处理事件：翻转X轴并生成帧
        std::vector<Metavision::EventCD> output;
        flip_algo.process_events(events.data(), events.data() + events.size(), std::back_inserter(output));
        frame_gen.process_events(output.begin(), output.end());
    })) {
        std::cerr << "启动事件数据采集失败" << std::endl;
        camera.close();
        return -1;
    }
    
    // 启动图像数据采集
    std::cout << "启动图像数据采集..." << std::endl;
    if (!camera.startImageCapture([&](const cv::Mat& image) {
        onImageData(image);
    })) {
        std::cerr << "启动图像数据采集失败" << std::endl;
        camera.stopEventCapture();
        camera.close();
        return -1;
    }
    
    std::cout << "数据采集已启动，按Ctrl+C或ESC键退出程序" << std::endl;
    
    // 创建OpenCV窗口
    cv::namedWindow("EVS Data", cv::WINDOW_AUTOSIZE);
    cv::namedWindow("APS Data", cv::WINDOW_AUTOSIZE);
    
    // 主循环
    while (running) {
        // 显示EVS数据
        {
            std::lock_guard<std::mutex> lock(evs_frame_mutex);
            if (evs_frame_ready && !evs_frame.empty()) {
                cv::imshow("EVS Data", evs_frame);
            }
        }
        
        // 显示APS数据
        {
            std::lock_guard<std::mutex> lock(aps_frame_mutex);
            if (aps_frame_ready && !aps_frame.empty()) {
                cv::imshow("APS Data", aps_frame);
            }
        }
        
        // 处理窗口事件
        int key = cv::waitKey(1) & 0xFF;
        if (key == 27 || key == 'q' || key == 'Q') { // ESC或Q键退出
            running = false;
        }
    }
    
    std::cout << "正在停止数据采集..." << std::endl;
    
    // 停止数据采集
    camera.stopImageCapture();
    camera.stopEventCapture();
    
    // 关闭相机
    camera.close();
    
    // 销毁OpenCV窗口
    cv::destroyAllWindows();
    
    std::cout << "程序已退出" << std::endl;
    return 0;
}
