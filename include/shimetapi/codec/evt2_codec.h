// Copyright 2026 ShiMetaPi. Licensed under the Apache License, Version 2.0.
// EVT2 编解码（移植自旧 hv::evt2，去除第三方 SDK 依赖）。
#ifndef SHIMETA_CODEC_EVT2_CODEC_H
#define SHIMETA_CODEC_EVT2_CODEC_H

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <shimetapi/core/event_cd.h>

namespace Shimeta {
namespace codec {

/// @brief Event types for EVT2 format
enum class EventTypes : uint8_t {
    CD_OFF        = 0x00, ///< OFF CD event, decrease in illumination (polarity '0')
    CD_ON         = 0x01, ///< ON CD event, increase in illumination (polarity '1')
    EVT_TIME_HIGH = 0x08, ///< Encodes the higher portion of the timebase (bits 33..6)
    EXT_TRIGGER   = 0x0A, ///< External trigger output
};

/// @brief EVT2 raw events are 32-bit words
struct RawEvent {
    unsigned int pad : 28;  ///< Padding
    unsigned int type : 4;  ///< Event type
};

/// @brief Time High event structure
struct RawEventTime {
    unsigned int timestamp : 28; ///< Most significant bits of the event timestamp (bits 33..6)
    unsigned int type : 4;       ///< Event type: EventTypes::EVT_TIME_HIGH
};

/// @brief CD event structure
struct RawEventCD {
    unsigned int y : 11;        ///< Pixel Y coordinate
    unsigned int x : 11;        ///< Pixel X coordinate
    unsigned int timestamp : 6; ///< Least significant bits of the event timestamp (bits 5..0)
    unsigned int type : 4;      ///< Event type: EventTypes::CD_OFF or EventTypes::CD_ON
};

/// @brief External trigger event structure
struct RawEventExtTrigger {
    unsigned int value : 1;     ///< Trigger current value (edge polarity)
    unsigned int unused2 : 7;   ///< Unused bits
    unsigned int id : 5;        ///< Trigger channel ID
    unsigned int unused1 : 9;   ///< Unused bits
    unsigned int timestamp : 6; ///< Least significant bits of the event timestamp (bits 5..0)
    unsigned int type : 4;      ///< Event type: EventTypes::EXT_TRIGGER
};

using Timestamp = uint64_t; ///< Type for timestamp, in microseconds

/// @brief EVT2 file header structure
struct Evt2Header {
    std::string format_line;    ///< Format line with sensor geometry
    std::string integrator;     ///< Integrator name
    std::string date;          ///< Creation date
    uint32_t width;            ///< Sensor width
    uint32_t height;           ///< Sensor height
    uint64_t start_timestamp;  ///< Start timestamp in microseconds
};

/// @brief CD event encoder for EVT2 format (内部辅助)
class EventCDEncoder {
public:
    unsigned short x;  ///< X coordinate
    unsigned short y;  ///< Y coordinate
    short p;          ///< Polarity (0 or 1)
    Timestamp t;      ///< Timestamp in microseconds

    /// @brief Encodes CD event to EVT2 format
    void encode(RawEvent* raw_event);

    /// @brief Sets event data
    void setEvent(unsigned short x_coord, unsigned short y_coord, short polarity, Timestamp timestamp);
};

/// @brief Time High encoder for EVT2 format (内部辅助)
class EventTimeEncoder {
public:
    /// @brief Constructor
    explicit EventTimeEncoder(Timestamp base);

    /// @brief Encodes Time High event
    void encode(RawEvent* raw_event);

    /// @brief Gets next time high value
    Timestamp getNextTimeHigh() const { return th; }

    /// @brief Resets the time encoder to a new base timestamp
    void reset(Timestamp base = 0);

private:
    Timestamp th;  ///< Next Time High to encode

    static constexpr char N_LOWER_BITS_TH = 6;
    static constexpr unsigned int REDUNDANCY_FACTOR = 4;
    static constexpr Timestamp TH_STEP = (1ull << N_LOWER_BITS_TH);
    static constexpr Timestamp TH_NEXT_STEP = TH_STEP / REDUNDANCY_FACTOR;
};

/// @brief EVT2 decoder（有状态：跨包维护 time-base / 翻转计数）
class Evt2Decoder {
public:
    Evt2Decoder();

    /// @brief 解码 32-bit word 流，把 CD 事件追加到 out。返回本调用新增 CD 事件数。
    size_t Decode(const uint8_t* buffer, size_t buffer_size, std::vector<EventCD>& out);

    /// @brief 重置解码器状态
    void Reset();

    /// @brief 当前 time-base（仅诊断用）
    Timestamp getCurrentTimeBase() const { return current_time_base_; }

private:
    Timestamp current_time_base_;        ///< Current time base
    bool first_time_base_set_;          ///< Whether first time base is set
    unsigned int n_time_high_loop_;     ///< Counter of time high loops

    void processEvent(const RawEvent* raw_event, std::vector<EventCD>& out);
};

/// @brief EVT2 编码器（事件 → 原始字节）
class Evt2Encoder {
public:
    Evt2Encoder();
    /// @brief 把 count 个事件编码进 out（含必要的 TimeHigh 冗余字）。
    void Encode(const EventCD* events, size_t count, std::vector<uint8_t>& out);
    /// @brief 重置编码器（下一次 Encode 从 time-base 0 起）
    void Reset();
private:
    EventTimeEncoder time_encoder_;
};

/// @brief Utility functions for EVT2 format
namespace utils {
    /// @brief Parses EVT2 header from file stream
    bool parseEvt2Header(const std::vector<std::string>& header_lines, Evt2Header& header);

    /// @brief Generates EVT2 header lines
    std::vector<std::string> generateEvt2Header(const Evt2Header& header);
    std::vector<std::string> generateEvt2Header(uint32_t width, uint32_t height,
                                               const std::string& integrator = "Shimeta");

    /// @brief Converts EventCD vector to EVT2 raw bytes via a time encoder.
    size_t convertToEvt2(const std::vector<EventCD>& events,
                         std::vector<uint8_t>& raw_data,
                         EventTimeEncoder& time_encoder);
}

} // namespace codec
} // namespace Shimeta

#endif // SHIMETA_CODEC_EVT2_CODEC_H
