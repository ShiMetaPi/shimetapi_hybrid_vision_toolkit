// Copyright 2026 ShiMetaPi. Licensed under the Apache License, Version 2.0.
// 事件文件写入器（移植自旧 hv::HVEventWriter，去除第三方 SDK 依赖，支持 EVT2/EVT3 + 原始透传）。
#ifndef SHIMETA_IO_EVENT_WRITER_H
#define SHIMETA_IO_EVENT_WRITER_H
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>
#include <shimetapi/core/event_cd.h>
#include <shimetapi/io/event_reader.h>   // RawFormat
namespace Shimeta::io {

class EventWriter {
public:
    EventWriter();
    ~EventWriter();

    /// 打开文件并写头部。fmt 决定头部的 ev_version（默认 EVT3）。
    bool open(const std::string& filename, uint32_t width, uint32_t height,
              RawFormat fmt = RawFormat::Evt3, uint64_t start_timestamp = 0);
    void close();
    bool isOpen() const;

    /// 原始字节透传写入（Frame.evs 直写，不再编码）。
    size_t writeRaw(const uint8_t* data, size_t len);
    /// 用 Evt2Encoder 编码事件后写入（EVT2 录制路径）。
    size_t writeEvents(const std::vector<EventCD>& events);

    void flush();
    uint64_t writtenEventCount() const;
private:
    std::ofstream file_;
    RawFormat  fmt_ = RawFormat::Evt3;
    uint32_t   width_ = 0, height_ = 0;
    bool       is_open_ = false;
    uint64_t   event_count_ = 0;
    std::vector<uint8_t> write_buffer_;
    void writeHeader();
    void writeRawData(const uint8_t* data, size_t len);
    void flushBuffer();
};

} // namespace Shimeta::io
#endif // SHIMETA_IO_EVENT_WRITER_H
