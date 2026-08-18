# HV Toolkit

**Language**: [中文](README.md) | **English**

HV Toolkit (ShiMetaPi Hybrid Vision Toolkit) **v2.0** is a high-performance C++17 SDK for event cameras (DVS/EVS). It unifies capture of event streams (EVS) and image streams (APS), and ships self-contained EVT2/EVT3 codecs with zero third-party dependencies. The v2.0 core rewrite unifies **USB / MIPI / Ethernet backends behind a single `Camera` API**; the public API surface no longer depends on any third-party event SDK.

> This repository is the **prebuilt binary release** (the core is shipped as closed-source `.so` files; only headers and sample sources are included).
> `lib/x86_64/` holds the x86_64 Linux libraries (USB + Ethernet backends); `lib/s100/` holds the S100 aarch64 libraries (MIPI + Ethernet backends).

## ✨ v2.0 Features

- **Unified backends**: USB (libusb) / MIPI (RDK, ARM-only) / Ethernet (POSIX TCP) — one `Camera` interface; the backend is selected via `DeviceConfig.backend`.
- **Dual callbacks + synchronous pull**: `SetFrameCallback` / `SetEventCallback` / `SetImageCallback` async callbacks, `GetFrame` synchronous pull.
- **Self-contained codecs**: EVT2 / EVT3 / MIPI RAW8 `Encoder`/`Decoder`, no external SDK.
- **IO**: `EventReader` / `EventWriter` / `HybridWriter` / `HybridReader` (RAW file IO + mixed EVS/APS record/playback).
- **Python bindings** (optional): a single `hv_toolkit` module (pybind11), prebuilt for x86_64 (Python 3.10).
- **Zero third-party event-SDK dependencies**: the public surface is fully decoupled from legacy third-party event SDKs.

## 📋 Specifications

### Event camera parameters

- **EVS resolution**: 768×608 (subsampling: 384×304)
- **APS resolution**: 768×608
- **Transport**: USB 3.0 (USB backend) / MIPI-CSI (MIPI backend) / TCP (Ethernet backend)
- **Event formats**: EVT2 / EVT3 (Prophesee EventCD-compatible semantics)

### Requirements

- **C++ standard**: C++17 or newer
- **CMake**: 3.16+
- **OS**: Linux — x86_64 (USB + Ethernet backends); aarch64/S100 (MIPI + Ethernet backends)

## 🔧 Dependencies

| Dependency | Purpose | Required |
| --- | --- | --- |
| **libusb-1.0** | USB backend runtime (`libusb-1.0-0`) | USB backend |
| **OpenCV** | `viewer` / `player` / `live_record_display` samples | Samples, optional |

> v2.0 has **no third-party event SDK dependency** — the codecs are an independent clean-room implementation. OpenCV is only used by sample visualizers, not the core.

```bash
# Ubuntu/Debian
sudo apt-get update
sudo apt-get install build-essential cmake libusb-1.0-0 libopencv-dev
```

## 🚀 Quick Start

### Build the samples (against the prebuilt libraries)

The prebuilt libraries ship with the repository; there is nothing to compile for the SDK itself. CMake picks `lib/x86_64` (on x86_64) or `lib/s100` (on aarch64) automatically based on the target architecture:

```bash
cd shimetapi_Hybrid_vision_toolkit
./run.sh build                  # = cmake configure + build (native arch by default)
./run.sh --list                 # show prebuilt architectures and status
```

Or call cmake directly (equivalent):

```bash
cmake -B out/x86_64/build -S .      # build dir out/<arch>/build (same as run.sh)
cmake --build out/x86_64/build -j    # builds the 7 sample executables
```

Cross-building samples for the S100 board (e.g. from x86_64) — links the aarch64 libs in `lib/s100`:

```bash
./run.sh build s100    # needs a system aarch64 cross compiler (apt install g++-aarch64-linux-gnu)
                        # the toolchain file is injected automatically; override with -DCMAKE_TOOLCHAIN_FILE=...
# OpenCV samples use the bundled aarch64 OpenCV in third_party/ (skipped automatically if absent)
```

Sample executables embed an rpath — run them directly (no `LD_LIBRARY_PATH` needed):

