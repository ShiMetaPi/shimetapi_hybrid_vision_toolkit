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
    int64_t aps_ts_ns  = 0;  // APS frame timestamp (alignment handle; 003 = tail ts ns,
                             // 014 = 80B frame-header wall clock ns, 0 = invalid)
    bool    ptp_locked = false; // whether the Ethernet backend PTP is locked
};
```

> `aps_ts_ns` (USB backend, 2026-10+): APX003 = APS tail timestamp (µs)×1000; APX014 = 80B frame-header wall clock (`sec×1e9+usec×1000`, ns) — same source and domain as event `t` (packet-header wall-clock µs, `sec×1e6+usec`), so they can be paired directly (no calibration stage); 0 = invalid (side-channel timeout). See the sample `samples/cpp/ts_sync`.

### `Shimeta::EvsTimestamp` (`core/evs_timestamp.h`)

EVS sensor-internal timestamp (extracted from the apx003 MIPI RAW8 subframe header), used by `HybridWriter` / `HybridReader` tsmp chunks. Extracted via `Shimeta::codec::extractEvsTimestamp()`. apx003 only — 014 (CF-NRS3 new format) timestamps come directly from the 40B packet header / 80B frame header (see the codec section, `evs_packet_codec.h`).

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

Header: `<shimetapi/codec/mipi_raw8_codec.h>`. Namespace `Shimeta::codec`. apx003 HVS RAW8 subframe stream decoder (clean-room, no third-party SDK dependency). **Stateless**: chunks are multiples of 32,768 bytes, event `t = ts/200` (microseconds, original semantics), no cross-packet reuse. Not thread-safe — call sequentially from a single thread. 014 no longer goes through this decoder (see [EVS 014 packet stream](#evs-014-packet-stream-cf-nrs3-new-format) below).

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

/// Sensor tier (probed by first-APS-frame length at USB backend Init).
enum class SensorModel { Apx003, Apx014 };

inline bool probeSensorByApsLen(size_t aps_transferred, SensorModel& out);
// First-APS-frame transferred length → sensor tier: 700,424→Apx003 (NV12 + 8B tail);
// 655,440→Apx014 (CF-NRS3 new format: 80B header + Gray8 640×1024); unknown → false.

class MipiRaw8Decoder {
public:
    MipiRaw8Decoder() = default;
    // Decode data[0..len): split into 32,768-byte subframes.
    // subframe_count<=0 is auto: decodes all subframes per buffer length (fits any
    // fps tier — whole-packet subframe counts differ: 120fps=16 ... 1000fps=128);
    // an explicit value decodes only the first N subframes.
    size_t Decode(const uint8_t* data, size_t len, std::vector<EventCD>& out,
                  int subframe_count = 0);
};
```

> The `Frame.evs` of the MIPI HVS backend (`Backend::MipiHvs`) is an apx003 RAW8 subframe stream and must be decoded with `MipiRaw8Decoder` (not `Evt2/Evt3Decoder`).

```cpp
// Extract sensor timestamp from the apx003 RAW8 subframe header (45-bit / 200 → microseconds).
// apx003 subframe streams only; 014 timestamps live in the packet/frame headers (next section).
Shimeta::EvsTimestamp extractEvsTimestamp(const uint8_t* data, size_t len);
```

### EVS 014 packet stream (CF-NRS3 new format)

Header: `<shimetapi/codec/evs_packet_codec.h>`. Namespace `Shimeta::codec`. USB data format of 014 (CF-NRS3 firmware):

- **EVS packet stream**: `[PacketHeader 40B][EVT2 payload]` variable-length packets over USB bulk, little-endian; a short trailing transfer marks the packet boundary (the bulk read length is always the slot capacity `kEvsSlotCap` = 32,836 B, sized for the maximum packet 40 + 8192×4 + 4);
- **APS frame stream**: `[80B header][Gray8 640×1024]`, fixed 655,440 B per frame.

**Packet parsing & validation** (`parseEvsPacket`): checks the magic ("DVS1") / version / packet_type, length consistency with the packet boundary, and CRC32 (standard zlib reflected CRC32 over "the 40B header with the checksum field zeroed + payload", with continuation support); on success it yields a pointer to the EVT2 payload (`payload_size` may be 0 — empty frame packets are legal). `evsPacketLooksLikeHeader` checks only the 4-byte magic — used for stream resync and 003/014 content dispatch.

