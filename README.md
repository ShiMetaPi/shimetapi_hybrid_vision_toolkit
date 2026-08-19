# 01 HV Toolkit

**语言**: **中文** | [English](README_EN.md)

HV Toolkit（ShiMetaPi Hybrid Vision Toolkit）**v2.0** 是面向事件相机（DVS/EVS）的高性能 C++17 SDK，统一采集事件数据流（EVS）与图像数据流（APS），自带零第三方依赖的 EVT2/EVT3 编解码。v2.0 完成核心重写：**USB / MIPI / Ethernet 三后端统一为同一套 `Camera` API**，公有 API 表面不再依赖任何第三方事件 SDK。

> 本仓库为**预编译二进制发布版**（核心以 `.so` 闭源交付，仅公开头文件与示例源码）。
> `lib/x86_64/` 为 x86_64 Linux 库（USB + Ethernet 后端），`lib/s100/` 与 `lib/x5/` 为 aarch64 库（MIPI + Ethernet 后端）。

## ✨ v2.0 特性

- **三后端统一**：USB（libusb）/ MIPI（RDK，仅 ARM）/ Ethernet（POSIX TCP）—— 同一套 `Camera` 接口，后端由 `DeviceConfig.backend` 选择。
- **双回调 + 同步拉取**：`SetFrameCallback` / `SetEventCallback` / `SetImageCallback` 异步回调，`GetFrame` 同步拉取。
- **自洽编解码**：EVT2 / EVT3 / MIPI RAW8 的 `Encoder`/`Decoder`，零外部 SDK。
- **IO 读写**：`EventReader` / `EventWriter` / `HybridWriter` / `HybridReader`（RAW 文件读写 + EVS/APS 混合录制/回放）。
- **Python 绑定**（可选）：单一 `hv_toolkit` 模块（pybind11），x86_64 预编译（Python 3.10）。
- **MIPI 帧率档运行时选择**：`DeviceConfig.evs_fps`（120/240/300/500/750/1000，Init 时生效，无需重编库）。
- **零第三方事件 SDK 依赖**：公有表面已与旧版第三方事件 SDK 完全解耦。

## 📋 技术规格

### 事件相机参数

- **EVS 分辨率**：768×608（子采样：384×304）
- **APS 分辨率**：768×608
- **数据传输**：USB 3.0（USB 后端）/ MIPI-CSI（MIPI 后端）/ TCP（Ethernet 后端）
- **事件格式**：EVT2 / EVT3（兼容 Prophesee EventCD 语义）

## 🔧 依赖

| 依赖 | 用途 | 适用平台 |
| --- | --- | --- |
| **libusb-1.0**（`libusb-1.0-0`） | USB 后端运行库 | x86_64 |
| **aarch64 交叉工具链**（`g++-aarch64-linux-gnu`） | s100/x5 交叉编译示例 | s100、x5（交叉编时） |
| **OpenCV** | `viewer` / `player` / `live_record_display` 示例（交叉编用仓库自带 `third_party/aarch64_opencv`） | 示例可选 |

> v2.0 **不再依赖**第三方事件 SDK —— 编解码为独立 clean-room 实现。OpenCV 仅在示例可视化中用到，非核心依赖。

## 🚀 快速开始

### 系统要求

- **C++ 标准**：C++17 或更高
- **CMake**：3.16+
- **操作系统**：Linux —— x86_64（USB + Ethernet 后端）；aarch64/S100 / X5（MIPI + Ethernet 后端）

### 构建

预编译库随仓库分发，无需编译 SDK 本体；构建只编译示例（链接 `lib/<arch>` 的库），CMake 按目标架构自动选择。

#### x86_64（USB）

前置：安装依赖

```bash
sudo apt-get update
sudo apt-get install build-essential cmake libusb-1.0-0 libopencv-dev
```

构建：

```bash
cd shimetapi_Hybrid_vision_toolkit
./run.sh build                  # = cmake 配置 + 编译（默认本机架构）
```

也可直接用 cmake（等价）：

```bash
cmake -B out/x86_64/build -S .      # 构建目录 out/<arch>/build（与 run.sh 一致）
cmake --build out/x86_64/build -j    # 编出 7 个示例可执行文件
```