```bash
./out/x86_64/build/samples/cpp/get_started/hv_sample_get_started
./out/s100/build/samples/cpp/get_started/hv_sample_get_started    # s100 cross output
```

Link from your own project (CMake):

```cmake
# Either add this repo as a subdirectory, or install it and use find_package
add_subdirectory(shimetapi_Hybrid_vision_toolkit)
target_link_libraries(your_app PRIVATE
    HVToolkit::shimetapi_hv HVToolkit::shimetapi_codec HVToolkit::shimetapi_io)
```

Install to the system (headers + libraries for the current architecture):

```bash
cmake --install out/x86_64/build            # default: /usr/local
cmake --install out/x86_64/build --prefix /your/prefix
```

### Running the samples

#### `get_started` — minimal capture

```bash
./out/x86_64/build/samples/cpp/get_started/hv_sample_get_started          # USB default 0x1d6b:0x0105
./out/x86_64/build/samples/cpp/get_started/hv_sample_get_started 0x1d6b 0x0105   # explicit VID PID
```

#### `record` — event recording

```bash
./out/x86_64/build/samples/cpp/record/hv_sample_record events.raw 5
```

#### `viewer` — visualized playback

```bash
./out/x86_64/build/samples/cpp/viewer/hv_sample_viewer events.raw
```

### Python samples

The Python binding (a single `hv_toolkit` module, **prebuilt for x86_64**, requires Python 3.10) ships in `lib/x86_64/python/`:

```bash
# Easiest deployment: install the libs into system paths first,
# then the module imports directly.
sudo ./run.sh install x86_64
python3 samples/python/get_started.py

# Alternative (no install): keep the module next to the libs and add it to sys.path
python3 -c "import sys; sys.path.insert(0, 'lib/x86_64/python'); import hv_toolkit; print(hv_toolkit.Camera)"
```

Minimal USB capture (`frame.evs` holds raw event bytes; decode with `Evt2Decoder`):

```python
import hv_toolkit as hv

cfg = hv.DeviceConfig()
cfg.backend    = hv.Backend.Usb
cfg.vendor_id  = 0x1d6b
cfg.product_id = 0x0105

cam = hv.Camera()
cam.init(cfg)
cam.start_stream()

dec = hv.Evt2Decoder()
f = hv.Frame()
for _ in range(10):
    if cam.get_frame(f, 1000):
        events = dec.decode(bytes(f.evs))   # → numpy structured array (x, y, t, polarity)
        print(f"frame {f.frame_id} {f.width}x{f.height}: {len(events)} events, aps={f.aps.nbytes} bytes")
cam.stop_stream()
cam.destroy()
```

For MIPI HVS, only the backend and decoder change (`Frame.evs` is a RAW8 subframe stream; use `MipiRaw8Decoder`):

```python
cfg = hv.DeviceConfig()
cfg.backend      = hv.Backend.MipiHvs
cfg.sensor_index = 0       # no VID/PID
cfg.evs_fps      = 500     # fps tier (0 = default 240; one of 120/240/300/500/750/1000; applied at Init)
cfg.i2c_bus      = 1
dec = hv.MipiRaw8Decoder()  # not Evt2Decoder
```

See the full MIPI sample at [`samples/python/get_started_mipi.py`](samples/python/get_started_mipi.py) (runs on-device; the prebuilt Python module is x86_64-only).

> **FPS tiers**: `cfg.evs_fps` (0 = default 240). Tiers map to whole-packet subframe counts — 120fps=16, 240fps=32, 300fps=40, 500fps=64, 750fps=100, 1000fps=128 subframes; `MipiRaw8Decoder.decode` adapts automatically to the data length when `subframe_count` is omitted. The tier is selected at `Init` (switching mid-stream would require rebuilding the pipeline, so `SetFrameRate` is unsupported on MIPI).

**Deploying on S100**: copy `libshimetapi_*.so*` from `lib/s100/` to the board (e.g. `/app/lib`), set the library path, and run the C++ samples:

```bash
# on the S100 board
export LD_LIBRARY_PATH=/app/lib:$LD_LIBRARY_PATH
./hv_sample_get_started
```

