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

    /**
     * 清除设备冗余数据
     * @return 是否清除成功
     */
    bool clearSharedMemory() ;

    /**
     * 获取USB设备句柄
     * @return USB设备句柄
     */
    libusb_device_handle* getHandle() const;

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