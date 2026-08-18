# HV Toolkit v2.0 C++ API Documentation

[中文](API.md) | **English**

This document describes the HV Toolkit v2.0 public API (headers under `include/shimetapi/`). All symbols live under the `Shimeta::` namespace; zero third-party event-SDK dependency.

---

## Table of Contents

- [Core types](#core-types)
- [Camera, DeviceConfig, Frame](#camera--deviceconfig--frame)
- [Codec: EVT2, EVT3, MIPI RAW8](#codec--evt2--evt3--mipi-raw8)
- [IO: EventReader, EventWriter, HybridWriter](#io--eventreader--eventwriter--hybridwriter)
- [Python bindings (hv_toolkit)](#python-bindings-hv_toolkit)

---

## Core types

Headers: `<shimetapi/core/*.h>`

### `Shimeta::EventCD` (`core/event_cd.h`)

Own event type (POD); field semantics correspond one-to-one with common industry event structures.

```cpp
struct EventCD {
    uint16_t x;        // pixel X coordinate
    uint16_t y;        // pixel Y coordinate
    int64_t  t;        // timestamp (microseconds)
    bool     polarity; // 1 = CD_ON, 0 = CD_OFF
};
```

### `Shimeta::Status` (`core/status.h`)

```cpp
enum class Status : int32_t {
    Ok = 0, ErrDeviceNotFound = -1, ErrPermissionDenied = -2,
    ErrUsbTransfer = -3, ErrV4l2Ioctl = -4, ErrNetworkTimeout = -5,
    ErrInvalidParam = -6, ErrBufferFull = -7, ErrDecodeFailure = -8,
    ErrUnsupportedFormat = -9,
};
const char* statusToString(Status s);
```

### `Shimeta::BufferView` / `BufferPool` (`core/buffer_pool.h`)

```cpp
struct BufferView {
    const uint8_t* data = nullptr;
    size_t         size = 0;
};
// Fixed-size slab pool: acquire() returns shared_ptr<uint8_t[]>;
// when the last reference is released the slab is returned to the pool automatically.
class BufferPool {
public:
    BufferPool(size_t slab_size, size_t slab_count);
    std::shared_ptr<uint8_t[]> acquire();
    size_t slab_size() const;
    size_t capacity() const;
    size_t available() const;
};
```

### `Shimeta::PixelFormat` (`core/pixel_format.h`)

```cpp
enum class PixelFormat : uint8_t { BayerRG8 = 0, RGB888 = 1, Gray8 = 2, RAW8 = 3, RAW10 = 4, NV12 = 5 };
```

> `NV12` is the packed YUV format of the APS frame produced by the MIPI HVS backend after ISP→PYM (see `Backend::MipiHvs`).

### `Shimeta::TimestampInfo` (`core/timestamp.h`)

```cpp
struct TimestampInfo {
    int64_t evs_ts_ns  = 0;  // EVS event reference timestamp (nanoseconds)
    int64_t aps_ts_ns  = 0;  // APS exposure instant (nanoseconds)
    bool    ptp_locked = false; // whether the Ethernet backend PTP is locked
};
```

### `Shimeta::EvsTimestamp` (`core/evs_timestamp.h`)

EVS sensor-internal timestamp (extracted from the MIPI RAW8 subframe header), used by `HybridWriter` / `HybridReader` tsmp chunks. Extracted via `Shimeta::codec::extractEvsTimestamp()`.

```cpp
struct EvsTimestamp {
    uint64_t raw_timestamp = 0;         // sensor 45-bit raw timestamp
    uint64_t processed_timestamp = 0;   // raw_timestamp / 200 (microseconds)
    bool     valid = false;
};
```

---

## Camera, DeviceConfig, Frame

### `Shimeta::hv::Backend` / `EventFormat` (`hv/device_config.h`, `hv/event_format.h`)

```cpp
enum class Backend    { Auto, Usb, Mipi, MipiHvs, Ethernet };
enum class EventFormat { Evt2, Evt3 };
```

| Backend | Description |
| --- | --- |
| `Auto` | Automatic selection (inferred from `DeviceConfig` fields). |
| `Usb` | libusb backend (USB camera). |
| `Mipi` | MIPI backend (EVS-only). |
| `MipiHvs` | MIPI HVS dual-VC backend: VC0 carries EVS events, VC1 carries APS frames (→ISP→PYM NV12). |
| `Ethernet` | Ethernet backend (POSIX sockets). |

### `Shimeta::hv::DeviceConfig` (`hv/device_config.h`)

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
    int         event_urbs   = 4;             // USB event endpoint in-flight URB count
    uint16_t    evs_fps      = 0;             // MIPI: EVS fps tier (0 = default 240; one of 120/240/300/500/750/1000; selects the sensor config at Init; invalid values fail). USB/Ethernet: change fps at runtime via Camera::SetFrameRate
    int         sensor_index = 0;             // MIPI sensor index
    uint8_t     i2c_bus      = 1;             // MIPI secure-chip authentication I2C bus
    uint16_t    listen_port = 8888;           // Ethernet: TCP listen port
    std::string bind_ip;                      // Ethernet: local bind IP (empty = INADDR_ANY)
};
```

### `Shimeta::hv::Camera` (`hv/camera.h`)

Unified acquisition API; the same interface covers all three backends (USB / MIPI / Ethernet). Supports synchronous pull (`GetFrame`) and asynchronous callbacks (any combination of Frame/Event/Image).

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

| Method | Description |
| --- | --- |
| `Init(cfg)` | Initialize the backend from `DeviceConfig` (does not block on opening hardware; some backends connect only at `StartStream`). |
| `StartStream()` | Start the acquisition thread; returns whether the device was connected successfully. |
| `StopStream()` | Stop acquisition and join the thread. |
| `Destroy()` | Release backend resources. |
| `GetFrame(frame, timeout_ms)` | Synchronously pull one combined frame (events + APS); returns whether a frame was obtained within the timeout. |
| `SetFrameCallback` / `SetEventCallback` / `SetImageCallback` | Register asynchronous callbacks; callbacks fire serially on the dispatch thread only, never on the acquisition thread. |
| `SetExposure(value)` | Set APS exposure. |
| `SetFrameRate(fps)` | Set the EVS event frame rate (currently supported on the USB / Ethernet backends). |
| `GetFrameRate(fps)` | Read the current EVS event frame rate. |
| `SyncClock()` | Clock synchronization (e.g. Ethernet PTP mode 0). |

### `Shimeta::Frame` (`core/frame.h`)

Unified frame: `aps`/`evs` are read-only views into pool memory; `*_owner` holds the slab reference so the views stay valid for the lifetime of the Frame (zero-copy, pool-managed lifetime).

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

> `Frame.evs` is the HAL-undecoded raw event bytes (EVT2/EVT3, determined by `event_fmt`); decode them with the matching codec.

### `Shimeta::hv::EventPacket` (`hv/event_packet.h`) / `ImageData` (`hv/image_data.h`)

```cpp
namespace Shimeta::hv {
struct EventPacket {
    BufferView data{};       // one packet of raw event bytes
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

## Codec: EVT2, EVT3, MIPI RAW8

Headers: `<shimetapi/codec/evt2_codec.h>`, `<shimetapi/codec/evt3_codec.h>`. Namespace `Shimeta::codec`. `Encoder`/`Decoder` are **stateful** (they maintain the time base / rollover counter across packets); reuse the same instance across packets of a stream, and call `Reset()` before a new stream.

### EVT3 (16-bit word stream)

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
    // len must be a multiple of 2; returns the number of events decoded in this call.
    size_t Decode(const uint8_t* buf, size_t len, std::vector<EventCD>& out);
    void Reset();
};
```

### EVT2 (32-bit word stream)

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

**Typical usage** (decoding `Frame.evs`):

```cpp
Shimeta::codec::Evt3Decoder dec;  // or Evt2Decoder, depending on cfg.event_fmt
std::vector<Shimeta::EventCD> events;
dec.Decode(frame.evs.data, frame.evs.size, events);
```

### MIPI RAW8 (apx003 subframe stream)

Header: `<shimetapi/codec/mipi_raw8_codec.h>`. Namespace `Shimeta::codec`. apx003 HVS RAW8 subframe stream decoder (clean-room, no third-party SDK dependency). **Stateless**; no cross-packet reuse needed, `Reset()` is a no-op.

```cpp
struct MipiRaw8Layout {
    static constexpr int    kSubWidth       = 384;
    static constexpr int    kSubHeight      = 304;
    static constexpr int    kEvsWidth       = 768;
    static constexpr int    kEvsHeight      = 608;
    static constexpr int    kSubFrameNum    = 4;      // 4 spatial subframes per frame
    static constexpr int    kRawMergeNum    = 8;      // 8 frames per MIPI packet
    static constexpr int    kTotalSubframes = kSubFrameNum * kRawMergeNum; // 32
    static constexpr size_t kSubframeBytes  = 32768;  // HV_SUB_FULL_BYTE_SIZE
    static constexpr uint32_t kHeaderMask   = 0x0000FFFFu;
};

class MipiRaw8Decoder {
public:
    MipiRaw8Decoder() = default;
    // Decode data[0..len). subframe_count<=0 is auto: decodes all subframes per
    // len/kSubframeBytes (whole-packet subframe count varies by fps tier:
    // 120fps=16 ... 1000fps=128); an explicit value decodes only the first N.
    size_t Decode(const uint8_t* data, size_t len, std::vector<EventCD>& out,
                  int subframe_count = 0);
    void Reset();  // stateless, no-op
};
```

> The `Frame.evs` of the MIPI HVS backend (`Backend::MipiHvs`) is an apx003 RAW8 subframe stream and must be decoded with `MipiRaw8Decoder` (not `Evt2/Evt3Decoder`).

```cpp
// Extract sensor timestamp from apx003 RAW8 subframe header (45-bit / 200 → microseconds).
Shimeta::EvsTimestamp extractEvsTimestamp(const uint8_t* data, size_t len);
```

---

## IO: EventReader, EventWriter, HybridWriter, HybridReader

Headers: `<shimetapi/io/event_reader.h>`, `<shimetapi/io/event_writer.h>`. Namespace `Shimeta::io`. Selects EVT2/EVT3 automatically from the RAW file header's `ev_version`.

```cpp
enum class RawFormat { Evt2, Evt3, Unknown };

class EventReader {
public:
    bool open(const std::string& filename);
    void close();
    bool isOpen() const;
    RawFormat format() const;
    std::pair<uint32_t, uint32_t> imageSize() const;
    size_t readAllEvents(std::vector<EventCD>& events);  // read and decode all events
    void reset();
};

class EventWriter {
public:
    bool open(const std::string& filename, uint32_t width, uint32_t height,
              RawFormat fmt = RawFormat::Evt3, uint64_t start_timestamp = 0);
    void close();
    bool isOpen() const;
    size_t writeRaw(const uint8_t* data, size_t len);              // write Frame.evs as-is (no encoding)
    size_t writeEvents(const std::vector<EventCD>& events);         // encode with Evt2Encoder then write
    void flush();
    uint64_t writtenEventCount() const;
};
```

### `Shimeta::io::HybridWriter` (`io/hybrid_writer.h`)

Hybrid recording facade: EVS is stored as a RAW event file (reusing `EventWriter`), and APS (packed NV12) is stored as AVI (with a tsmp timestamp chunk). Symmetric with the read-side `HybridReader`. `EvsTimestamp` is defined in the [Core section](#shimetavstimestamp-core-timestamph).

```cpp
class HybridWriter {
public:
    ~HybridWriter();
    // Open the EVS/APS outputs; aps_fps is written to the AVI header only and does not control acquisition.
    bool open(const std::string& evs_path, const std::string& aps_path,
              uint32_t width, uint32_t height, RawFormat evs_format = RawFormat::Evt3,
              double aps_fps = 30.0);
    // Write one frame: EVS goes through writeRaw, APS (NV12) into the AVI; evs_ts optionally injects a tsmp.
    bool writeFrame(const Shimeta::Frame& frame, const Shimeta::EvsTimestamp* evs_ts = nullptr);
    void close();
    uint32_t apsFrameCount() const;     // number of APS frames written
};
```

### `Shimeta::io::HybridReader` (`io/hybrid_reader.h`)

Read-side counterpart to `HybridWriter` — reads its hybrid recording (EVS raw + APS NV12 AVI, with tsmp chunks). Like `Camera`, returns raw bytes (APS as NV12, EVS as raw packets); the application does `cvtColor` / codec decoding.

```cpp
class HybridReader {
public:
    HybridReader(); ~HybridReader();
    // Open EVS/APS files; either path empty → skip that side.
    bool open(const std::string& evs_path, const std::string& aps_path);
    void close();
    bool isOpen() const;

    uint32_t width() const;          // APS width
    uint32_t height() const;         // APS height
    double   apsFps() const;         // AVI header fps (falls back to 30.0)
    uint32_t apsFrameCount() const;

    // Read next APS frame sequentially (NV12 raw bytes).
    bool readApsFrame(Shimeta::Frame& out, Shimeta::EvsTimestamp* evs_ts = nullptr);
    // Read next EVS packet sequentially (EVT3 text header already skipped). packet_bytes=0 → default 1 MiB.
    bool readEvsPacket(Shimeta::Frame& out, size_t packet_bytes = 0);
};
```

---

## Python bindings (hv_toolkit)

A single `hv_toolkit` module (pybind11) exposing **the same API for USB (x86_64) and MIPI HVS (S100/RK3588)**. Methods are snake_case; `Frame.evs`/`aps` are zero-copy numpy `uint8` views and decoders return a numpy structured array (fields `x`/`y`/`t`/`polarity`).

### Exported surface

| Python symbol | C++ equivalent | Notes |
| --- | --- | --- |
| `Backend` / `EventFormat` / `PixelFormat` / `QueuePolicy` / `RawFormat` | same-name enums | enums |
| `EventCD` | `Shimeta::EventCD` | fields `x`/`y`/`t`/`polarity` |
| `Frame` | `Shimeta::Frame` | `width`/`height`/`frame_id`/`format` + read-only `evs`/`aps` (numpy views) |
| `DeviceConfig` | `Shimeta::hv::DeviceConfig` | all fields (USB `vendor_id`/`product_id`, MIPI `sensor_index`/`i2c_bus`/`device_node`, Ethernet `ip`/`listen_port`, …) |
| `Camera` | `Shimeta::hv::Camera` | `init` / `start_stream` / `get_frame` / `stop_stream` / `destroy` / `set_exposure` / `set_frame_rate` / `get_frame_rate` / `sync_clock` |
| `Evt2Decoder` / `Evt2Encoder` | `Shimeta::codec::Evt2*` | `decode(bytes)→ndarray` / `encode(list[EventCD])→bytes` |
| `Evt3Decoder` / `Evt3Encoder` | `Shimeta::codec::Evt3*` | same |
| `MipiRaw8Decoder` | `Shimeta::codec::MipiRaw8Decoder` | MIPI HVS only; `decode(bytes, subframe_count=0)→ndarray` (0 = auto: all subframes per data length) |
| `MipiRaw8Layout` | `Shimeta::codec::MipiRaw8Layout` | class constants `kSubframeBytes` / `kTotalSubframes` … |
| `extract_evs_timestamp(bytes)` | `Shimeta::codec::extractEvsTimestamp` | returns `(raw, processed_us, valid)` |

### USB (x86_64)

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

### MIPI HVS (S100 / RK3588)

Only the backend and decoder change (`Frame.evs` is an apx003 RAW8 subframe stream — **must use** `MipiRaw8Decoder`):

```python
cfg = hv.DeviceConfig()
cfg.backend      = hv.Backend.MipiHvs
cfg.device_node  = "/dev/video0"
cfg.sensor_index = 0
cfg.i2c_bus      = 1
dec = hv.MipiRaw8Decoder()
```

### Build & deploy

```bash
./run.sh --python build x86_64     # USB; artifact build/hv_toolkit.<abi>.so
./run.sh --python build s100       # MIPI HVS (aarch64); artifact out/s100/build/hv_toolkit.<abi>.so
```

- x86: uses the host Python; just `import hv_toolkit`.
- S100/RK3588: cross-compilation needs the target-arch `Python.h`; the build searches `PYTHON_TARGET_ROOT`/`S100_SYSROOT`/`RDK_SYSROOT`/`/usr/aarch64-linux-gnu` and the bundled `legacy/rdk_sysroot` (aarch64 python3.10 headers) in order, so it usually works with no manual config and produces `hv_toolkit.cpython-310-aarch64-linux-gnu.so`. On the board, set `LD_LIBRARY_PATH` to the directory containing `libshimetapi_*.so`. See [README_EN.md](README_EN.md).

> Not yet exported (added on demand): callbacks (`SetFrameCallback`/`SetEventCallback`/`SetImageCallback`), `EventReader`/`EventWriter`, `HybridReader`/`HybridWriter`.

---

## License

Apache License 2.0. The EVT2/EVT3 codecs are independent implementations based on public specifications (clean-room) and contain no third-party closed-source code.