验证产物：

```bash
ls out/x86_64/build/libshimetapi_*.so                        # 构建时自动捆绑的 4 个库
ls out/x86_64/build/samples/cpp/get_started/hv_sample_get_started  # 示例可执行文件
```

#### S100（ARM MIPI，交叉编译）

前置：下载 + 解压 S100 工具链到 /opt/

```bash
curl -fO http://archive.d-robotics.cc/toolchain/arm-gnu-toolchain-11.3.rel1-x86_64-aarch64-none-linux-gnu.tar.xz
sudo tar -xvf arm-gnu-toolchain-11.3.rel1-x86_64-aarch64-none-linux-gnu.tar.xz -C /opt
```

加入 PATH

```bash
export PATH=$PATH:/opt/arm-gnu-toolchain-11.3.rel1-x86_64-aarch64-none-linux-gnu/bin
```

S100_SDK仓库地址：https://github.com/ShiMetaPi/evs_device_vendor_sdk.git
拉取仓库后，设 S100 sysroot（板级库路径）

```bash
export S100_SYSROOT=evs_device_vendor_sdk/source/hobot-multimedia/debian/usr
```

构建：

```bash
./run.sh build s100    # 工具链文件自动注入（toolchains/toolchain-aarch64-linux-gnu.cmake）
                        # 也可 -DCMAKE_TOOLCHAIN_FILE=<你的-toolchain.cmake> 覆盖
```

验证产物：

```bash
ls out/s100/build/libshimetapi_*.so
file out/s100/build/samples/cpp/get_started/hv_sample_get_started  # 应为 ELF aarch64
# OpenCV 类样例（player / live_record_display）用 third_party/ 自带 aarch64 OpenCV，7/7 全编
```

#### X5（ARM MIPI，交叉编译）

X5 平台预编译库在 `lib/x5/`（aarch64）。前置：

```bash
# 1) aarch64 交叉工具链（同 S100 步骤，apt 装 g++-aarch64-linux-gnu 或 Arm GNU 11.3）
sudo apt-get install g++-aarch64-linux-gnu            # Ubuntu/Debian 系统编译器

# 2) X5 SDK 源码树（hobot-spdev/hobot-multimedia/hobot-multimedia-samples/hobot-camera）
#    默认期望 ../x5_sdk/RDK_X5（相对 toolkit 根目录），也可设 X5_SDK_ROOT 指向其他位置
git clone <X5_SDK_REPO> /path/to/RDK_X5
export X5_SDK_ROOT=/path/to/RDK_X5
```

构建：

```bash
./run.sh build x5      # aarch64 交叉；工具链与 SDK 已就绪即可
```

> `./run.sh` 自动探测 `../x5_sdk/RDK_X5`；缺失 X5_SDK_ROOT 时显式导出 `X5_SDK_ROOT=<path>`。
> 也可用 `-DCMAKE_TOOLCHAIN_FILE=<你的-toolchain.cmake>` 覆盖工具链。

产物布局同 s100：

```bash
ls out/x5/build/libshimetapi_*.so                                                # 预编译 4 个库已捆绑
file out/x5/build/samples/cpp/get_started/hv_sample_get_started                  # 应为 ELF aarch64
# OpenCV 类样例（player / live_record_display）用 third_party/ 自带 aarch64 OpenCV，7/7 全编
```

在自己的工程中链接（CMake）：

```cmake
# 把本仓库作为子目录，或 install 后使用
add_subdirectory(shimetapi_Hybrid_vision_toolkit)
target_link_libraries(your_app PRIVATE
HVToolkit::shimetapi_hv HVToolkit::shimetapi_codec HVToolkit::shimetapi_io)
```

安装到系统（头文件 + 当前架构的库）：

```bash
./run.sh install x86_64                          # 默认 /usr/local（需 sudo）
./run.sh install x86_64 /your/prefix             # 自定义 prefix
```

#### 查看支持的架构

```bash
./run.sh --list
# ARCH    STATUS        PREBUILT LIBS
# x86_64   ready         .../lib/x86_64
# s100     ready         .../lib/s100
# x5       ready         .../lib/x5
```

