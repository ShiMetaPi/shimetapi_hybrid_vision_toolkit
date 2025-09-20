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
#ifndef HV_CAMERA_H
#define HV_CAMERA_H

#include <string>
#include <functional>
#include <opencv2/opencv.hpp>
#include <mutex>
#include <vector>
#include <memory>
#include <queue>
#include <condition_variable>

// 引入Metavision事件数据结构
#include <metavision/sdk/base/events/event_cd.h>
#include <metavision/sdk/base/events/event2d.h>

// 前向声明，避免包含完整的USB设备头文件
namespace hv {
    class USBDevice;
}

// 定义常量
#define HV_BUF_LEN (4096 * 128)
#define HV_SUB_FULL_BYTE_SIZE (32768)
#define HV_SUB_VALID_BYTE_SIZE (29200)
#define HV_EVS_WIDTH (768)
#define HV_EVS_HEIGHT (608)
#define HV_EVS_SUB_WIDTH (384)
#define HV_EVS_SUB_HEIGHT (304)
#define HV_APS_WIDTH (768)
#define HV_APS_HEIGHT (608)
#define HV_APS_DATA_LEN (HV_APS_WIDTH * HV_APS_HEIGHT * 3 / 2)

namespace hv {

// 使用Metavision的事件数据结构
using EventCD = Metavision::EventCD;

// 事件回调函数类型
typedef std::function<void(const std::vector<EventCD>&)> EventCallback;

// 图像回调函数类型
typedef std::function<void(const cv::Mat&)> ImageCallback;

/**
 * HV_Camera类 - 用于获取DVS相机的事件数据和图像数据
 */
class HV_Camera {
public:
    /**
     * 构造函数
     * @param vendor_id USB设备厂商ID
     * @param product_id USB设备产品ID
     */
    HV_Camera(uint16_t vendor_id, uint16_t product_id);
    
    /**
     * 析构函数
     */
    ~HV_Camera();
    
    /**
     * 打开相机设备
     * @return 是否成功打开设备
     */
    bool open();
    
    /**
     * 检查设备是否已打开
     * @return 设备是否已打开
     */
    bool isOpen() const;
    
    /**
     * 关闭相机设备
     */
    void close();
    
    /**
     * 启动事件数据采集
     * @param callback 事件回调函数
     * @return 是否成功启动
     */
    bool startEventCapture(EventCallback callback);
    
    /**
     * 停止事件数据采集
     */
    void stopEventCapture();
    
    /**
     * 启动图像数据采集
     * @param callback 图像回调函数
     * @return 是否成功启动
     */
    bool startImageCapture(ImageCallback callback);
    
    /**
     * 停止图像数据采集
     */
    void stopImageCapture();
    
    /**
     * 获取最新的图像
     * @return 图像数据
     */
    cv::Mat getLatestImage() const;
    
    /**
     * 清空事件数据队列
     * 用于在开始录制前清空缓存的事件数据
     */
    void clearEventQueue();

private:
    // USB设备
    std::unique_ptr<USBDevice> usb_device_;
    uint8_t event_endpoint_; // 事件数据端点
    uint8_t image_endpoint_; // 图像数据端点
    
    // 线程控制
    bool event_running_;
    bool image_running_;
    
    // 回调函数
    EventCallback event_callback_;
    ImageCallback image_callback_;
    
    // 最新图像缓存
    cv::Mat latest_image_;
    
    // 互斥锁
    mutable std::mutex image_mutex_;
    
    // 事件数据缓存相关
    struct EventDataBuffer {
        std::unique_ptr<uint8_t[]> data;
        size_t size;
        EventDataBuffer(size_t s) : data(std::make_unique<uint8_t[]>(s)), size(s) {}
    };
    
    std::queue<std::unique_ptr<EventDataBuffer>> event_data_queue_;
    std::mutex event_queue_mutex_;
    std::condition_variable event_queue_cv_;
    bool event_processing_running_;
    static const size_t MAX_QUEUE_SIZE = 6000; // 最大缓存队列大小
    
    // 性能优化：预分配事件数组
    std::vector<EventCD> reusable_event_array_;
    static const size_t ESTIMATED_EVENTS_PER_FRAME = 10000; // 预估每帧事件数

    // 线程函数
    void eventThreadFunc();           // USB接收线程
    void eventProcessingThreadFunc(); // 事件处理线程
    void imageThreadFunc();
    
    // 处理事件数据
    void processEventData(uint8_t* dataPtr);
    
    // 禁止拷贝构造和赋值
    HV_Camera(const HV_Camera&) = delete;
    HV_Camera& operator=(const HV_Camera&) = delete;
};

} // namespace hv

#endif // HV_CAMERA_H