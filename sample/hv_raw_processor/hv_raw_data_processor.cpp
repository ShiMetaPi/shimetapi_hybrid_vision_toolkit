#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <string>
#include <chrono>
#include <iomanip>
#include <cstring>
#include <sstream>
#include <ctime>
#include <memory>

// 引入Metavision事件数据结构
#include <metavision/sdk/base/events/event_cd.h>
#include <metavision/sdk/base/events/event2d.h>

// 定义常量（与hv_camera.h保持一致）
#define HV_BUF_LEN (4096 * 128)
#define HV_SUB_FULL_BYTE_SIZE (32768)
#define HV_SUB_VALID_BYTE_SIZE (29200)
#define HV_EVS_WIDTH (768)
#define HV_EVS_HEIGHT (608)
#define HV_EVS_SUB_WIDTH (384)
#define HV_EVS_SUB_HEIGHT (304)

// 内存池相关常量
#define POOL_SIZE 10
#define MAX_EVENTS_PER_ARRAY 100000

using EventCD = Metavision::EventCD;

// 时间戳元数据结构
struct TimestampMetadata {
    uint64_t timestamp;      // 处理后的时间戳（除以200后）
    uint64_t raw_timestamp;  // 原始时间戳
    uint64_t subframe;       // 子帧编号
    size_t block_index;      // 数据块索引
    size_t sub_index;        // 子帧在块中的索引
    
    TimestampMetadata(uint64_t ts, uint64_t raw_ts, uint64_t sf, size_t bi, size_t si)
        : timestamp(ts), raw_timestamp(raw_ts), subframe(sf), block_index(bi), sub_index(si) {}
};

// EVT2格式相关定义（从hv_evt2_codec.h复制）
namespace evt2 {
    enum class EventTypes : uint8_t {
        CD_OFF        = 0x00,
        CD_ON         = 0x01,
        EVT_TIME_HIGH = 0x08,
        EXT_TRIGGER   = 0x0A,
    };

    struct RawEvent {
        unsigned int pad : 28;
        unsigned int type : 4;
    };

    struct RawEventTime {
        unsigned int timestamp : 28;
        unsigned int type : 4;
    };

    struct RawEventCD {
        unsigned int y : 11;
        unsigned int x : 11;
        unsigned int timestamp : 6;
        unsigned int type : 4;
    };

    using Timestamp = uint64_t;

    // 时间编码器类
    class EventTimeEncoder {
    public:
        explicit EventTimeEncoder(Timestamp base) : th((base / TH_NEXT_STEP) * TH_NEXT_STEP) {}
        
        void encode(RawEvent* raw_event) {
            RawEventTime* ev_th = reinterpret_cast<RawEventTime*>(raw_event);
            ev_th->timestamp = th >> N_LOWER_BITS_TH;
            ev_th->type = static_cast<uint8_t>(EventTypes::EVT_TIME_HIGH);
            th += TH_NEXT_STEP;
        }
        
        Timestamp getNextTimeHigh() const { return th; }
        
        void reset(Timestamp base = 0) {
            th = (base / TH_NEXT_STEP) * TH_NEXT_STEP;
        }
        
    private:
        Timestamp th;
        static constexpr char N_LOWER_BITS_TH = 6;
        static constexpr unsigned int REDUNDANCY_FACTOR = 4;
        static constexpr Timestamp TH_STEP = (1ul << N_LOWER_BITS_TH);
        static constexpr Timestamp TH_NEXT_STEP = TH_STEP / REDUNDANCY_FACTOR;
    };

    // CD事件编码器类
    class EventCDEncoder {
    public:
        void encode(RawEvent* raw_event) {
            RawEventCD* raw_cd_event = reinterpret_cast<RawEventCD*>(raw_event);
            raw_cd_event->x = x;
            raw_cd_event->y = y;
            raw_cd_event->timestamp = t & 0x3F;
            raw_cd_event->type = p ? static_cast<uint8_t>(EventTypes::CD_ON) : static_cast<uint8_t>(EventTypes::CD_OFF);
        }
        
        void setEvent(unsigned short x_coord, unsigned short y_coord, short polarity, Timestamp timestamp) {
            x = x_coord;
            y = y_coord;
            p = polarity;
            t = timestamp;
        }
        