```cpp
struct EvsPacketHeader {           // packed 40B, little-endian on host & device alike
    uint32_t magic;                // "DVS1" (0x44565331)
    uint8_t  version;              // = 1
    uint8_t  packet_type;          // = 1 (EVT2 data)
    uint16_t flags;
    uint32_t sequence_num;         // @8
    uint32_t device_id;
    uint32_t timestamp_sec;        // packet timestamp (seconds)
    uint32_t timestamp_usec;       // packet timestamp (microseconds)
    uint32_t payload_size;         // @24, EVT2 payload bytes
    uint32_t event_count;
    uint32_t checksum;             // @32, CRC32 (checksum zeroed for the computation)
    uint32_t reserved;             // @36, device-side EVS frame number fn (aggregation key)
};

bool parseEvsPacket(const uint8_t* buf, size_t len, EvsPacketHeader& h,
                    const uint8_t** payload = nullptr);
bool evsPacketLooksLikeHeader(const uint8_t* p);   // 4-byte magic quick probe
uint32_t evsPacketCrc32(const uint8_t* d, size_t n);
uint32_t evsPacketCrc32Cont(uint32_t seed, const uint8_t* d, size_t n);  // CRC continuation
```

**Frame-aggregating decoder `Evs014PacketDecoder`** (stateful, single-threaded): aggregates **by frame number** — the `reserved@36` header field carries the device-side EVS frame number fn; events of packets sharing an fn accumulate, and when fn changes the entire previous frame is emitted at once (same-frame packets arrive contiguously, never interleaved across frames). **Timestamps are already microseconds — no self-calibration stage**: event `t = timestamp_sec × 1e6 + timestamp_usec` (packets of one frame share the same ts). EVT2 word decoding is inlined (reusing the word layout of `evt2_codec.h`): CD_ON/CD_OFF words produce events; TIME_HIGH/EXT_TRIGGER words are skipped. Empty frame packets (`payload_size=0`) are legal and still emit on an fn change; a bad packet (parse failure) does not mutate internal state.

```cpp
class Evs014PacketDecoder {
public:
    // Feed one packet: returns the number of events appended to out by this call
    // (= the previous frame's events flushed on a frame-number change; 0 while
    // accumulating within a frame, or on a bad packet).
    size_t Decode(const uint8_t* pkt, size_t len, std::vector<EventCD>& out);
    size_t Flush(std::vector<EventCD>& out);  // flush the accumulated frame (stream tail)
    void   Reset();                           // reset aggregation state
    uint32_t currentFrameNum() const;         // current frame number (0 when none)
    bool     hasCurrentFrame() const;         // whether a frame is accumulating
};
```

**Typical usage** (decoding `Frame.evs` in the USB callback, from `samples/cpp/ts_sync`):

```cpp
Shimeta::codec::Evs014PacketDecoder dec;   // stateful across packets: one instance per callback thread
std::vector<Shimeta::EventCD> events;
dec.Decode(frame.evs.data, frame.evs.size, events);   // emits the previous frame on fn change
```

**APS frame-header parsing** (`parseAps014Header`): 80B header guarded by five sanity gates (magic 0xCF14 / CRC16-CCITT over the first 70B / format / hdr_version / payload_len) plus width-height vs payload geometry consistency (Gray8: W×H == payload_len); `Aps014FrameMeta` returns exposure / gain / geometry / temperature metadata, with `ts_sec/ts_usec` as the frame-start wall clock.

```cpp
struct Aps014FrameMeta {
    uint32_t ts_sec = 0, ts_usec = 0;        // frame-start wall clock (sec/usec)
    uint64_t t_trigger = 0;                  // external-trigger instant (µs)
    uint32_t exposure_us = 0;                // exposure time (µs)
    // … gain / gray statistics / gamma / frame counters / geometry / temperature / status …
    bool     valid = false;                  // all five gates + geometry check passed
};

bool parseAps014Header(const uint8_t* buf, Aps014FrameMeta& meta,
                       uint32_t expect_payload_len = 655360,   // Gray8 640×1024
                       uint8_t  expect_format      = 0);       // Gray8
```

> 014 event coordinates are native device-canvas coordinates (640×1024, x<640, y<1024), no interpolation.

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

