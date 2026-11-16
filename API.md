# HV Toolkit v2.0 C++ API 文档

**中文** | [English](API_EN.md)

本文档描述 HV Toolkit v2.0 公有 API（头文件位于 `include/shimetapi/`）。所有符号位于 `Shimeta::` 命名空间下；零第三方事件 SDK 依赖。

---

## 目录

- [core：核心类型](#core核心类型)
- [hv：Camera / DeviceConfig / Frame](#hvcamera--deviceconfig--frame)
- [codec：EVT2/EVT3 编解码（+ MIPI RAW8）](#codecevt2evt3-编解码)
- [io：EventReader / EventWriter（+ HybridWriter / HybridReader）](#ioeventreader--eventwriter)
- [Python 绑定（hv_toolkit）](#python-绑定hv_toolkit)

---

## core：核心类型

头文件：`<shimetapi/core/*.h>`

### `Shimeta::EventCD`（`core/event_cd.h`）

自有事件类型（POD），字段语义与业界常见事件结构一一对应。

```cpp
struct EventCD {
    uint16_t x;        // 像素 X 坐标
    uint16_t y;        // 像素 Y 坐标
    int64_t  t;        // 时间戳（微秒）
    bool     polarity; // 1 = CD_ON, 0 = CD_OFF
};
```

### `Shimeta::Status`（`core/status.h`）

```cpp
enum class Status : int32_t {
    Ok = 0, ErrDeviceNotFound = -1, ErrPermissionDenied = -2,
    ErrUsbTransfer = -3, ErrV4l2Ioctl = -4, ErrNetworkTimeout = -5,
    ErrInvalidParam = -6, ErrBufferFull = -7, ErrDecodeFailure = -8,
    ErrUnsupportedFormat = -9,
};
const char* statusToString(Status s);
```

### `Shimeta::BufferView` / `BufferPool`（`core/buffer_pool.h`）

```cpp
struct BufferView {
    const uint8_t* data = nullptr;
    size_t         size = 0;
};
// 固定大小 slab 池：acquire() 返回 shared_ptr<uint8_t[]>，
// 最后一个引用释放时 slab 自动归还池。
class BufferPool {
public:
    BufferPool(size_t slab_size, size_t slab_count);
    std::shared_ptr<uint8_t[]> acquire();
    size_t slab_size() const;
    size_t capacity() const;
    size_t available() const;
};
```

### `Shimeta::PixelFormat`（`core/pixel_format.h`）

```cpp
enum class PixelFormat : uint8_t { BayerRG8 = 0, RGB888 = 1, Gray8 = 2, RAW8 = 3, RAW10 = 4, NV12 = 5 };
```

> `NV12` 为 MIPI HVS 后端 APS 帧经 ISP→PYM 后的 packed YUV 格式（见 `Backend::MipiHvs`）。

### `Shimeta::TimestampInfo`（`core/timestamp.h`）

```cpp
struct TimestampInfo {
    int64_t evs_ts_ns  = 0;  // EVS 事件参考时间戳（纳秒）
    int64_t aps_ts_ns  = 0;  // APS 帧时间戳（对齐句柄；003=尾时间戳 ns、014=80B 帧头墙钟 ns，0=无效）
    bool    ptp_locked = false; // Ethernet 后端 PTP 是否锁定
};
```

> `aps_ts_ns`（USB 后端，2026-10+）：APX003 = APS 尾时间戳(µs)×1000；APX014 = 80B 帧头墙钟（`sec×1e9+usec×1000`，ns），与事件 t（包头墙钟 µs，`sec×1e6+usec`）同源同域，可直接配对（无校准环节）；0 = 无效（侧信道超时）。参见示例 `samples/cpp/ts_sync`。

### `Shimeta::EvsTimestamp`（`core/evs_timestamp.h`）

EVS 传感器内部时间戳（从 apx003 MIPI RAW8 子帧头提取），用于 `HybridWriter` / `HybridReader` 的 tsmp chunk。由 `Shimeta::codec::extractEvsTimestamp()` 提取。

```cpp
struct EvsTimestamp {
    uint64_t raw_timestamp = 0;         // 传感器 45-bit 原始时间戳
    uint64_t processed_timestamp = 0;   // raw_timestamp / 200（微秒）
    bool     valid = false;
};
```

> **仅适用于 apx003 RAW8 子帧流**。014（CF-NRS3 新格式）不再有子帧头时间戳提取——EVS 时间戳直接来自 40B 包头（`timestamp_sec×1e6+timestamp_usec`，µs）、APS 时间戳来自 80B 帧头（见 codec 节 `evs_packet_codec.h`）。

---

## hv：Camera / DeviceConfig / Frame

### `Shimeta::hv::Backend` / `EventFormat`（`hv/device_config.h`、`hv/event_format.h`）

```cpp
enum class Backend    { Auto, Usb, Mipi, MipiHvs, Ethernet };
enum class EventFormat { Evt2, Evt3 };
```

| Backend | 说明 |
| --- | --- |
| `Auto` | 自动选择（按 `DeviceConfig` 字段推断）。 |
| `Usb` | libusb 后端（USB 相机）。`Init` 时按 APS 首帧长度自动识别传感器档位（APX003 / APX014），读长与图像尺寸随之切换。 |
| `Mipi` | MIPI 后端（EVS-only）。 |
| `MipiHvs` | MIPI HVS 双 VC 后端：VC0 传 EVS 事件，VC1 传 APS 帧（→ISP→PYM NV12）。 |
| `Ethernet` | 以太网后端（POSIX sockets）。 |

### `Shimeta::hv::DeviceConfig`（`hv/device_config.h`）

```cpp
struct DeviceConfig {
    Backend     backend      = Backend::Auto;
    std::string device_node;                 // MIPI: "/dev/video0"
    std::string ip;                          // Ethernet
    uint16_t    data_port    = 8000;
    uint16_t    ctrl_port    = 8001;
    EventFormat event_fmt    = EventFormat::Evt3;
    int         buffer_count = 8;
    uint16_t    vendor_id = 0, product_id = 0;   // USB VID/PID
    enum class QueuePolicy { DropOldest, Block };
    QueuePolicy queue_policy = QueuePolicy::DropOldest;
    int         event_urbs   = 4;             // USB 事件端点在途 URB 数
    uint16_t    evs_fps      = 0;             // MIPI：EVS 帧率档（0=默认 240；可选 120/240/300/500/750/1000，Init 时选传感器配置，非档位值报错）。USB/Ethernet 运行时改帧率用 Camera::SetFrameRate
    int         sensor_index = 0;             // MIPI 传感器索引
    uint8_t     i2c_bus      = 1;             // MIPI 安全芯片认证 I2C 总线
    uint16_t    listen_port = 8888;           // Ethernet: TCP 监听端口
    std::string bind_ip;                      // Ethernet: 本地绑定 IP（空=INADDR_ANY）
};
```

### `Shimeta::hv::Camera`（`hv/camera.h`）

统一采集 API；同一套接口覆盖 USB / MIPI / Ethernet 三后端。支持同步拉取（`GetFrame`）与异步回调（Frame/Event/Image 三选一或多）。

```cpp
namespace Shimeta::hv {
class Camera {
public:
    Camera();
    ~Camera();
    Camera(const Camera&) = delete;
    Camera& operator=(const Camera&) = delete;

    bool Init(const DeviceConfig& cfg);
    bool StartStream();
    void StopStream();
    void Destroy();

    bool GetFrame(Frame& frame, int timeout_ms = 1000);

    using FrameCallback = std::function<void(const Frame&)>;
    using EventCallback = std::function<void(const EventPacket&)>;
    using ImageCallback = std::function<void(const ImageData&)>;
    void SetFrameCallback(FrameCallback cb);
    void SetEventCallback(EventCallback cb);
    void SetImageCallback(ImageCallback cb);

    bool SetExposure(int value);
    bool SetFrameRate(unsigned fps);
    bool GetFrameRate(unsigned& fps);
    bool SyncClock();
};
} // namespace Shimeta::hv
```

| 方法 | 说明 |
| --- | --- |
| `Init(cfg)` | 按 `DeviceConfig` 初始化后端（不阻塞打开硬件，部分后端在 `StartStream` 才连接）。 |
| `StartStream()` | 启动采集线程；返回是否成功连上设备。 |
| `StopStream()` | 停止采集并 join 线程。 |
| `Destroy()` | 释放后端资源。 |
| `GetFrame(frame, timeout_ms)` | 同步拉取一帧组合数据（事件 + APS），返回是否在超时内取到。 |
| `SetFrameCallback` / `SetEventCallback` / `SetImageCallback` | 注册异步回调；回调仅在派发线程串行触发，采集线程不回调。 |
| `SetExposure(value)` | 设置 APS 曝光。 |
| `SetFrameRate(fps)` | 设置 EVS 事件帧率（当前支持 USB / Ethernet 后端）。 |
| `GetFrameRate(fps)` | 读取当前 EVS 事件帧率。 |
| `SyncClock()` | 时钟同步（Ethernet PTP mode 0 等）。 |

### `Shimeta::Frame`（`core/frame.h`）

统一帧：`aps`/`evs` 为池内存的只读视图，`*_owner` 持有 slab 引用以保证视图在 Frame 存活期间有效（零拷贝、池托管生命周期）。

```cpp
struct Frame {
    BufferView    aps{};
    BufferView    evs{};
    TimestampInfo ts{};
    int           width{0};
    int           height{0};
    int           frame_id{0};
    PixelFormat   format{};
    std::shared_ptr<uint8_t[]> aps_owner{};
    std::shared_ptr<uint8_t[]> evs_owner{};
};
```

> `Frame.evs` 是 HAL 未解码的原始事件字节（EVT2/EVT3，由 `event_fmt` 决定）；需用对应 codec 解码。

### `Shimeta::hv::EventPacket`（`hv/event_packet.h`）/ `ImageData`（`hv/image_data.h`）

```cpp
namespace Shimeta::hv {
struct EventPacket {
    BufferView data{};       // 一包事件原始字节
    int64_t    t_begin_ns = 0;
    int64_t    t_end_ns   = 0;
};
struct ImageData {
    BufferView    pixels{};
    int           width = 0, height = 0;
    PixelFormat   format{};
    TimestampInfo ts{};
};
}
```

---

## codec：EVT2/EVT3 编解码

头文件：`<shimetapi/codec/evt2_codec.h>`、`<shimetapi/codec/evt3_codec.h>`。命名空间 `Shimeta::codec`。`Encoder`/`Decoder` 均为**有状态**（跨包维护时间基准/翻转计数），多包流请复用同一实例，新流前调用 `Reset()`。

### EVT3（16-bit word 流）

```cpp
class Evt3Encoder {
public:
    Evt3Encoder();
    void Encode(const EventCD* events, size_t count, std::vector<uint8_t>& out);
    void Reset();
};
class Evt3Decoder {
public:
    Evt3Decoder();
    // len 必须为 2 的倍数；返回本调用解码事件数。
    size_t Decode(const uint8_t* buf, size_t len, std::vector<EventCD>& out);
    void Reset();
};
```

### EVT2（32-bit word 流）

```cpp
class Evt2Encoder {
public:
    Evt2Encoder();
    void Encode(const EventCD* events, size_t count, std::vector<uint8_t>& out);
    void Reset();
};
class Evt2Decoder {
public:
    Evt2Decoder();
    size_t Decode(const uint8_t* buffer, size_t buffer_size, std::vector<EventCD>& out);
    void Reset();
};
```

**典型用法**（解码 `Frame.evs`）：

```cpp
Shimeta::codec::Evt3Decoder dec;  // 或 Evt2Decoder，取决于 cfg.event_fmt
std::vector<Shimeta::EventCD> events;
dec.Decode(frame.evs.data, frame.evs.size, events);
```

### MIPI RAW8（apx003 子帧流）

头文件：`<shimetapi/codec/mipi_raw8_codec.h>`。命名空间 `Shimeta::codec`。apx003 HVS RAW8 子帧流解码（clean-room，无第三方 SDK 依赖）。**无状态**：chunk 为 32,768 整数倍字节，事件 `t = ts/200`（微秒，原语义），无跨包复用；`Decode` 非线程安全——按流序单线程调用（与既有用法一致）。014 不再走本解码器（见下节 [EVS 014 包流](#evs-014-包流cf-nrs3-新格式)）。

```cpp
struct MipiRaw8Layout {
    static constexpr int    kSubWidth       = 384;
    static constexpr int    kSubHeight      = 304;
    static constexpr int    kEvsWidth       = 768;
    static constexpr int    kEvsHeight      = 608;
    static constexpr int    kSubFrameNum    = 4;      // 每帧 4 空间子帧
    static constexpr int    kRawMergeNum    = 8;      // 每个 MIPI 包 8 帧
    static constexpr int    kTotalSubframes = kSubFrameNum * kRawMergeNum; // 32
    static constexpr size_t kSubframeBytes  = 32768;  // HV_SUB_FULL_BYTE_SIZE
    static constexpr uint32_t kHeaderMask   = 0x0000FFFFu;
};

/// 传感器档位（USB 后端 Init 时按 APS 首帧长度探测）。
enum class SensorModel { Apx003, Apx014 };

inline bool probeSensorByApsLen(size_t aps_transferred, SensorModel& out);
// APS 首帧传输长度判档：700,424→Apx003（NV12+8B 尾）；655,440→Apx014
//（CF-NRS3 新格式：80B 头 + Gray8 640×1024）；未知长度返回 false。

class MipiRaw8Decoder {
public:
    MipiRaw8Decoder() = default;
    // 解码 data[0..len)：按 32,768B 子帧切分。
    // subframe_count<=0 → 自动档（按 buffer 实际长度解全部子帧，适配任意帧率档：
    // 各档整包子帧数不同，120fps=16 … 1000fps=128）；显式传值可只解前 N 个子帧。
    size_t Decode(const uint8_t* data, size_t len, std::vector<EventCD>& out,
                  int subframe_count = 0);
};
```

> MIPI HVS 后端（`Backend::MipiHvs`）的 `Frame.evs` 为 apx003 RAW8 子帧流，需用 `MipiRaw8Decoder`（而非 `Evt2/Evt3Decoder`）解码。APX003 帧率档（`DeviceConfig.evs_fps`）与整包子帧数对应：120fps=16、240fps=32、300fps=40、500fps=64、750fps=100、1000fps=128；`Decode` 自动档按数据长度适配，无需手传。

```cpp
// 从 apx003 RAW8 子帧头提取传感器时间戳（45-bit / 200 → 微秒）。
// 仅适用于 003 子帧流；014 时间戳在包头/帧头内（见下节）。
Shimeta::EvsTimestamp extractEvsTimestamp(const uint8_t* data, size_t len);
```

### EVS 014 包流（CF-NRS3 新格式）

头文件：`<shimetapi/codec/evs_packet_codec.h>`。命名空间 `Shimeta::codec`。014（CF-NRS3 固件）的 USB 数据格式：

- **EVS 包流**：`[PacketHeader 40B][EVT2 payload]` 变长包，USB bulk 小端传输，尾短包 = 包界（bulk 读长恒为槽容量 `kEvsSlotCap` = 32,836B，容纳最大包 40 + 8192×4 + 4）；
- **APS 帧流**：`[80B 头][Gray8 640×1024]`，定长 655,440B/帧。

**包解析与校验**（`parseEvsPacket`）：校验 magic（"DVS1"）/ version / packet_type、长度与包界一致、CRC32（标准 zlib 反射 CRC32，连续覆盖"checksum 清零的 40B 头 + payload"，支持续算）；成功时输出 EVT2 负载指针（`payload_size` 可为 0——空帧包合法）。`evsPacketLooksLikeHeader` 只查 4 字节魔数，用于流重同步与 003/014 内容分派。

```cpp
struct EvsPacketHeader {           // packed 40B，主机/设备均小端，逐字节镜像设备端
    uint32_t magic;                // "DVS1"（0x44565331）
    uint8_t  version;              // = 1
    uint8_t  packet_type;          // = 1（EVT2 数据）
    uint16_t flags;
    uint32_t sequence_num;         // @8
    uint32_t device_id;
    uint32_t timestamp_sec;        // 包时间戳（秒）
    uint32_t timestamp_usec;       // 包时间戳（微秒）
    uint32_t payload_size;         // @24，EVT2 负载字节数
    uint32_t event_count;
    uint32_t checksum;             // @32，CRC32（checksum 清零参与）
    uint32_t reserved;             // @36，设备端填 EVS 帧号 fn（聚帧分组键）
};

bool parseEvsPacket(const uint8_t* buf, size_t len, EvsPacketHeader& h,
                    const uint8_t** payload = nullptr);
bool evsPacketLooksLikeHeader(const uint8_t* p);   // 4B 魔数快速探测
uint32_t evsPacketCrc32(const uint8_t* d, size_t n);
uint32_t evsPacketCrc32Cont(uint32_t seed, const uint8_t* d, size_t n);  // CRC 续算
```

**聚帧解码器 `Evs014PacketDecoder`**（状态化，单线程使用）：按包头 `reserved@36`（设备端 EVS 帧号 fn）**按帧号聚合**——同 fn 包的事件累计，fn 变化时把上一帧全部事件一次性输出（设备端同帧包连续发送，不跨帧交错）。**时间戳已是微秒、无自校准环节**：事件 `t = timestamp_sec × 1e6 + timestamp_usec`（同帧各包同 ts）。EVT2 词解码内联（复用 `evt2_codec.h` 的词布局）：CD_ON/CD_OFF 词产出事件，TIME_HIGH/EXT_TRIGGER 词跳过。空帧包（`payload_size=0`）合法，fn 变化时仍输出；坏包（parse 失败）不改内部状态。

```cpp
class Evs014PacketDecoder {
public:
    // 喂入一包：返回本次输出到 out 的事件数（= 帧号变化时冲刷的上一帧事件数；
    // 同帧累计 / 坏包返回 0）。
    size_t Decode(const uint8_t* pkt, size_t len, std::vector<EventCD>& out);
    size_t Flush(std::vector<EventCD>& out);  // 冲刷当前累计帧（流收尾用）
    void   Reset();                           // 重置聚帧状态
    uint32_t currentFrameNum() const;         // 当前帧号（= 最近包的 reserved；无帧时 0）
    bool     hasCurrentFrame() const;         // 是否有累计中的帧
};
```

**典型用法**（USB 回调解码 `Frame.evs`，节选自 `samples/cpp/ts_sync`）：

```cpp
Shimeta::codec::Evs014PacketDecoder dec;   // 跨包有状态：回调线程内单实例持有
std::vector<Shimeta::EventCD> events;
dec.Decode(frame.evs.data, frame.evs.size, events);   // 帧号变化时输出上一帧事件
```

**APS 帧头解析**（`parseAps014Header`）：80B 头五重防呆（magic 0xCF14 / CRC16-CCITT 覆盖前 70B / format / hdr_version / payload_len），外加宽高与载荷几何自洽（Gray8：W×H == payload_len）；`Aps014FrameMeta` 回填曝光/增益/几何/温度等元数据，`ts_sec/ts_usec` 为帧起始墙钟。

```cpp
struct Aps014FrameMeta {
    uint32_t ts_sec = 0, ts_usec = 0;        // 帧起始墙钟（秒/微秒）
    uint64_t t_trigger = 0;                  // 外触发时刻（µs）
    uint32_t exposure_us = 0;                // 曝光时间（µs）
    // …增益 / 灰度统计 / gamma / 帧计数 / 外触发计数 / 几何 / 温度 / 状态位 略
    bool     valid = false;                  // 五重防呆 + 几何自洽全通过
};

bool parseAps014Header(const uint8_t* buf, Aps014FrameMeta& meta,
                       uint32_t expect_payload_len = 655360,   // Gray8 640×1024
                       uint8_t  expect_format      = 0);       // Gray8
```

> 014 事件坐标即设备画布原生坐标（640×1024，x<640、y<1024），无插值。

---

## io：EventReader / EventWriter

头文件：`<shimetapi/io/event_reader.h>`、`<shimetapi/io/event_writer.h>`。命名空间 `Shimeta::io`。按 RAW 文件头部的 `ev_version` 自动选 EVT2/EVT3。

```cpp
enum class RawFormat { Evt2, Evt3, Unknown };

class EventReader {
public:
    bool open(const std::string& filename);
    void close();
    bool isOpen() const;
    RawFormat format() const;
    std::pair<uint32_t, uint32_t> imageSize() const;
    // 头部几何单独访问器：头缺失/未写 width|height 时为 640×512 内部回退值，
    // 先用 hasHeaderSize() 判断是否可信（EventWriter 产物恒携带真实几何）。
    uint32_t evsWidth() const;
    uint32_t evsHeight() const;
    bool hasHeaderSize() const;
    size_t readAllEvents(std::vector<EventCD>& events);  // 读取并解码全部事件
    void reset();
};

class EventWriter {
public:
    bool open(const std::string& filename, uint32_t width, uint32_t height,
              RawFormat fmt = RawFormat::Evt3, uint64_t start_timestamp = 0);
    void close();
    bool isOpen() const;
    size_t writeRaw(const uint8_t* data, size_t len);              // Frame.evs 直写（不编码）
    size_t writeEvents(const std::vector<EventCD>& events);         // Evt2Encoder 编码后写
    void flush();
    uint64_t writtenEventCount() const;
};
```

### `Shimeta::io::HybridWriter`（`io/hybrid_writer.h`）

混合录制：EVS 原始字节写入 RAW，APS 写入 AVI（APX003 为 NV12，APX014 为 Gray8）。`aps_format` 默认 NV12，录制 APX014 时传 `PixelFormat::Gray8`。

```cpp
class HybridWriter {
public:
    ~HybridWriter();
    // 打开 EVS/APS 两路输出（单尺寸：EVS 与 APS 同几何）；aps_fps 仅写入 AVI 头，不控制采集。
    bool open(const std::string& evs_path, const std::string& aps_path,
              uint32_t width, uint32_t height, RawFormat evs_format = RawFormat::Evt3,
              double aps_fps = 30.0, PixelFormat aps_format = PixelFormat::NV12);
    // 双尺寸版：EVS 头写 evs_w×evs_h、AVI 写 aps_w×aps_h——两路几何不同时用此重载；
    // 旧 4 参版本等价于 evs=aps 同尺寸。014 新格式 EVS 头几何为 640×1024（设备画布）。
    bool open(const std::string& evs_path, const std::string& aps_path,
              uint32_t evs_w, uint32_t evs_h, uint32_t aps_w, uint32_t aps_h,
              RawFormat evs_format = RawFormat::Evt3, double aps_fps = 30.0, PixelFormat aps_format = PixelFormat::NV12);
    // 写一帧：EVS 走 writeRaw，APS(NV12/Gray8) 写入 AVI；evs_ts 可选注入 tsmp。
    bool writeFrame(const Shimeta::Frame& frame, const Shimeta::EvsTimestamp* evs_ts = nullptr);
    void close();
    uint32_t apsFrameCount() const;     // 已写入的 APS 帧数
};
```

### `Shimeta::io::HybridReader`（`io/hybrid_reader.h`）

`HybridWriter` 的读取对偶 —— 读取其产出的混合录像（EVS raw + APS NV12/Gray8 AVI，含 tsmp chunk）。与 `Camera` 一样返回原始字节（APS 为 NV12/Gray8、EVS 为原始包），应用自行 `cvtColor` / codec 解码。

```cpp
class HybridReader {
public:
    HybridReader(); ~HybridReader();
    // 打开 EVS/APS 两路文件；任一为空跳过该侧。
    bool open(const std::string& evs_path, const std::string& aps_path);
    void close();
    bool isOpen() const;

    uint32_t width() const;          // APS 宽
    uint32_t height() const;         // APS 高
    double   apsFps() const;         // AVI 头帧率（无效回退 30.0）
    uint32_t apsFrameCount() const;
    PixelFormat apsFormat() const;  // NV12 (APX003) / Gray8 (APX014)

    // 顺序读下一帧 APS（NV12/Gray8 原始字节）。
    bool readApsFrame(Shimeta::Frame& out, Shimeta::EvsTimestamp* evs_ts = nullptr);
    // 顺序读下一包 EVS 原始字节（已跳过 EVT3 文本头）。packet_bytes=0 按 EVT3 头几何自适应：
    //   - 640×1024（新 014）→ DVS1 变长包流逐包切读：每次恰返回一个完整包
    //     （40B 包头 + 包内 payload_size 字节，跨读残余缓冲拼接）；
    //   - 1280×1024（旧 014 录像）→ 固定 164,864B/包（恰好一帧）；
    //   - 其余（003）→ 1 MiB。
    // 显式 packet_bytes 优先于自适应（原始定长读，API 兼容）。
    bool readEvsPacket(Shimeta::Frame& out, size_t packet_bytes = 0);
};
```

---

## Python 绑定（hv_toolkit）

单一 `hv_toolkit` 模块（pybind11）；本发布包提供 **x86_64 / Python 3.10** 预编译模块。方法名转 snake_case，`Frame.evs`/`aps` 为零拷贝 numpy `uint8` 视图，decoder 返回 numpy 结构数组（字段 `x`/`y`/`t`/`polarity`）。

### 导出表面

| Python 符号 | 对应 C++ | 说明 |
| --- | --- | --- |
| `Backend` / `EventFormat` / `PixelFormat` / `QueuePolicy` / `RawFormat` | 同名 enum | 枚举 |
| `EventCD` | `Shimeta::EventCD` | 字段 `x`/`y`/`t`/`polarity` |
| `Frame` | `Shimeta::Frame` | `width`/`height`/`frame_id`/`format` + 只读 `evs`/`aps`（numpy 视图） |
| `DeviceConfig` | `Shimeta::hv::DeviceConfig` | 全字段（USB 的 `vendor_id`/`product_id`、MIPI 的 `sensor_index`/`i2c_bus`/`device_node`、Ethernet 的 `ip`/`listen_port` 等） |
| `Camera` | `Shimeta::hv::Camera` | `init` / `start_stream` / `get_frame` / `stop_stream` / `destroy` / `set_exposure` / `set_frame_rate` / `get_frame_rate` / `sync_clock` |
| `Evt2Decoder` / `Evt2Encoder` | `Shimeta::codec::Evt2*` | `decode(bytes)→ndarray` / `encode(list[EventCD])→bytes` |
| `Evt3Decoder` / `Evt3Encoder` | `Shimeta::codec::Evt3*` | 同上 |
| `MipiRaw8Decoder` | `Shimeta::codec::MipiRaw8Decoder` | MIPI HVS 专用；`decode(bytes, subframe_count=0)→ndarray`（0=自动按长度解全部子帧） |
| `MipiRaw8Layout` | `Shimeta::codec::MipiRaw8Layout` | 类常量 `kSubframeBytes` / `kTotalSubframes` 等 |
| `extract_evs_timestamp(bytes)` | `Shimeta::codec::extractEvsTimestamp` | 返回 `(raw, processed_us, valid)` |

### USB（x86_64）

```python
import hv_toolkit as hv

cfg = hv.DeviceConfig()
cfg.backend    = hv.Backend.Usb
cfg.vendor_id  = 0x1d6b
cfg.product_id = 0x0105

cam = hv.Camera(); cam.init(cfg); cam.start_stream()
f = hv.Frame()
while cam.get_frame(f, 1000):
    print("raw EVS", f.evs.nbytes, "bytes, APS", f.aps.nbytes, "bytes")
cam.stop_stream(); cam.destroy()
```

### MIPI HVS（S100 / RK3588）

只换 backend 与解码器（`Frame.evs` 是 apx003 RAW8 子帧流，**必须用** `MipiRaw8Decoder`）：

```python
cfg = hv.DeviceConfig()
cfg.backend      = hv.Backend.MipiHvs
cfg.device_node  = "/dev/video0"
cfg.sensor_index = 0
cfg.i2c_bus      = 1
dec = hv.MipiRaw8Decoder()
```

### 构建与部署

本发布仓库不编译 SDK 或 Python 绑定源码。随包提供 x86_64 / Python 3.10 预编译模块：

```bash
export LD_LIBRARY_PATH="$PWD/lib/x86_64${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export PYTHONPATH="$PWD/lib/x86_64/python${PYTHONPATH:+:$PYTHONPATH}"
python3 -c "import hv_toolkit; print(hv_toolkit.__version__)"
```

APX014 的 Python 采集返回原始 DVS1 包；不能直接交给 `Evt2Decoder` 或 `MipiRaw8Decoder` 解码整包。`Evs014PacketDecoder`、回调与 IO 类尚未导出 Python；请参考 C++ 示例。ARM Python 模块不在本发布包中。

## 许可证

核心运行库按二进制发布条款交付，见 [README.md](README.md) 的版权声明。头文件中的许可标注不授予闭源二进制组件额外权限。