    private:
        unsigned short x;
        unsigned short y;
        short p;
        Timestamp t;
    };

    // 转换函数
    size_t convertToEVT2(const std::vector<Metavision::EventCD>& events,
                         std::vector<uint8_t>& raw_data,
                         EventTimeEncoder& time_encoder) {
        if (events.empty()) {
            raw_data.clear();
            return 0;
        }
        
        std::vector<RawEvent> raw_events;
        raw_events.reserve(events.size() + events.size() / 1000);
        
        EventCDEncoder cd_encoder;
        
        // 添加初始时间高位事件
        RawEvent time_event;
        time_encoder.encode(&time_event);
        raw_events.push_back(time_event);
        
        for (const auto& event : events) {
            // 检查是否需要插入时间高位事件
            while (static_cast<evt2::Timestamp>(event.t) >= time_encoder.getNextTimeHigh()) {
                RawEvent th_event;
                time_encoder.encode(&th_event);
                raw_events.push_back(th_event);
            }
            
            // 编码CD事件
            cd_encoder.setEvent(event.x, event.y, event.p, event.t);
            RawEvent cd_event;
            cd_encoder.encode(&cd_event);
            raw_events.push_back(cd_event);
        }
        
        // 转换RawEvent向量为字节向量
        raw_data.resize(raw_events.size() * sizeof(RawEvent));
        std::memcpy(raw_data.data(), raw_events.data(), raw_data.size());
        
        return events.size();
    }

    // 生成EVT2头部
    std::vector<std::string> generateEVT2Header(uint32_t width, uint32_t height, const std::string& integrator = "Shimeta") {
        std::vector<std::string> header_lines;
        
        // 生成日期
        const std::time_t tt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        const struct std::tm* ptm = std::localtime(&tt);
        
        std::ostringstream date_stream;
        date_stream << "% date " << std::put_time(ptm, "%Y-%m-%d %H:%M:%S");
        header_lines.push_back(date_stream.str());
        
        // 格式行
        std::ostringstream format_stream;
        format_stream << "% format EVT2;width=" << width << ";height=" << height;
        header_lines.push_back(format_stream.str());
        
        // 集成器
        header_lines.push_back("% integrator_name " + integrator);
        
        // 结束标记
        header_lines.push_back("% end");
        
        return header_lines;
    }
}

class RawDataProcessor {
public:
    RawDataProcessor() {
        // 初始化内存池
        for (int i = 0; i < POOL_SIZE; ++i) {
            buffer_pool_.push_back(std::vector<uint8_t>(HV_BUF_LEN));
            event_array_pool_.push_back(std::vector<EventCD>());
            event_array_pool_[i].reserve(MAX_EVENTS_PER_ARRAY);
        }
        
        // 缓存预热
        warmupCache();
        
        // 初始化EVT2时间编码器
        time_encoder_ = std::make_unique<evt2::EventTimeEncoder>(0);
    }
    ~RawDataProcessor() = default;
    
    // 缓存预热函数
    void warmupCache() {
        // 简单的缓存预热，访问内存池
        for (auto& buffer : buffer_pool_) {
            std::fill(buffer.begin(), buffer.begin() + 1024, 0);
        }
    }
    
