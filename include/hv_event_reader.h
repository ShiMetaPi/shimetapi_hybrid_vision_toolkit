#ifndef HV_EVENT_READER_H
#define HV_EVENT_READER_H

#include "hv_evt2_codec.h"
#include <string>
#include <fstream>
#include <vector>
#include <functional>
#include <metavision/sdk/base/events/event_cd.h>

namespace hv {

/**
 * HV事件文件读取器类
 * 支持读取EVT2格式的raw文件并转换为EventCD事件
 */
class HVEventReader {
public:
    using EventCallback = std::function<void(const std::vector<Metavision::EventCD>&)>;
    
    HVEventReader();
    ~HVEventReader();
    
    /**
     * 打开事件文件
     * @param filename 文件路径
     * @return 是否成功打开
     */
    bool open(const std::string& filename);
    
    /**
     * 关闭文件
     */
    void close();
    
    /**
     * 检查文件是否已打开
     */
    bool isOpen() const;
    
    /**
     * 获取文件头信息
     */
    const evt2::EVT2Header& getHeader() const;
    
    /**
     * 读取指定数量的事件
     * @param num_events 要读取的事件数量
     * @param events 输出的事件向量
     * @return 实际读取的事件数量
     */
    size_t readEvents(size_t num_events, std::vector<Metavision::EventCD>& events);
    
    /**
     * 读取所有事件
     * @param events 输出的事件向量
     * @return 读取的事件总数
     */
    size_t readAllEvents(std::vector<Metavision::EventCD>& events);
    
    /**
     * 流式读取事件（适用于大文件）
     * @param batch_size 每批次读取的事件数量
     * @param callback 事件处理回调函数
     * @return 总共处理的事件数量
     */
    size_t streamEvents(size_t batch_size, EventCallback callback);
    
    /**
     * 重置读取位置到文件开始
     */
    void reset();
    
    /**
     * 获取图像尺寸
     */
    std::pair<uint32_t, uint32_t> getImageSize() const;
    
private:
    std::ifstream file_;
    evt2::EVT2Header header_;
    evt2::EVT2Decoder decoder_;
    bool is_open_;
    std::streampos data_start_pos_;
    std::vector<uint8_t> read_buffer_;
    
    bool readHeader();
    size_t readRawData(std::vector<uint8_t>& buffer, size_t max_bytes);
};

} // namespace hv

#endif // HV_EVENT_READER_H