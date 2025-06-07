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
#ifndef HV_EVENT_WRITER_H
#define HV_EVENT_WRITER_H

#include "hv_evt2_codec.h"
#include <string>
#include <fstream>
#include <vector>
#include <memory>
#include <metavision/sdk/base/events/event_cd.h>

namespace hv {

/**
 * HV事件文件写入器类
 * 支持将EventCD事件编码并写入EVT2格式的raw文件
 */
class HVEventWriter {
public:
    HVEventWriter();
    ~HVEventWriter();
    
    /**
     * 创建新文件并写入文件头
     * @param filename 文件路径
     * @param width 图像宽度
     * @param height 图像高度
     * @param start_timestamp 起始时间戳
     * @return 是否成功创建
     */
    bool open(const std::string& filename, uint32_t width, uint32_t height, uint64_t start_timestamp = 0);
    
    /**
     * 关闭文件
     */
    void close();
    
    /**
     * 检查文件是否已打开
     */
    bool isOpen() const;
    
    /**
     * 批量写入事件
     * @param events EventCD事件向量
     * @return 成功写入的事件数量
     */
    size_t writeEvents(const std::vector<Metavision::EventCD>& events);
    
    /**
     * 强制刷新缓冲区到磁盘
     */
    void flush();
    
    /**
     * 获取已写入的事件数量
     */
    uint64_t getWrittenEventCount() const;
    
    /**
     * 获取文件大小（字节）
     */
    size_t getFileSize() const;
    
private:
    std::ofstream file_;
    evt2::EVT2Header header_;
    std::unique_ptr<evt2::EventTimeEncoder> time_encoder_;
    bool is_open_;
    uint64_t event_count_;
    std::vector<uint8_t> write_buffer_;
    
    void writeHeader();
    void writeRawData(const std::vector<uint8_t>& data);
    void flushBuffer();
};

} // namespace hv

#endif // HV_EVENT_WRITER_H