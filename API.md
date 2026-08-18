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
    int64_t aps_ts_ns  = 0;  // APS 曝光时刻（纳秒）
    bool    ptp_locked = false; // Ethernet 后端 PTP 是否锁定
};
```

### `Shimeta::EvsTimestamp`（`core/evs_timestamp.h`）

EVS 传感器内部时间戳（从 MIPI RAW8 子帧头提取），用于 `HybridWriter` / `HybridReader` 的 tsmp chunk。由 `Shimeta::codec::extractEvsTimestamp()` 提取。

```cpp
struct EvsTimestamp {
    uint64_t raw_timestamp = 0;         // 传感器 45-bit 原始时间戳
    uint64_t processed_timestamp = 0;   // raw_timestamp / 200（微秒）
    bool     valid = false;
};
```

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
| `Usb` | libusb 后端（USB 相机）。 |
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
    uint16_t    evs_fps      = 0;             // 0=不设置（用设备默认）；非 0=Init 时自动下发（运行时改用 Camera::SetFrameRate）
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

头文件：`<shimetapi/codec/mipi_raw8_codec.h>`。命名空间 `Shimeta::codec`。apx003 HVS RAW8 子帧流解码（clean-room，无第三方 SDK 依赖）。**无状态**，无需跨包复用、`Reset()` 为 no-op。

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

class MipiRaw8Decoder {
public:
    MipiRaw8Decoder() = default;
    // 解码 data[0..len)；subframe_count 指定子帧数（默认整包 32）。返回解码事件数。
    size_t Decode(const uint8_t* data, size_t len, std::vector<EventCD>& out,
                  int subframe_count = MipiRaw8Layout::kTotalSubframes);
    void Reset();  // 无状态，no-op
};
```

> MIPI HVS 后端（`Backend::MipiHvs`）的 `Frame.evs` 为 apx003 RAW8 子帧流，需用 `MipiRaw8Decoder`（而非 `Evt2/Evt3Decoder`）解码。

```cpp
// 从 apx003 RAW8 子帧头提取传感器时间戳（45-bit / 200 → 微秒）。
Shimeta::EvsTimestamp extractEvsTimestamp(const uint8_t* data, size_t len);
```

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

混合录制门面：EVS 存为 RAW 事件文件（复用 `EventWriter`），APS（packed NV12）存为 AVI（含 tsmp 时间戳 chunk）。与读侧 `HybridReader` 对称。`EvsTimestamp` 定义见 [core 节](#shimetavstimestamp-core-timestamph)。

```cpp
class HybridWriter {
public:
    ~HybridWriter();
    // 打开 EVS/APS 两路输出；aps_fps 仅写入 AVI 头，不控制采集。
    bool open(const std::string& evs_path, const std::string& aps_path,
              uint32_t width, uint32_t height, RawFormat evs_format = RawFormat::Evt3,
              double aps_fps = 30.0);
    // 写一帧：EVS 走 writeRaw，APS(NV12) 写入 AVI；evs_ts 可选注入 tsmp。
    bool writeFrame(const Shimeta::Frame& frame, const Shimeta::EvsTimestamp* evs_ts = nullptr);
    void close();
    uint32_t apsFrameCount() const;     // 已写入的 APS 帧数
};
```

### `Shimeta::io::HybridReader`（`io/hybrid_reader.h`）

`HybridWriter` 的读取对偶 —— 读取其产出的混合录像（EVS raw + APS NV12 AVI，含 tsmp chunk）。与 `Camera` 一样返回原始字节（APS 为 NV12、EVS 为原始包），应用自行 `cvtColor` / codec 解码。

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

    // 顺序读下一帧 APS（NV12 原始字节）。
    bool readApsFrame(Shimeta::Frame& out, Shimeta::EvsTimestamp* evs_ts = nullptr);
    // 顺序读下一包 EVS 原始字节（已跳过 EVT3 文本头）。packet_bytes=0 默认 1 MiB。
    bool readEvsPacket(Shimeta::Frame& out, size_t packet_bytes = 0);
};
```

---

## Python 绑定（hv_toolkit）

单一 `hv_toolkit` 模块（pybind11），**同一套 API 支持 USB（x86_64）与 MIPI HVS（S100/RK3588）**。方法名转 snake_case，`Frame.evs`/`aps` 为零拷贝 numpy `uint8` 视图，decoder 返回 numpy 结构数组（字段 `x`/`y`/`t`/`polarity`）。

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
| `MipiRaw8Decoder` | `Shimeta::codec::MipiRaw8Decoder` | MIPI HVS 专用；`decode(bytes, subframe_count=-1)→ndarray` |
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
dec = hv.Evt2Decoder()
f = hv.Frame()
while cam.get_frame(f, 1000):
    ev = dec.decode(bytes(f.evs))      # → ndarray[(x,y,t,polarity)]
    print(len(ev), "events, aps", f.aps.nbytes, "bytes")
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

```bash
./run.sh --python build x86_64     # USB；产物 build/hv_toolkit.<abi>.so
./run.sh --python build s100       # MIPI HVS（aarch64）；产物 out/s100/build/hv_toolkit.<abi>.so
```

- x86：用 host python，`import hv_toolkit` 即可。
- S100/RK3588：交叉编译需目标架构 `Python.h`，构建系统按序在 `PYTHON_TARGET_ROOT`/`S100_SYSROOT`/`RDK_SYSROOT`/`/usr/aarch64-linux-gnu` 及仓库自带 `legacy/rdk_sysroot`（aarch64 python3.10 头）下查找，通常免配置即可编出 `hv_toolkit.cpython-310-aarch64-linux-gnu.so`；板卡运行需 `LD_LIBRARY_PATH` 指向 `libshimetapi_*.so` 目录。详见 [README.md](README.md)「Python 示例」。

> 未导出（后续按需补）：回调（`SetFrameCallback`/`SetEventCallback`/`SetImageCallback`）、`EventReader`/`EventWriter`、`HybridReader`/`HybridWriter`。

---

## 许可证

Apache License 2.0。EVT2/EVT3 编解码为基于公开规范的独立实现（clean-room），不含第三方闭源源码。
