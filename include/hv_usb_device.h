#ifndef HV_USB_DEVICE_H
#define HV_USB_DEVICE_H

#include <libusb-1.0/libusb.h>
#include <string>
#include <iostream>

namespace hv {

/**
 * USB设备管理类 - 负责USB设备的打开、关闭和数据传输
 */
class USBDevice {
public:
    /**
     * 构造函数
     * @param vendor_id USB设备厂商ID
     * @param product_id USB设备产品ID
     */
    USBDevice(uint16_t vendor_id, uint16_t product_id);
    
    /**
     * 析构函数
     */
    ~USBDevice();
    
    /**
     * 打开USB设备
     * @return 是否成功打开设备
     */
    bool open();
    
    /**
     * 检查设备是否已打开
     * @return 设备是否已打开
     */
    bool isOpen() const;
    
    /**
     * 关闭USB设备
     */
    void close();
    
    /**
     * 获取端点地址
     * @param index 端点索引
     * @return 端点地址
     */
    uint8_t getEndpointAddress(int index) const;
    
    /**
     * 执行批量数据传输
     * @param endpoint 端点地址
     * @param data 数据缓冲区
     * @param length 数据长度
     * @param transferred 实际传输的字节数
     * @param timeout 超时时间(毫秒)
     * @return 是否传输成功
     */
    bool bulkTransfer(uint8_t endpoint, unsigned char* data, int length, int* transferred, unsigned int timeout);

private:
    // USB设备相关成员
    const uint16_t vendor_id_;
    const uint16_t product_id_;
    libusb_context* ctx_;
    libusb_device* device_;
    libusb_device_handle* handle_;
    bool attached_;
    uint8_t endpoints_[8]; // 存储端点地址
    
    // 禁止拷贝构造和赋值
    USBDevice(const USBDevice&) = delete;
    USBDevice& operator=(const USBDevice&) = delete;
};

} // namespace hv

#endif // HV_USB_DEVICE_H