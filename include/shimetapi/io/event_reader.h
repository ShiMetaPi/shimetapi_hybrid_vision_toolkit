// Copyright 2026 ShiMetaPi. Licensed under the Apache License, Version 2.0.
// 事件文件读取器（移植自旧 hv::HVEventReader，去除第三方 SDK 依赖，按 RAW 头 ev_version 选 EVT2/EVT3 解码）。
#ifndef SHIMETA_IO_EVENT_READER_H
#define SHIMETA_IO_EVENT_READER_H
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <utility>
#include <vector>
#include <shimetapi/core/event_cd.h>
namespace Shimeta::io {

enum class RawFormat { Evt2, Evt3, Unknown };

class EventReader {
public:
    EventReader();
    ~EventReader();

    bool open(const std::string& filename);
    void close();
    bool isOpen() const;

    RawFormat format() const;
    std::pair<uint32_t, uint32_t> imageSize() const;

    /// 读取并解码全部事件（按头部 ev_version 选 EVT2/EVT3 解码器）。
    size_t readAllEvents(std::vector<EventCD>& events);
    void reset();
private:
    std::ifstream file_;
    RawFormat format_ = RawFormat::Unknown;
    uint32_t  width_ = 0, height_ = 0;
    bool      is_open_ = false;
    std::streampos data_start_ = 0;
    std::vector<uint8_t> read_buffer_;
    bool readHeader();
};

} // namespace Shimeta::io
#endif // SHIMETA_IO_EVENT_READER_H