### 运行示例程序

构建产物在 `out/<arch>/build/samples/cpp/<name>/hv_sample_<name>`（7 个）。
采集类样例（get_started / callback / record / viewer）默认 USB 后端，
支持 `--mipi`（MIPI EVS-only）/ `--mipi-hvs`（MIPI 双 VC，S100 板上用）切换；
USB 模式可用前两个位置参数指定 VID/PID（默认 `0x1d6b 0x0105`）。
以下以 x86_64 为例，S100 上把路径换成 `out/s100/build`。

#### x86_64（USB）

```bash
# get_started — 最小采集
./out/x86_64/build/samples/cpp/get_started/hv_sample_get_started                 # 默认 0x1d6b:0x0105
./out/x86_64/build/samples/cpp/get_started/hv_sample_get_started 0x1d6b 0x0105   # 指定 VID PID
```

程序运行截图
![替代文本](assets/imgs/run02.png)

```bash
# callback — 双回调演示（采集 2 秒）
./out/x86_64/build/samples/cpp/callback/hv_sample_callback
# 输出：callback: events=N images=M

# record — 事件 + APS 混合录制（写 /tmp/hv_record.raw + .avi）
./out/x86_64/build/samples/cpp/record/hv_sample_record

# viewer — 实时采集解码计数
./out/x86_64/build/samples/cpp/viewer/hv_sample_viewer
# 输出：viewer: decoded N events

# bench_hw — USB 性能基准（默认 5 秒）
./out/x86_64/build/samples/cpp/bench_hw/hv_sample_bench_hw
./out/x86_64/build/samples/cpp/bench_hw/hv_sample_bench_hw 0x1d6b 0x0105 10   # 指定 VID PID 与时长
```

```bash
# player — 离线回放录制文件（需 OpenCV）
./out/x86_64/build/samples/cpp/player/hv_sample_player events.raw video.avi          # 默认 fps=30，速度 1.0
./out/x86_64/build/samples/cpp/player/hv_sample_player events.raw video.avi 60 2.0   # 指定帧率与速度
```

程序运行截图
![替代文本](assets/imgs/run05.jpg)

#### S100（MIPI HVS）

部署到板卡（`out/s100/build` 是**自包含**的——构建时已把 `lib/s100` 的
`libshimetapi_*.so` 捆绑进去，样例 rpath 用 `$ORIGIN` 相对路径，整个目录拷上板即可）：

```bash
# 宿主机
scp -r out/s100/build root@<板卡IP>:/app/
# 板卡上直接跑（无需 LD_LIBRARY_PATH）
/app/build/samples/cpp/get_started/hv_sample_get_started --mipi-hvs
```

> 板卡上**不需要**交叉工具链等构建环境 —— 那些只在编译主机上用。
> OpenCV 样例（player / live_record_display）还需把 `third_party/aarch64_opencv/lib/aarch64-linux-gnu`
> 拷到板上某目录并 `export LD_LIBRARY_PATH` 指向它。

运行示例：

```bash
# get_started — MIPI-HVS 最小采集
/app/build/samples/cpp/get_started/hv_sample_get_started --mipi-hvs

# record — MIPI-HVS 混合录制
/app/build/samples/cpp/record/hv_sample_record --mipi-hvs

# live_record_display — 实时预览 + 按键录制
/app/build/samples/cpp/live_record_display/hv_sample_live_record_display
# 按 r 开始/停止录制，按 q 退出；--no-display 无屏模式，--evs-prefix/--aps-prefix 指定录制前缀

# player — 离线回放录制文件
/app/build/samples/cpp/player/hv_sample_player events.raw aps.avi
```

### Python 示例

Python 绑定（单一 `hv_toolkit` 模块，**x86_64 预编译**，要求 Python 3.10）随 `lib/x86_64/python/` 分发：

```bash
# 最省事：先装库进系统路径，模块直接 import
sudo ./run.sh install x86_64
python3 samples/python/get_started.py

# 或不安装直接用（仓库根目录下，模块加 sys.path）
python3 -c "import sys; sys.path.insert(0, 'lib/x86_64/python'); import hv_toolkit; print(hv_toolkit.Camera)"
```

