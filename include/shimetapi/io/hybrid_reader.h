// Copyright 2026 ShiMetaPi. Licensed under the Apache License, Version 2.0.
// HybridWriter 的读取对偶：读取其产出的混合录像（EVS raw + APS AVI，含 tsmp）。
// 与 Camera 一样返回原始字节：APS 为 NV12 或 Gray8 字节（按 AVI 头检测，见
// apsFormat()）、EVS 为原始包字节；应用自行 cvtColor / codec 解码（零 OpenCV 依赖）。
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
#include <shimetapi/core/pixel_format.h>
namespace Shimeta::io {

/// 读取 HybridWriter 产出的混合录像。APS 侧解析 RIFF/AVI（NV12=003 / Gray8=014 +
/// tsmp chunk，像素格式按 strf biBitCount/biCompression 检测，缺省时按首帧载荷
/// 长度启发 w*h=Gray8、w*h*3/2=NV12），EVS 侧跳过 EVT3 文本头后按包读取原始字节。
/// 两侧可独立使用（对应路径留空即跳过）。
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
    PixelFormat apsFormat() const;  ///< APS 像素格式（NV12=003 / Gray8=014）

    /// 顺序读下一帧 APS（原始字节，长度=该格式帧长：NV12=w*h*3/2、Gray8=w*h）。
    /// 填充 out.aps（自包含 slab）+ out.format / out.width / out.height；
    /// evs_ts（可选）取该帧传感器时间戳；out.ts.aps_ts_ns 同步置为
    /// processed_timestamp（纳秒）。返回是否取到。
    bool readApsFrame(Shimeta::Frame& out, Shimeta::EvsTimestamp* evs_ts = nullptr);

    /// 顺序读下一包 EVS 原始字节（已跳过 EVT3 文本头）。填充 out.evs（自包含 slab）。
    /// packet_bytes=0 时按 EVT3 头几何自适应：
    ///   - 1280×1024 = 旧 014 → 固定 164,864B（恰好一帧；014 帧边界被切断会走错
    ///     解码路径——014 子帧魔数与 003 低 24bit 掩码同值，切断后仍"通过"校验）；
    ///   - 640×1024 = 新 014 → DVS1 变长包流逐包切包：每次恰返回一个完整包
    ///     （40B 包头 + 包内 payload_size 字节，跨读残余缓冲拼接）；
    ///   - 其余（003，解码长度无关）→ 1 MiB。
    /// 显式 packet_bytes 优先于自适应（原始定长读，API 兼容）。
    bool readEvsPacket(Shimeta::Frame& out, size_t packet_bytes = 0);

private:
    bool parseAviHeader();
    bool emitPending(Shimeta::Frame& out, Shimeta::EvsTimestamp* evs_ts);
    bool readEvs014Packet(Shimeta::Frame& out);   // 新 014：按包头 payload_size 切变长包

    // APS (AVI)
    std::ifstream aps_file_;
    uint32_t aps_width_ = 0, aps_height_ = 0;
    PixelFormat aps_format_ = PixelFormat::NV12;   // strf/载荷长度检测（缺省 NV12）
    size_t   aps_frame_bytes_ = 0;                 // 该格式帧长（NV12=w*h*3/2、Gray8=w*h）
    double   aps_fps_ = 0.0;
    uint64_t aps_total_frames_ = 0;
    uint64_t movi_data_pos_ = 0, movi_end_pos_ = 0, next_aps_index_ = 0;
    std::vector<uint8_t> aps_buf_;        // 当前帧 NV12 字节
    bool     has_pending_ = false;
    Shimeta::EvsTimestamp pending_ts_{};

    // EVS (raw)
    std::ifstream evs_file_;
    bool evs_open_ = false;
    size_t evs_packet_bytes_ = 0;   // open() 按 EVT3 头几何自适应；0 = 1 MiB 默认
    // 新 014（头几何 640×1024）变长包切包状态：evs_pkt_buf_ 为跨读残余缓冲，
    // evs_pkt_pos_ 为其中未消费的偏移（每次文件读 ≤ kEvsSlotCap 字节追加到尾部）。
    bool evs_pkt_mode_ = false;
    std::vector<uint8_t> evs_pkt_buf_;
    size_t evs_pkt_pos_ = 0;
};

} // namespace Shimeta::io
#endif // SHIMETA_IO_HYBRID_READER_H