Hybrid recording: EVS raw bytes go to RAW; APS goes to AVI (NV12 for APX003, Gray8 for APX014). The default `aps_format` is NV12; pass `PixelFormat::Gray8` for APX014.

```cpp
class HybridWriter {
public:
    ~HybridWriter();
    // Open the EVS/APS outputs; aps_fps is written to the AVI header only and does not control acquisition.
    bool open(const std::string& evs_path, const std::string& aps_path,
              uint32_t width, uint32_t height, RawFormat evs_format = RawFormat::Evt3,
              double aps_fps = 30.0, PixelFormat aps_format = PixelFormat::NV12);
    // Dual-size overload: the EVS header gets evs_w×evs_h, the AVI gets aps_w×aps_h — use when
    // the two geometries differ; the 4-arg version means evs=aps same size. The 014 new-format
    // EVS header geometry is 640×1024 (device canvas).
    bool open(const std::string& evs_path, const std::string& aps_path,
              uint32_t evs_w, uint32_t evs_h, uint32_t aps_w, uint32_t aps_h,
              RawFormat evs_format = RawFormat::Evt3, double aps_fps = 30.0, PixelFormat aps_format = PixelFormat::NV12);
    // Write one frame: EVS goes through writeRaw, APS (NV12/Gray8) into the AVI; evs_ts optionally injects a tsmp.
    bool writeFrame(const Shimeta::Frame& frame, const Shimeta::EvsTimestamp* evs_ts = nullptr);
    void close();
    uint32_t apsFrameCount() const;     // number of APS frames written
};
```

### `Shimeta::io::HybridReader` (`io/hybrid_reader.h`)

Read-side counterpart to `HybridWriter` — reads its hybrid recording (EVS raw + APS NV12/Gray8 AVI, with tsmp chunks). Like `Camera`, returns raw bytes (APS as NV12/Gray8, EVS as raw packets); the application does `cvtColor` / codec decoding.

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
    PixelFormat apsFormat() const;  // NV12 (APX003) / Gray8 (APX014)

    // Read next APS frame sequentially (NV12/Gray8 raw bytes).
    bool readApsFrame(Shimeta::Frame& out, Shimeta::EvsTimestamp* evs_ts = nullptr);
    // Read next EVS packet sequentially (EVT3 text header already skipped). packet_bytes=0
    // adapts by the EVT3 header geometry:
    //   - 640×1024 (new 014) → DVS1 variable-length packet stream: exactly one whole packet
    //     per call (40B header + payload_size bytes, residual bytes spliced across reads);
    //   - 1280×1024 (legacy 014 recordings) → fixed 164,864 B per packet (exactly one frame);
    //   - others (003) → 1 MiB.
    // An explicit packet_bytes overrides the adaptive mode (raw fixed-size reads, API-compatible).
    bool readEvsPacket(Shimeta::Frame& out, size_t packet_bytes = 0);
};
```

---

## Python bindings (hv_toolkit)

A single `hv_toolkit` module (pybind11); this release includes a prebuilt **x86_64 / Python 3.10** module. Methods are snake_case; `Frame.evs`/`aps` are zero-copy numpy `uint8` views and decoders return a numpy structured array (fields `x`/`y`/`t`/`polarity`).

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
f = hv.Frame()
while cam.get_frame(f, 1000):
    print("raw EVS", f.evs.nbytes, "bytes, APS", f.aps.nbytes, "bytes")
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

This release does not compile SDK or Python binding sources. A prebuilt x86_64 / Python 3.10 module is included:

```bash
export LD_LIBRARY_PATH="$PWD/lib/x86_64${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export PYTHONPATH="$PWD/lib/x86_64/python${PYTHONPATH:+:$PYTHONPATH}"
python3 -c "import hv_toolkit; print(hv_toolkit.__version__)"
```

Python capture returns raw DVS1 packets for APX014. Do not pass whole packets directly to `Evt2Decoder` or `MipiRaw8Decoder`. `Evs014PacketDecoder`, callbacks and IO classes are not exported to Python; see the C++ samples. ARM Python modules are not included in this release.

## License

The core runtime is distributed under the binary release terms in [README_EN.md](README_EN.md). License notices in headers do not grant additional rights to closed-source binary components.