最小采集示例（USB）—— `frame.evs` 是原始事件字节，用 `Evt2Decoder` 解码：

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
        events = dec.decode(bytes(f.evs))   # → numpy 结构数组 (x, y, t, polarity)
        print(f"frame {f.frame_id} {f.width}x{f.height}: {len(events)} events, aps={f.aps.nbytes} bytes")
cam.stop_stream(); cam.destroy()
```

MIPI HVS 只换 backend 与解码器（`Frame.evs` 是 RAW8 子帧流，必须用 `MipiRaw8Decoder`）：

```python
cfg = hv.DeviceConfig()
cfg.backend      = hv.Backend.MipiHvs
cfg.sensor_index = 0       # 不用 VID/PID
cfg.evs_fps      = 500     # 帧率档（0=默认 240；可选 120/240/300/500/750/1000，Init 时生效）
cfg.i2c_bus      = 1
dec = hv.MipiRaw8Decoder()
```

完整 MIPI 样例见 [`samples/python/get_started_mipi.py`](samples/python/get_started_mipi.py)
（板卡运行；预编译 Python 模块是 x86_64 专属，S100 板上不可用）。

> **帧率档**：`cfg.evs_fps`（0=默认 240）。档位与 EVS 整包子帧数对应——120fps=16、240fps=32、300fps=40、500fps=64、750fps=100、1000fps=128 子帧；`MipiRaw8Decoder.decode` 不传 `subframe_count` 时自动按数据长度适配，任意档位无需手传。档位在 `Init` 时选定（MIPI 运行中切档需重建管线，不支持 `SetFrameRate`）。

> Python 模块目前导出 `Camera` / `DeviceConfig` / `Frame`（含 `evs`/`aps` 零拷贝 numpy 视图）/ `Evt2`、`Evt3`、`MipiRaw8` 编解码器 / `extract_evs_timestamp`。回调、`EventReader/Writer`、`HybridReader` 暂未导出 —— 需要时用 C++ API（见 [API.md](API.md)）。

### USB 设备权限

首次运行若报 `LIBUSB_ERROR_ACCESS`，推荐配置 udev 规则（免 sudo）：

```bash
echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="1d6b", ATTR{idProduct}=="0105", MODE="0666"' \
  | sudo tee /etc/udev/rules.d/99-hv-camera.rules
