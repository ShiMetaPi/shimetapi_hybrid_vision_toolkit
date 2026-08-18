# 01 HV Toolkit

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
- **Runtime MIPI fps tiers**: `DeviceConfig.evs_fps` (120/240/300/500/750/1000, applied at Init — no library rebuild).
- **Zero third-party event-SDK dependencies**: the public surface is fully decoupled from legacy third-party event SDKs.

## 📋 Specifications

### Event camera parameters

- **EVS resolution**: 768×608 (subsampling: 384×304)
- **APS resolution**: 768×608
- **Transport**: USB 3.0 (USB backend) / MIPI-CSI (MIPI backend) / TCP (Ethernet backend)
- **Event formats**: EVT2 / EVT3 (Prophesee EventCD-compatible semantics)

## 🔧 Dependencies

| Dependency | Purpose | Platform |
| --- | --- | --- |
| **libusb-1.0** (`libusb-1.0-0`) | USB backend runtime | x86_64 |
| **aarch64 cross toolchain** (`g++-aarch64-linux-gnu`) | cross-compiling S100 samples | S100 (cross) |
| **OpenCV** | `viewer` / `player` / `live_record_display` samples (cross builds use the bundled `third_party/aarch64_opencv`) | samples, optional |

> v2.0 has **no third-party event SDK dependency** — the codecs are an independent clean-room implementation. OpenCV is only used by sample visualizers, not the core.

## 🚀 Quick Start

### Requirements

- **C++ standard**: C++17 or newer
- **CMake**: 3.16+
- **OS**: Linux — x86_64 (USB + Ethernet backends); aarch64/S100 (MIPI + Ethernet backends)

### Build

The prebuilt libraries ship with the repository; building only compiles the samples
(linking `lib/<arch>`), with the target architecture selected automatically by CMake.

#### x86_64 (USB)

Prerequisites:

```bash
sudo apt-get update
sudo apt-get install build-essential cmake libusb-1.0-0 libopencv-dev
```

Build:

```bash
cd shimetapi_Hybrid_vision_toolkit
./run.sh build                  # = cmake configure + build (native arch by default)
```

Or call cmake directly (equivalent):

```bash
cmake -B out/x86_64/build -S .      # build dir out/<arch>/build (same as run.sh)
cmake --build out/x86_64/build -j    # builds the 7 sample executables
```

Verify outputs:

```bash
ls out/x86_64/build/libshimetapi_*.so                        # 4 libs bundled at build time
ls out/x86_64/build/samples/cpp/get_started/hv_sample_get_started  # sample executable
```

#### S100 (ARM MIPI, cross-compile)

Prerequisites:

```bash
sudo apt-get install g++-aarch64-linux-gnu
```

Build:

```bash
./run.sh build s100    # toolchain file injected automatically (toolchains/toolchain-aarch64-linux-gnu.cmake)
                        # override with -DCMAKE_TOOLCHAIN_FILE=<your-toolchain.cmake>
```

Verify outputs:

```bash
ls out/s100/build/libshimetapi_*.so
file out/s100/build/samples/cpp/get_started/hv_sample_get_started  # should be ELF aarch64
# OpenCV samples use the bundled third_party/aarch64_opencv — all 7 build
```

Link from your own project (CMake):

```cmake
add_subdirectory(shimetapi_Hybrid_vision_toolkit)
target_link_libraries(your_app PRIVATE
    HVToolkit::shimetapi_hv HVToolkit::shimetapi_codec HVToolkit::shimetapi_io)
```

Install to the system (headers + libraries for the current architecture):

```bash
./run.sh install x86_64                          # default /usr/local (sudo)
./run.sh install x86_64 /your/prefix             # custom prefix
```

#### Show supported architectures

```bash
./run.sh --list
# ARCH    STATUS        PREBUILT LIBS
# x86_64   ready         .../lib/x86_64
# s100     ready         .../lib/s100
```

### Running the samples

Build outputs live at `out/<arch>/build/samples/cpp/<name>/hv_sample_<name>` (7 of them).
Capture samples (get_started / callback / record / viewer) default to the USB
backend and switch via `--mipi` (MIPI EVS-only) / `--mipi-hvs` (MIPI dual-VC,
on the S100 board); in USB mode the first two positional args set VID/PID
(default `0x1d6b 0x0105`). Paths below use x86_64; on S100 use `out/s100/build`.

#### x86_64 (USB)