> The Python module currently exports `Camera` / `DeviceConfig` / `Frame` (with zero-copy numpy views of `evs`/`aps`) / the `Evt2`, `Evt3`, `MipiRaw8` codecs / `extract_evs_timestamp`. Callbacks, `EventReader/Writer`, and `HybridReader` are not exported yet — use the C++ API (see [API_EN.md](API_EN.md)).

### USB device permissions

If the first run reports `LIBUSB_ERROR_ACCESS`, set up a udev rule (no sudo needed afterwards):

```bash
echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="1d6b", ATTR{idProduct}=="0105", MODE="0666"' \
  | sudo tee /etc/udev/rules.d/99-hv-camera.rules
sudo udevadm control --reload-rules && sudo udevadm trigger
```

## 🚨 Troubleshooting

### USB device not found / LIBUSB_ERROR_ACCESS

**no match devices found** means the device is not connected — check the cable and the `vendor_id` / `product_id` values.

If the device is connected and the IDs are correct but you get **Cannot open device: LIBUSB_ERROR_ACCESS**, it is a permissions issue:

```bash
# Temporary: open up the USB bus
sudo chmod -R 777 /dev/bus/usb/
# Recommended: the udev rule above (persistent, no sudo)
```

## 📁 Repository layout

```
shimetapi_Hybrid_vision_toolkit/
├── CMakeLists.txt              # prebuilt integration (IMPORTED targets + samples + install rules)
├── README.md / README_EN.md    # docs (zh/en)
├── API.md / API_EN.md          # public API reference (zh/en)
├── include/shimetapi/          # public headers
│   ├── core/                   # EventCD / Status / BufferPool / Frame / PixelFormat / Timestamp
│   ├── hv/                     # Camera / DeviceConfig / EventFormat / EventPacket / ImageData
│   ├── codec/                  # EVT2 / EVT3 / MIPI RAW8 codecs
│   └── io/                     # EventReader / EventWriter / HybridWriter / HybridReader
├── lib/                        # prebuilt libraries (closed-source binaries)
│   ├── x86_64/                 # x86_64: USB + Ethernet backends (python/ holds the binding module)
│   └── s100/                   # S100 aarch64: MIPI + Ethernet backends
├── samples/                    # samples
│   ├── cpp/                    # C++ samples
│   │   ├── get_started/        # minimal capture
│   │   ├── callback/           # dual-callback demo
│   │   ├── record/             # event recording
│   │   ├── viewer/             # visualized playback
│   │   ├── bench_hw/           # real-hardware USB benchmark
│   │   ├── live_record_display/  # MIPI-HVS live preview + recording
│   │   └── player/             # offline playback (EVS+APS)
│   └── python/                 # Python samples
└── docs/                       # on-device smoke-test notes
```

## 🔍 Sample overview

### get_started

The introductory sample: initialize the camera, pull frames synchronously, decode events. The best starting point for learning HV Toolkit.

### callback

Dual-callback demo (event + APS async callbacks) showing `SetEventCallback` / `SetImageCallback`.

### record

Writes the camera event stream to a RAW file, with both `writeRaw` (direct) and `writeEvents` (encode-then-write) paths.

### viewer

Event visualizer: reads a RAW file and accumulates display via OpenCV, with pause and playback controls.

### bench_hw

Real-hardware USB benchmark (default `0x1d6b:0x0105`, 5 s), printing Mev/s and APS fps.

### live_record_display

MIPI-HVS live preview + key-controlled recording (`r` toggles). Shows the EVS visualization and the APS image simultaneously; writes EVS raw + APS AVI via `HybridWriter`.

### player

Offline playback of recorded files (`.raw` + `.avi`) via `HybridReader` and `MipiRaw8Decoder`, with GUI controls (play/pause/step/speed/sync).

## 📄 Copyright

Copyright © ShiMetaPi. This repository distributes the HV Toolkit runtime as prebuilt binaries; headers and sample code are provided for integration development. Reverse engineering, disassembly, or redistribution of the binary components is not permitted without written permission. The EVT2/EVT3 codecs are an independent clean-room implementation based on public specifications.

---

## 🙋 Contact

Open hardware site: https://www.shimetapi.cn (CN) / https://www.shimetapi.com (global)
Docs: https://forum.shimetapi.cn/wiki/zh/
Community: https://forum.shimetapi.cn

**HV Toolkit** — making event-camera development simple 🚀