    // 处理单个子帧
    // 替换原有的 processSingleSubframe 和 processEventData 逻辑，采用 hv_camera.cpp 的实现方式
    std::vector<EventCD> processSingleSubframe(uint8_t* dataPtr, size_t block_index = 0, int subframe_idx = 0, std::vector<TimestampMetadata>* timestamp_metadata = nullptr) {
        uint64_t* pixelBufferPtr = reinterpret_cast<uint64_t*>(dataPtr);
        std::vector<EventCD> eventArray;
        eventArray.reserve(HV_EVS_SUB_HEIGHT * HV_EVS_SUB_WIDTH);
        uint64_t timestamp = 0;
        uint64_t subframe = 0;
        uint64_t buffer = pixelBufferPtr[0];
        uint64_t raw_timestamp = (buffer >> 24) & 0xFFFFFFFFFF;
        timestamp = raw_timestamp;
        uint64_t header_vec = buffer & 0xFFFFFF;
        if (header_vec != 0xFFFF) {
            std::cerr << "bits process error" << std::endl;
        }
        buffer = pixelBufferPtr[1];
        subframe = (buffer >> 44) & 0xF;
        timestamp /= 200;
        if (timestamp_metadata != nullptr) {
            timestamp_metadata->emplace_back(timestamp, raw_timestamp, subframe, block_index, subframe_idx);
        }
        pixelBufferPtr += 2;
        int x = 0, y = 0, x_offset = 0, y_offset = 0;
        switch (subframe) {
        case 0:
            x_offset = 0;
            y_offset = 0;
            break;
        case 1:
            x_offset = 1;
            y_offset = 0;
            break;
        case 2:
            x_offset = 0;
            y_offset = 1;
            break;
        case 3:
            x_offset = 1;
            y_offset = 1;
            break;
        }
        y = y_offset;
        for (int i = 0; i < HV_EVS_SUB_HEIGHT; i++) {
            x = x_offset;
            for (int j = 0; j < HV_EVS_SUB_WIDTH / 32; j++) {
                buffer = pixelBufferPtr[j];
                for (int k = 0; k < 64; k += 2) {
                    uint64_t pix = (buffer >> k) & 0x3;
                    if (x >= HV_EVS_WIDTH || y >= HV_EVS_HEIGHT) {
                        x += 2;
                        continue;
                    }
                    if (pix > 0) {
                        pix >>= 1;
                        EventCD event(static_cast<unsigned short>(x), static_cast<unsigned short>(y), static_cast<short>(pix), static_cast<Metavision::timestamp>(timestamp));
                        eventArray.push_back(event);
                    }
                    x += 2;
                }
            }
            pixelBufferPtr += HV_EVS_SUB_WIDTH / 32;
            y += 2;
        }
        return eventArray;
    }
    
    // 处理单个数据块（等同于processEventData函数）
    std::vector<EventCD> processEventData(uint8_t* dataPtr, size_t block_index = 0, std::vector<TimestampMetadata>* timestamp_metadata = nullptr) {
        uint64_t* pixelBufferPtr = reinterpret_cast<uint64_t*>(dataPtr);
        std::vector<EventCD> eventArray;
        eventArray.reserve(4 * HV_EVS_SUB_HEIGHT * HV_EVS_SUB_WIDTH);
        for (int sub = 0; sub < 4; sub++) {
            uint64_t timestamp = 0;
            uint64_t subframe = 0;
            uint64_t buffer = pixelBufferPtr[0];
            uint64_t raw_timestamp = (buffer >> 24) & 0xFFFFFFFFFF;
            timestamp = raw_timestamp;
            uint64_t header_vec = buffer & 0xFFFFFF;
            if (header_vec != 0xFFFF) {
                std::cerr << "bits process error" << std::endl;
            }
            buffer = pixelBufferPtr[1];
            subframe = (buffer >> 44) & 0xF;
            timestamp /= 200;
            if (timestamp_metadata != nullptr) {
                timestamp_metadata->emplace_back(timestamp, raw_timestamp, subframe, block_index, sub);
            }
            pixelBufferPtr += 2;
            int x = 0, y = 0, x_offset = 0, y_offset = 0;
            switch (subframe) {
            case 0:
                x_offset = 0;
                y_offset = 0;
                break;
            case 1:
                x_offset = 1;
                y_offset = 0;
                break;
            case 2:
                x_offset = 0;
                y_offset = 1;
                break;
            case 3:
                x_offset = 1;
                y_offset = 1;
                break;
            }
            y = y_offset;
            for (int i = 0; i < HV_EVS_SUB_HEIGHT; i++) {
                x = x_offset;
                for (int j = 0; j < HV_EVS_SUB_WIDTH / 32; j++) {
                    buffer = pixelBufferPtr[j];
                    for (int k = 0; k < 64; k += 2) {
                        uint64_t pix = (buffer >> k) & 0x3;
                        if (x >= HV_EVS_WIDTH || y >= HV_EVS_HEIGHT) {
                            x += 2;
                            continue;
                        }
                        if (pix > 0) {
                            pix >>= 1;
                            EventCD event(static_cast<unsigned short>(x), static_cast<unsigned short>(y), static_cast<short>(pix), static_cast<Metavision::timestamp>(timestamp));
                            eventArray.push_back(event);
                        }
                        x += 2;
                    }
                }
                pixelBufferPtr += HV_EVS_SUB_WIDTH / 32;
                y += 2;
            }
            pixelBufferPtr += (HV_SUB_FULL_BYTE_SIZE - HV_SUB_VALID_BYTE_SIZE) / 8;
        }
        return eventArray;
    }
    