```bash
# get_started — minimal capture
./out/x86_64/build/samples/cpp/get_started/hv_sample_get_started                 # default 0x1d6b:0x0105
./out/x86_64/build/samples/cpp/get_started/hv_sample_get_started 0x1d6b 0x0105   # explicit VID PID
```

```bash
# callback — dual-callback demo (2 seconds)
./out/x86_64/build/samples/cpp/callback/hv_sample_callback
# output: callback: events=N images=M

# record — EVS + APS mixed recording (writes /tmp/hv_record.raw + .avi)
./out/x86_64/build/samples/cpp/record/hv_sample_record

# viewer — live capture with decode counting
./out/x86_64/build/samples/cpp/viewer/hv_sample_viewer
# output: viewer: decoded N events

# bench_hw — USB benchmark (default 5 s)
./out/x86_64/build/samples/cpp/bench_hw/hv_sample_bench_hw
./out/x86_64/build/samples/cpp/bench_hw/hv_sample_bench_hw 0x1d6b 0x0105 10   # explicit VID PID and duration
```

```bash
# player — offline playback of recordings (OpenCV)
./out/x86_64/build/samples/cpp/player/hv_sample_player events.raw video.avi          # fps=30, speed 1.0
./out/x86_64/build/samples/cpp/player/hv_sample_player events.raw video.avi 60 2.0   # explicit fps and speed
```

#### S100 (MIPI HVS)

Deploy to the board (`out/s100/build` is **self-contained** — the `lib/s100`
`libshimetapi_*.so` files are bundled into it at build time and sample rpaths
use `$ORIGIN` relative paths; just copy the whole directory):

```bash
# host
scp -r out/s100/build root@<board-IP>:/app/
# on the board, run directly (no LD_LIBRARY_PATH needed)
/app/build/samples/cpp/get_started/hv_sample_get_started --mipi-hvs
```

> The board needs **no** build environment — cross toolchains live on the build host only.
> OpenCV samples (player / live_record_display) also need `third_party/aarch64_opencv/lib/aarch64-linux-gnu`
> copied somewhere on the board with `export LD_LIBRARY_PATH` pointing at it.

Run samples:

```bash
# get_started — MIPI-HVS minimal capture
/app/build/samples/cpp/get_started/hv_sample_get_started --mipi-hvs

# record — MIPI-HVS mixed recording
/app/build/samples/cpp/record/hv_sample_record --mipi-hvs

# live_record_display — live preview + key-controlled recording
/app/build/samples/cpp/live_record_display/hv_sample_live_record_display
# r start/stop recording, q quit; --no-display headless, --evs-prefix/--aps-prefix set recording prefixes

# player — offline playback
/app/build/samples/cpp/player/hv_sample_player events.raw aps.avi
```

### Python samples

The Python binding (a single `hv_toolkit` module, **prebuilt for x86_64**, requires Python 3.10) ships in `lib/x86_64/python/`:

```bash
# Easiest: install the libs into system paths first, then import directly
sudo ./run.sh install x86_64
python3 samples/python/get_started.py

# Alternative (no install): add the module to sys.path from the repo root
python3 -c "import sys; sys.path.insert(0, 'lib/x86_64/python'); import hv_toolkit; print(hv_toolkit.Camera)"
```

Minimal USB capture — `frame.evs` holds raw event bytes; decode with `Evt2Decoder`:

```python
import hv_toolkit as hv

cfg = hv.DeviceConfig()
cfg.backend    = hv.Backend.Usb
cfg.vendor_id  = 0x1d6b
cfg.product_id = 0x0105

cam = hv.Camera(); cam.init(cfg); cam.start_stream()
dec = hv.Evt2Decoder()
f = hv.Frame()
for _ in range(10):
    if cam.get_frame(f, 1000):
        events = dec.decode(bytes(f.evs))   # → numpy structured array (x, y, t, polarity)
        print(f"frame {f.frame_id} {f.width}x{f.height}: {len(events)} events, aps={f.aps.nbytes} bytes")
cam.stop_stream(); cam.destroy()
```

For MIPI HVS, only the backend and decoder change (`Frame.evs` is a RAW8 subframe stream; use `MipiRaw8Decoder`):

```python
cfg = hv.DeviceConfig()
cfg.backend      = hv.Backend.MipiHvs
cfg.sensor_index = 0       # no VID/PID
cfg.evs_fps      = 500     # fps tier (0 = default 240; one of 120/240/300/500/750/1000; applied at Init)
cfg.i2c_bus      = 1
dec = hv.MipiRaw8Decoder()
```

