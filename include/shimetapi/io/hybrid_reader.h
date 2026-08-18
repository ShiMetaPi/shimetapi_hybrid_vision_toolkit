// Copyright 2026 ShiMetaPi. Licensed under the Apache License, Version 2.0.
// HybridWriter 的读取对偶：读取其产出的混合录像（EVS raw + APS NV12 AVI，含 tsmp）。
// 与 Camera 一样返回原始字节：APS 为 NV12 字节、EVS 为原始包字节；
// 应用自行 cvtColor / codec 解码（工具包保持零 OpenCV 依赖）。
#ifndef SHIMETA_IO_HYBRID_READER_H
#define SHIMETA_IO_HYBRID_READER_H
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include <shimetapi/core/evs_timestamp.h>
#include <shimetapi/core/frame.h>
namespace Shimeta::io {

/// 读取 HybridWriter 产出的混合录像。APS 侧解析 RIFF/AVI（NV12 + tsmp chunk），
/// EVS 侧跳过 EVT3 文本头后按包读取原始字节。两侧可独立使用（对应路径留空即跳过）。
class HybridReader {
public:
    HybridReader();
    ~HybridReader();
    HybridReader(const HybridReader&) = delete;
    HybridReader& operator=(const HybridReader&) = delete;

    /// 打开 EVS / APS 两路文件；任一为空则跳过该侧。两路都需成功打开对应文件。
    bool open(const std::string& evs_path, const std::string& aps_path);
    void close();
    bool isOpen() const;

    uint32_t width() const;         ///< APS 宽
    uint32_t height() const;        ///< APS 高
    double   apsFps() const;        ///< AVI 头帧率（无效时回退 30.0）
    uint32_t apsFrameCount() const; ///< AVI 头声明的总帧数

    /// 顺序读下一帧 APS（NV12 原始字节）。填充 out.aps（自包含 slab）+
    /// out.format=NV12 / out.width / out.height；evs_ts（可选）取该帧传感器时间戳；
    /// out.ts.aps_ts_ns 同步置为 processed_timestamp（纳秒）。返回是否取到。
    bool readApsFrame(Shimeta::Frame& out, Shimeta::EvsTimestamp* evs_ts = nullptr);

    /// 顺序读下一包 EVS 原始字节（已跳过 EVT3 文本头）。填充 out.evs（自包含 slab）。
    /// packet_bytes=0 时默认 1 MiB（apx003 RAW8 单包 = 32768×32）。
    bool readEvsPacket(Shimeta::Frame& out, size_t packet_bytes = 0);

private:
    bool parseAviHeader();
    bool emitPending(Shimeta::Frame& out, Shimeta::EvsTimestamp* evs_ts);

    // APS (AVI)
    std::ifstream aps_file_;
    uint32_t aps_width_ = 0, aps_height_ = 0;
    double   aps_fps_ = 0.0;
    uint64_t aps_total_frames_ = 0;
    uint64_t movi_data_pos_ = 0, movi_end_pos_ = 0, next_aps_index_ = 0;
    std::vector<uint8_t> aps_buf_;        // 当前帧 NV12 字节
    bool     has_pending_ = false;
    Shimeta::EvsTimestamp pending_ts_{};

    // EVS (raw)
    std::ifstream evs_file_;
    bool evs_open_ = false;
};

} // namespace Shimeta::io
#endif // SHIMETA_IO_HYBRID_READER_H
