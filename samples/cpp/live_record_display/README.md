# live_record_display —— MIPI-HVS 实时预览与按键录制

**语言**: **中文**

`live_record_display` 是 HV Toolkit 的 MIPI-HVS 后端示例程序，提供 **实时预览**（EVS 事件可视化 + APS 图像）与 **按键开关录制**（EVS `.raw` + APS `.avi` 双流同时输出）。基于 toolkit 统一 `Camera` API 实现，仅需 OpenCV 作显示窗口。

## 功能特性

- **实时预览**：OpenCV 窗口，左侧 EVS 事件可视化（累积衰减，白色=ON、橙色=OFF），右侧 APS 图像（NV12 → BGR，缩放到一半宽度）
- **按键录制**：按 `r` 开始录制，再次按 `r` 停止，每次录制生成带时间戳的新文件名
- **双流输出**：EVS 事件写入 `.raw` 文件，APS 图像写入 `.avi`（NV12 AVI 容器）
- **显示开关**：按 `d` 切换显示更新（录制不受影响）
- **控制台模式**：`--no-display` 支持无 GUI 环境，通过 stdin 交互

## 按键操作

| 按键 | 功能 |
| --- | --- |
| `r` | 开始 / 停止录制 |
| `d` | 开启 / 关闭显示更新 |
| `q` / `ESC` | 退出程序 |

## 命令行参数

```
Usage: hv_sample_live_record_display [OPTIONS]

Options:
  --no-display         禁用 OpenCV 窗口，使用控制台交互
  --evs-prefix <str>   EVS 录制文件前缀 (default: live_events)
  --aps-prefix <str>   APS 录制文件前缀 (default: live_video)
  -h, --help           显示帮助
```

## 传感器配置

本示例固定使用 MIPI-HVS 双 VC 后端，传感器配置如下：

| 通道 | 传感器 | 分辨率 | 帧率 | 配置文件 |
| --- | --- | --- | --- | --- |
| **VC0 (EVS)** | apx003cc-hvs-evs-4096x256-raw8-240fps-4lane-vc0 | 4096×256（RAW8） | 240 fps | `hvs_aps_binning_evs_240fps_4lane_evs_vc0.c` |
| **VC1 (APS)** | apx003cc-hvs-aps-1632x1224-raw10-30fps-4lane-vc1 | 1632×1224（RAW10 → PYM NV12 768×608） | 30 fps | `hvs_aps_binning_evs_240fps_4lane.c` |

### 修改帧率配置

帧率由 RDK 平台传感器配置文件决定，toolkit 运行时通过 `vp_sensor_config_list` 自动匹配配置文件名。如需修改帧率，需：

1. 在 RDK 板端传感器配置目录中找到对应的 `.c` 配置文件（通常位于 RDK sysroot 的 `vp_sensors` 目录下）
2. 修改配置文件中的帧率参数（如 `hvs_aps_binning_evs_240fps_4lane_evs_vc0.c` 中的 EVS 帧率，`hvs_aps_binning_evs_240fps_4lane.c` 中的 APS 帧率）
3. 重新编译 toolkit（`./run.sh build <platform>`）使新配置生效

> **注意**：帧率受硬件能力限制。EVS 240 fps 和 APS 30 fps 是 apx003 传感器在当前配置下的典型值，修改前请确认传感器规格。

## 构建

### 板端本地编译（S100 / RK3588）

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_SAMPLES=ON \
      -DENABLE_MIPI_BACKEND=ON \
      -DENABLE_USB_BACKEND=OFF
cmake --build build -j
```

### 交叉编译（S100）

```bash
export PATH="/opt/arm-gnu-toolchain-11.3.rel1-x86_64-aarch64-none-linux-gnu/bin:$PATH"
./run.sh build s100
```

交叉编译时自动使用 `third_party/aarch64_opencv/` 下的 aarch64 OpenCV 4.5.4。

## 运行

```bash
# GUI 模式（需显示器）
./build/samples/cpp/live_record_display/hv_sample_live_record_display

# 控制台模式（无显示器）
./build/samples/cpp/live_record_display/hv_sample_live_record_display --no-display

# 自定义输出前缀
./build/samples/cpp/live_record_display/hv_sample_live_record_display \
  --evs-prefix /data/evs_record --aps-prefix /data/aps_record
```

## 依赖

| 依赖 | 用途 | 必需 |
| --- | --- | --- |
| **OpenCV** 4.x | 显示窗口（`highgui`）、图像处理（`imgproc`、`core`） | MIPI-HVS 后端 |
| **HV Toolkit** | `shimetapi_hv`、`shimetapi_codec`、`shimetapi_io` | 是 |

## 输出文件示例

```
live_events_20260804_143025_300.raw   ← EVS 事件数据
live_video_20260804_143025_300.avi    ← APS NV12 视频
```

录制时自动生成带时间戳的文件名，每次按 `r` 开始新录制均生成新文件。