See the full MIPI sample at [`samples/python/get_started_mipi.py`](samples/python/get_started_mipi.py)
(runs on-device; the prebuilt Python module is x86_64-only).

> **FPS tiers**: `cfg.evs_fps` (0 = default 240). Tiers map to whole-packet subframe counts — 120fps=16, 240fps=32, 300fps=40, 500fps=64, 750fps=100, 1000fps=128 subframes; `MipiRaw8Decoder.decode` adapts automatically to the data length when `subframe_count` is omitted. The tier is selected at `Init` (switching mid-stream would require rebuilding the pipeline, so `SetFrameRate` is unsupported on MIPI).

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

### S100 cross-build: file in wrong format

`./run.sh build s100` fails **early** with install guidance when no aarch64 toolchain is present
(it never reaches the link stage). If you invoke cmake yourself without a toolchain file, the
host compiler fails at link time with `libshimetapi_hv.so: file in wrong format` — fix:
`apt install g++-aarch64-linux-gnu` then use `./run.sh build s100`, or pass `-DCMAKE_TOOLCHAIN_FILE`.

## 📁 Repository layout

```
shimetapi_Hybrid_vision_toolkit/
├── CMakeLists.txt              # prebuilt integration (IMPORTED targets + samples + install rules)
├── run.sh                      # one-stop entry (build/install/samples/pydeploy/--list)
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
├── toolchains/                 # cross toolchain file (aarch64-linux-gnu)
├── third_party/                # aarch64 OpenCV (for cross-building OpenCV samples)
├── samples/                    # samples
│   ├── cpp/                    # C++ samples (7)
│   └── python/                 # Python samples
└── docs/                       # board validation steps and smoke-test notes
```

## 🔍 Sample overview

| Sample | Purpose | Backends | Command |
|---|---|---|---|
| `get_started` | Minimal capture (sync GetFrame) | USB / `--mipi` | `hv_sample_get_started [vid pid] [--mipi]` |
| `callback` | Event + APS async callbacks | USB / `--mipi` / `--mipi-hvs` | `hv_sample_callback [--mipi-hvs]` |
| `record` | EVS+APS recording to /tmp | USB / `--mipi` / `--mipi-hvs` | `hv_sample_record [--mipi-hvs]` |
| `viewer` | Live capture + decode counting | USB / `--mipi` / `--mipi-hvs` | `hv_sample_viewer [--mipi]` |
| `bench_hw` | USB throughput benchmark | USB | `hv_sample_bench_hw [vid pid duration_s]` |
| `live_record_display` | MIPI-HVS live preview + record (OpenCV) | MipiHvs | `hv_sample_live_record_display [--no-display] [--evs-prefix s] [--aps-prefix s]` |
| `player` | Offline playback of .raw + .avi (OpenCV) | offline | `hv_sample_player <events.raw> <video.avi> [fps] [speed]` |

Notes:

- **get_started**: the starter — Init → StartStream → GetFrame ×10, prints per-frame evs bytes.
- **callback**: `SetEventCallback` / `SetImageCallback` dual async callbacks; prints counts after 2 s.
- **record**: `HybridWriter` writes 10 frames to `/tmp/hv_record.raw` (EVS) + `/tmp/hv_record.avi` (APS).
- **viewer**: streams and auto-selects the decoder per backend (USB=EVT2, MIPI=MipiRaw8); prints total decoded events.
- **bench_hw**: timed USB benchmark (default `0x1d6b:0x0105`, 5 s), prints Mev/s and APS fps.
- **live_record_display**: MIPI-HVS dual-VC live preview (EVS left / APS right) + `r`-key recording via `HybridWriter`.
- **player**: `HybridReader` + `MipiRaw8Decoder` playback with GUI controls (play/pause/step/speed/sync).

## 📄 Copyright

Copyright © ShiMetaPi. This repository distributes the HV Toolkit runtime as prebuilt binaries; headers and sample code are provided for integration development. Reverse engineering, disassembly, or redistribution of the binary components is not permitted without written permission. The EVT2/EVT3 codecs are an independent clean-room implementation based on public specifications.

---

## 🙋 Contact

Open hardware site: https://www.shimetapi.cn (CN) / https://www.shimetapi.com (global)
Docs: https://forum.shimetapi.cn/wiki/zh/
Community: https://forum.shimetapi.cn

**HV Toolkit** — making event-camera development simple 🚀