sudo udevadm control --reload-rules && sudo udevadm trigger
```

## 🚨 故障排除

### 无法找到 USB 设备 / LIBUSB_ERROR_ACCESS

如果出现 **no match devices found**，说明设备未成功连接。

![替代文本](assets/imgs/run01.jpg)

请检查 USB 设备是否连接、`vendor_id` / `product_id` 是否正确。

![替代文本](assets/imgs/code01.png)

配置正确且设备已连接，但运行报 **Cannot open device: LIBUSB_ERROR_ACCESS** —— 这是权限不足导致，执行下述指令即可：

```bash
# 临时：放开 USB 总线权限
sudo chmod -R 777 /dev/bus/usb/
# 推荐：见上文 udev 规则（免 sudo，持久）
```

### S100 交叉编译报 file in wrong format

`./run.sh build s100` 在未装 aarch64 工具链时会**提前报错**并给出安装指引（不会等到链接期）。
若自行用 cmake 且未传工具链，host 编译器会在链接期报
`libshimetapi_hv.so: file in wrong format` —— 解决：`apt install g++-aarch64-linux-gnu`
后用 `./run.sh build s100`，或显式传 `-DCMAKE_TOOLCHAIN_FILE`。

## 📁 项目结构

```
shimetapi_Hybrid_vision_toolkit/
├── CMakeLists.txt              # 预编译库接入配置（IMPORTED 目标 + 示例 + 安装规则）
├── run.sh                      # 一站式入口（build/install/samples/pydeploy/--list）
├── README.md / README_EN.md    # 项目文档（中/英）
├── API.md / API_EN.md          # 公有 API 参考（中/英）
├── include/shimetapi/          # 公有头文件
│   ├── core/                   # EventCD / Status / BufferPool / Frame / PixelFormat / Timestamp
│   ├── hv/                     # Camera / DeviceConfig / EventFormat / EventPacket / ImageData
│   ├── codec/                  # EVT2 / EVT3 / MIPI RAW8 编解码
│   └── io/                     # EventReader / EventWriter / HybridWriter / HybridReader
├── lib/                        # 预编译库（闭源二进制）
│   ├── x86_64/                 # x86_64：USB + Ethernet 后端（含 python/ 绑定模块）
│   └── s100/                   # S100 aarch64：MIPI + Ethernet 后端
├── toolchains/                 # 交叉工具链文件（aarch64-linux-gnu）
├── third_party/                # aarch64 OpenCV（交叉编 OpenCV 类示例用）
├── samples/                    # 示例
│   ├── cpp/                    # C++ 示例（7 个）
│   └── python/                 # Python 示例
└── docs/                       # 板端验证步骤与冒烟记录
```

## 🔍 示例程序说明

| 样例 | 用途 | 后端 | 命令速查 |
|---|---|---|---|
| `get_started` | 最小采集（同步 GetFrame） | USB / `--mipi` | `hv_sample_get_started [vid pid] [--mipi]` |
| `callback` | 事件 + APS 异步回调 | USB / `--mipi` / `--mipi-hvs` | `hv_sample_callback [--mipi-hvs]` |
| `record` | EVS+APS 混合录制到 /tmp | USB / `--mipi` / `--mipi-hvs` | `hv_sample_record [--mipi-hvs]` |
| `viewer` | 实时采集 + 解码计数 | USB / `--mipi` / `--mipi-hvs` | `hv_sample_viewer [--mipi]` |
| `bench_hw` | USB 实机吞吐基准 | USB | `hv_sample_bench_hw [vid pid duration_s]` |
| `live_record_display` | MIPI-HVS 实时预览 + 录制（OpenCV） | MipiHvs | `hv_sample_live_record_display [--no-display] [--evs-prefix s] [--aps-prefix s]` |
| `player` | 离线回放 .raw + .avi（OpenCV） | 离线 | `hv_sample_player <events.raw> <video.avi> [fps] [speed]` |

详细说明：

- **get_started**：基础入门，Init → StartStream → GetFrame 10 帧，打印每帧 evs 字节数。学习 HV Toolkit 的最佳起点。
- **callback**：`SetEventCallback` / `SetImageCallback` 双异步回调演示，采集 2 秒后打印计数。
- **record**：`HybridWriter` 把 10 帧写入 `/tmp/hv_record.raw`（EVS）+ `/tmp/hv_record.avi`（APS）。
- **viewer**：拉流并按后端自动选解码器（USB=EVT2，MIPI=MipiRaw8），打印累计解码事件数。
- **bench_hw**：USB 实机计时基准（默认 `0x1d6b:0x0105`，5 秒），输出 Mev/s 与 APS fps。
- **live_record_display**：MIPI-HVS 双 VC 实时预览（左 EVS 可视化 / 右 APS）+ `r` 键录制，`HybridWriter` 落盘。
- **player**：`HybridReader` + `MipiRaw8Decoder` 回放录制文件，带 GUI 按钮（播放/暂停/步进/变速/同步）。

## 📄 版权声明

版权所有 © ShiMetaPi。本仓库以预编译二进制形式分发 HV Toolkit 运行库；头文件与示例代码供集成开发使用。未经书面许可，不得反向工程、反汇编库文件或再分发其中的二进制组件。EVT2/EVT3 编解码为基于公开规范的独立实现（clean-room）。

---

## 🙋 联系我们

如果你在使用 HV Toolkit 过程中遇到任何问题或有任何建议，欢迎通过以下方式与我们联系：

开源硬件网站：https://www.shimetapi.cn （国内） / https://www.shimetapi.com （海外）
在线技术文档：https://forum.shimetapi.cn/wiki/zh/
在线技术社区：https://forum.shimetapi.cn

**HV Toolkit** - 让事件相机开发更简单 🚀