    // 写入EVT2文件
    bool writeEVT2File(const std::string& filename, const std::vector<EventCD>& events) {
        std::ofstream file(filename, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "无法创建EVT2文件: " << filename << std::endl;
            return false;
        }
        
        // 写入头部
        auto header_lines = evt2::generateEVT2Header(HV_EVS_WIDTH, HV_EVS_HEIGHT);
        for (const auto& line : header_lines) {
            file << line << "\n";
        }
        
        // 转换事件为EVT2格式
        std::vector<uint8_t> raw_data;
        time_encoder_->reset(0);
        size_t event_count = evt2::convertToEVT2(events, raw_data, *time_encoder_);
        
        // 写入原始数据
        file.write(reinterpret_cast<const char*>(raw_data.data()), raw_data.size());
        
        std::cout << "已写入 " << event_count << " 个事件到EVT2文件: " << filename << std::endl;
        return true;
    }
    
    // 写入时间戳元数据文件
    bool writeTimestampFile(const std::string& filename, const std::vector<TimestampMetadata>& timestamp_data) {
        std::ofstream file(filename);
        if (!file.is_open()) {
            std::cerr << "无法创建时间戳文件: " << filename << std::endl;
            return false;
        }
        
        // 写入CSV格式的头部
        file << "block_index,sub_index,subframe,raw_timestamp,processed_timestamp,timestamp_diff_us\n";
        
        // 写入时间戳数据
        uint64_t prev_timestamp = 0;
        for (size_t i = 0; i < timestamp_data.size(); ++i) {
            const auto& ts = timestamp_data[i];
            
            // 计算与前一个时间戳的差值（微秒）
            uint64_t timestamp_diff = (i > 0) ? (ts.timestamp - prev_timestamp) : 0;
            
            file << ts.block_index << ","
                 << ts.sub_index << ","
                 << ts.subframe << ","
                 << ts.raw_timestamp << ","
                 << ts.timestamp << ","
                 << timestamp_diff << "\n";
            
            prev_timestamp = ts.timestamp;
        }
        
        std::cout << "已写入 " << timestamp_data.size() << " 个时间戳记录到文件: " << filename << std::endl;
        return true;
    }
    
    // 处理整个raw文件
    bool processRawFile(const std::string& filename, const std::string& output_filename = "", const std::string& timestamp_filename = "") {
        std::ifstream file(filename, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "无法打开文件: " << filename << std::endl;
            return false;
        }
        
        // 获取文件大小
        file.seekg(0, std::ios::end);
        size_t file_size = file.tellg();
        file.seekg(0, std::ios::beg);
        
        std::cout << "文件大小: " << file_size << " 字节" << std::endl;
        std::cout << "预计数据块数量: " << file_size / HV_BUF_LEN << std::endl;
        
        // 存储所有事件用于EVT2输出
        std::vector<EventCD> all_events;
        
        // 存储所有时间戳元数据
        std::vector<TimestampMetadata> all_timestamps;
        
        // 分配缓冲区
        std::vector<uint8_t> buffer(HV_BUF_LEN);
        
        size_t total_events = 0;
        size_t block_count = 0;
        auto start_time = std::chrono::high_resolution_clock::now();
        
        while (file.read(reinterpret_cast<char*>(buffer.data()), HV_BUF_LEN)) {
            block_count++;
            // 按照hv_camera.cpp的方式处理数据块：每个偏移量为HV_SUB_FULL_BYTE_SIZE * 4
            for (size_t offset = 0; offset < HV_BUF_LEN; offset += HV_SUB_FULL_BYTE_SIZE * 4) {
                auto events = processEventData(buffer.data() + offset, block_count, &all_timestamps);
                total_events += events.size();
                all_events.insert(all_events.end(), events.begin(), events.end());
            }
            if (block_count % 100 == 0) {
                auto current_time = std::chrono::high_resolution_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - start_time).count();
                size_t total_subframes = block_count * 4;
                std::cout << "已处理 " << block_count << " 个数据块 (" << total_subframes << " 个子帧), 总事件数: " << total_events 
                         << ", 时间戳记录数: " << all_timestamps.size() << " (每个子帧一个时间戳)"
                         << ", 耗时: " << elapsed << "ms" << std::endl;
            }
        }
        
