#ifndef HV_CAMERA_H
#define HV_CAMERA_H

#include <string>
#include <functional>
#include <opencv2/opencv.hpp>
#include <mutex>
#include <vector>
#include <memory>

// 引入Metavision事件数据结构
#include <metavision/sdk/base/events/event_cd.h>
#include <metavision/sdk/base/events/event2d.h>

// 前向声明，避免包含完整的USB设备头文件
namespace hv {
    class USBDevice;
}

// 定义常量
#define HV_BUF_LEN (4096 * 64)
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
    
    // 线程函数
    void eventThreadFunc();
    void imageThreadFunc();
    
    // 处理事件数据
    void processEventData(uint8_t* dataPtr);
    
    // 禁止拷贝构造和赋值
    HV_Camera(const HV_Camera&) = delete;
    HV_Camera& operator=(const HV_Camera&) = delete;
};

} // namespace hv

#endif // HV_CAMERA_H