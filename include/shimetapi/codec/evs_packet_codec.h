// Copyright 2026 ShiMetaPi. Licensed under the Apache License, Version 2.0.
// CF-NRS3 固件 014 新格式包解码：
//   EVS 包流 [PacketHeader 40B][EVT2 payload]（USB bulk 小端，尾短包=包界）
//   APS 帧流 [80B 头][Gray8 640×1024]
// 移植自 CF-NRS3/EVS_PC_QT/usb_camera/{evs_evt2_packet_pc.h, aps_frame_header_pc.h}
// （其本身镜像设备端 packet_protocol.h / aps_frame_header.h，改需两侧同步）。
#ifndef SHIMETA_CODEC_EVS_PACKET_CODEC_H
#define SHIMETA_CODEC_EVS_PACKET_CODEC_H

#include <cstddef>
#include <cstdint>
#include <vector>
#include <shimetapi/core/event_cd.h>

namespace Shimeta {
namespace codec {

// ---- EVS 014 包格式常量（镜像 evspkt::，设备端 packet_protocol.h）----
constexpr uint32_t kEvsMagic          = 0x44565331u; ///< "DVS1" = PACKET_MAGIC_NUMBER
constexpr uint8_t  kEvsVersion        = 0x01;        ///< PACKET_VERSION
constexpr uint8_t  kEvsPacketTypeEvt2 = 0x01;        ///< PACKET_TYPE_EVT2_DATA
constexpr size_t   kEvsHdrLen         = 40;          ///< 包头长度（packed 4+1+1+2+4*8）
constexpr size_t   kEvsSlotCap        = 32836;       ///< bulk 读上限（容纳最大包 40+8192*4+4）

// 关键字段偏移（重同步/防呆用，static_assert 把关真实布局）
constexpr size_t kEvsSeqOff          = 8;  ///< sequence_num 偏移
constexpr size_t kEvsPayloadSizeOff  = 24; ///< payload_size 偏移
constexpr size_t kEvsChecksumOff     = 32; ///< checksum 偏移
constexpr size_t kEvsReservedOff     = 36; ///< reserved 偏移（设备端填 EVS 帧号 fn）

/// @brief EVS 包头（镜像设备端 PacketHeader_t，逐字节一致，主机/设备均小端）。
/// checksum = CRC32 连续覆盖 "checksum 清零的 40B 头 + payload"。
#pragma pack(push, 1)
struct EvsPacketHeader {
    uint32_t magic;
    uint8_t  version;
    uint8_t  packet_type;
    uint16_t flags;
    uint32_t sequence_num;
    uint32_t device_id;
    uint32_t timestamp_sec;
    uint32_t timestamp_usec;
    uint32_t payload_size;
    uint32_t event_count;
    uint32_t checksum;   ///< CRC32（checksum 清零参与，同设备端 write_packet）
    uint32_t reserved;   ///< 设备端填 EVS 帧号 fn（聚帧分组键）
};
#pragma pack(pop)

static_assert(sizeof(EvsPacketHeader) == kEvsHdrLen, "EvsPacketHeader 必须 40B");
static_assert(offsetof(EvsPacketHeader, sequence_num) == kEvsSeqOff, "sequence_num 必须@8");
static_assert(offsetof(EvsPacketHeader, payload_size) == kEvsPayloadSizeOff, "payload_size 必须@24");
static_assert(offsetof(EvsPacketHeader, checksum) == kEvsChecksumOff, "checksum 必须@32");
static_assert(offsetof(EvsPacketHeader, reserved) == kEvsReservedOff, "reserved 必须@36");
static_assert(kEvsSlotCap >= kEvsHdrLen + 8192 * 4 + 4,
              "读上限必须容纳最大包（8192 事件 + 首包 TIME_HIGH 词）");

/// @brief 标准 zlib CRC32（反射 0xEDB88320），与设备端逐位一致。
/// 空输入返回 0；续算：evsPacketCrc32Cont(evsPacketCrc32(A), B) == evsPacketCrc32(A||B)。
uint32_t evsPacketCrc32(const uint8_t* d, size_t n);

/// @brief CRC32 续算（seed 为前段结果，0 即首段）。设备端分两段写头+负载时同法拼接。
uint32_t evsPacketCrc32Cont(uint32_t seed, const uint8_t* d, size_t n);

/// @brief 解析一包（buf 指向包头，无长度前缀；len = 40 + payload_size）。
/// 校验 magic/version/packet_type、长度与包界一致、CRC32。成功时 *payload 指向
/// EVT2 负载（payload_size 可为 0——空帧包合法）。
bool parseEvsPacket(const uint8_t* buf, size_t len, EvsPacketHeader& h,
                    const uint8_t** payload = nullptr);

/// @brief 快速探测（重同步扫 magic / 003 vs 014 分派用，只查 4 字节魔数）。
bool evsPacketLooksLikeHeader(const uint8_t* p);

// ---- APS 014 帧头常量（镜像 aps014::，设备端 aps_frame_header.h）----
constexpr uint16_t kAps014Magic       = 0xCF14;     ///< 帧头魔数
constexpr uint8_t  kAps014FormatGray8 = 0;          ///< Gray8 像素格式（014）
constexpr uint8_t  kAps014HdrVer      = 1;          ///< 帧头版本
constexpr size_t   kAps014HdrLen      = 80;         ///< 帧头长度
constexpr size_t   kAps014YPayloadLen = 640 * 1024; ///< Gray8 载荷 655,360B
constexpr size_t   kAps014FrameLen    = kAps014HdrLen + kAps014YPayloadLen; ///< 655,440B

/// @brief APS 014 帧头元数据（镜像 APSFrameMeta，字段语义逐项一致）。
struct Aps014FrameMeta {
    uint32_t ts_sec = 0, ts_usec = 0;        ///< 帧起始墙钟（秒/微秒）
    uint64_t t_trigger = 0;                  ///< 外触发时刻（µs）
    uint32_t exposure_us = 0;                ///< 曝光时间（µs）
    uint32_t analog_gain_x256 = 0, digital_gain_x256 = 0; ///< 增益（×256 定点）
    uint32_t gray_brightness = 0;            ///< 灰度统计
    uint16_t gamma_index = 0;                ///< gamma 曲线索引
    uint32_t frame_counter = 0, ext_trigger_count = 0;    ///< 帧计数 / 外触发累计
    uint32_t width = 0, height = 0, roi_x = 0, roi_y = 0; ///< 几何
    int16_t  sensor_temp_x01 = 0;            ///< 传感器温度（×0.1°C）
    uint16_t status_flags = 0;               ///< 状态位
    bool     valid = false;                  ///< 五重防呆 + 几何自洽全通过
};

/// @brief CRC16-CCITT（初值 0xFFFF，多项式 0x1021），覆盖帧头前 70 字节。
uint16_t aps014Crc16(const uint8_t* p, size_t n);

/// @brief 解析 80B APS 帧头（buf 须至少 80B）。五重防呆：magic / CRC16 / format /
/// hdr_version / payload_len，外加宽高与载荷几何自洽（Gray8: W×H==payload_len）。
/// expect_payload_len / expect_format 按传感器参数表传入，缺省 014 Gray8。
bool parseAps014Header(const uint8_t* buf, Aps014FrameMeta& meta,
                       uint32_t expect_payload_len = static_cast<uint32_t>(kAps014YPayloadLen),
                       uint8_t  expect_format      = kAps014FormatGray8);

/// @brief 014 EVS 包聚帧解码器（状态化，单线程使用）。
/// 按包头 reserved@36（设备端 EVS 帧号 fn）分组：同 fn 包的事件累计，fn 变化时
/// 把上一帧全部事件一次性输出（设备端同帧包连续发送，不跨帧交错）。
/// 事件 t = 包头 timestamp_sec × 1e6 + timestamp_usec（µs，无校准）；同帧各包
/// 同 ts。EVT2 词解码内联（复用 evt2_codec.h 的词布局）：CD_ON/CD_OFF 产出事件，
/// TIME_HIGH/EXT_TRIGGER 词跳过。空帧包（payload_size=0）合法，fn 变化时仍输出。
class Evs014PacketDecoder {
public:
    /// @brief 喂入一包。返回本次输出到 out 的事件数（= 帧号变化时冲刷的上一帧
    /// 事件数；同帧累计/坏包返回 0）。坏包（parse 失败）不改内部状态。
    size_t Decode(const uint8_t* pkt, size_t len, std::vector<EventCD>& out);

    /// @brief 冲刷当前累计帧（流收尾用），返回输出事件数。
    size_t Flush(std::vector<EventCD>& out);

    /// @brief 重置聚帧状态。
    void Reset();

    /// @brief 当前帧号（= 最近包的 reserved；无帧时 0）
    uint32_t currentFrameNum() const { return cur_fn_; }

    /// @brief 是否有累计中的帧
    bool hasCurrentFrame() const { return acc_valid_; }

private:
    std::vector<EventCD> acc_;      ///< 聚合中的帧事件
    uint32_t cur_fn_ = 0;           ///< 当前帧号
    bool acc_valid_ = false;        ///< 是否有累计中的帧
};

} // namespace codec
} // namespace Shimeta

#endif // SHIMETA_CODEC_EVS_PACKET_CODEC_H
