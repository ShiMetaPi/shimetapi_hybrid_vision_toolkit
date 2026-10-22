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
    /// 头部解析出的 EVS 几何单独访问器（width_/height_）。
    /// 注意：头缺失/未写 width|height 时 readHeader 回退 640×512——调用方需先以
    /// hasHeaderSize() 判断该值是否可信（工具包 EventWriter 产物恒携带真实几何）。
    uint32_t evsWidth() const;
    uint32_t evsHeight() const;
    /// RAW 头是否显式携带 width/height（false = 头缺失/非法，几何值为内部回退）。
    bool hasHeaderSize() const;

    /// 读取并解码全部事件（按头部 ev_version 选 EVT2/EVT3 解码器）。
    size_t readAllEvents(std::vector<EventCD>& events);
    void reset();
private:
    std::ifstream file_;
    RawFormat format_ = RawFormat::Unknown;
    uint32_t  width_ = 0, height_ = 0;
    bool      header_has_size_ = false;   // 头部是否显式携带 width/height
    bool      is_open_ = false;
    std::streampos data_start_ = 0;
    std::vector<uint8_t> read_buffer_;
    bool readHeader();
};

} // namespace Shimeta::io
#endif // SHIMETA_IO_EVENT_READER_H