        // 处理最后一个不完整的块（如果有）
        size_t remaining = file.gcount();
        if (remaining > 0) {
            std::cout << "最后一个不完整的数据块大小: " << remaining << " 字节" << std::endl;
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto total_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
        
        std::cout << "\n处理完成!" << std::endl;
        std::cout << "总数据块数: " << block_count << std::endl;
        std::cout << "总子帧数: " << (block_count * 4) << " (每块4个子帧)" << std::endl;
        std::cout << "总事件数: " << total_events << std::endl;
        std::cout << "总时间戳记录数: " << all_timestamps.size() << " (每个子帧一个时间戳)" << std::endl;
        std::cout << "总耗时: " << total_time << "ms" << std::endl;
        std::cout << "平均处理速度: " << (block_count * 1000.0 / total_time) << " 块/秒" << std::endl;
        std::cout << "平均子帧处理速度: " << (block_count * 4 * 1000.0 / total_time) << " 子帧/秒" << std::endl;
        
        // 写入EVT2文件（如果指定输出文件名）
        if (!output_filename.empty()) {
            writeEVT2File(output_filename, all_events);
        }
        
        // 写入时间戳文件（如果指定时间戳文件名）
        if (!timestamp_filename.empty()) {
            writeTimestampFile(timestamp_filename, all_timestamps);
        }
        
        return true;
    }
    
private:
    std::unique_ptr<evt2::EventTimeEncoder> time_encoder_;
    std::vector<std::vector<uint8_t>> buffer_pool_;
    std::vector<std::vector<EventCD>> event_array_pool_;
};

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "用法: " << argv[0] << " <raw_file_path> [output_event_file] [timestamp_file]" << std::endl;
        std::cout << "参数说明:" << std::endl;
        std::cout << "  raw_file_path     - 输入的原始.raw文件路径" << std::endl;
        std::cout << "  output_event_file - 输出的事件文件路径（可选，默认为输入文件名_processed.raw）" << std::endl;
        std::cout << "  timestamp_file    - 输出的时间戳CSV文件路径（可选，默认为输入文件名_timestamps.csv）" << std::endl;
        std::cout << "示例: " << argv[0] << " data.raw events.raw timestamps.csv" << std::endl;
        std::cout << "注意: 输出格式与hv_camera.cpp和hv_camera_record.cpp一致" << std::endl;
        return 1;
    }
    
    std::string input_file = argv[1];
    std::string output_file = "";
    std::string timestamp_file = "";
    
    if (argc >= 3) {
        output_file = argv[2];
    } else {
        // 如果没有指定输出文件，生成默认的事件文件名
        size_t dot_pos = input_file.find_last_of('.');
        if (dot_pos != std::string::npos) {
            output_file = input_file.substr(0, dot_pos) + "_processed.raw";
        } else {
            output_file = input_file + "_processed.raw";
        }
    }
    
    if (argc >= 4) {
        timestamp_file = argv[3];
    } else {
        // 如果没有指定时间戳文件，生成默认的时间戳文件名
        size_t dot_pos = input_file.find_last_of('.');
        if (dot_pos != std::string::npos) {
            timestamp_file = input_file.substr(0, dot_pos) + "_timestamps.csv";
        } else {
            timestamp_file = input_file + "_timestamps.csv";
        }
    }
    
    std::cout << "Raw数据处理器" << std::endl;
    std::cout << "输入文件: " << input_file << std::endl;
    std::cout << "输出事件文件: " << output_file << std::endl;
    std::cout << "输出时间戳文件: " << timestamp_file << std::endl;
    std::cout << "开始处理..." << std::endl;
    
    RawDataProcessor processor;
    bool success = processor.processRawFile(input_file, output_file, timestamp_file);
    
    if (!success) {
        std::cerr << "处理失败!" << std::endl;
        return 1;
    }
    
    return 0;
}

