# S100 MIPI 板上验证手册（手动冒烟测试）

> HV Toolkit v2.0 S100 平台 MIPI 后端板级验证。涵盖 EVS-only（`Backend::Mipi`）和双 VC 模式（`Backend::MipiHvs`，APS+EVS）的全部 sample 验证步骤。

## 前置条件

### 硬件

- **S100 开发板**（aarch64，Horizon Robotics J6 系列），板载 apx003 HVS 传感器接入 MIPI-CSI 接口。
- 传感器型号：**apx003cc**（HVS 双 VC 模式：VC0 EVS raw8 + VC1 APS NV12）。

### 主机（x86_64 开发机）

- **S100 交叉工具链**已安装：`aarch64-none-linux-gnu-gcc` 在 `PATH` 中。
  - arm-gnu-toolchain-11.3 rel1，tarball 位于 `toolchains/s100/arm-gnu-toolchain-11.3.rel1-x86_64-aarch64-none-linux-gnu.tar.xz`。
  - 安装步骤见 `toolchains/s100/README.md`（解压到 `/opt/` 并 `export PATH=/opt/s100-toolchain/bin:$PATH`）。
- **S100 sysroot 可用**：`S100_SYSROOT` 环境变量指向 SDK 路径。
- 验证工具链就绪：
  ```bash
  aarch64-none-linux-gnu-gcc --version
  ./run.sh --list          # s100 状态应为 "ready"
  ```

### 目标板（S100）

- 运行 S100 官方系统镜像（含 RDK 运行时库 `libcam`、`libvpf`、`libhbmem`、`libalog`、`libcjson` 等）。
- 可通过 `scp` 或 U 盘将构建产物拷贝到板端。
- 板端有 `ssh` 终端可用。

---

## 第一步：构建 S100 ARM 产物

在主机上执行：

```bash
cd shimetapi_Hybrid_vision_toolkit

# 设置 sysroot（指向 S100 SDK 中 hobot-multimedia-dev 目录）
export S100_SYSROOT=$(realpath ../s100_sdk/platform_source_code_20260513145949/source/hobot-multimedia/debian/usr)

# 构建（MIPI 后端 ON，USB 后端 OFF）
./run.sh build s100
```

构建成功后产物位于 `out/s100/build/`：

| 产物 | 路径 |
|------|------|
| `libshimetapi_core.so.2.0.0` | `out/s100/build/libshimetapi_core.so`* |
| `libshimetapi_codec.so.2.0.0` | `out/s100/build/libshimetapi_codec.so`* |
| `libshimetapi_io.so.2.0.0` | `out/s100/build/libshimetapi_io.so`* |
| `libshimetapi_hv.so.2.0.0` | `out/s100/build/libshimetapi_hv.so`* |
| `hv_sample_get_started` | `out/s100/build/samples/cpp/get_started/hv_sample_get_started` |
| `hv_sample_callback` | `out/s100/build/samples/cpp/callback/hv_sample_callback` |
| `hv_sample_record` | `out/s100/build/samples/cpp/record/hv_sample_record` |
| `hv_sample_viewer` | `out/s100/build/samples/cpp/viewer/hv_sample_viewer` |
| `hv_sample_bench` | `out/s100/build/samples/cpp/bench/hv_sample_bench` |
| `hv_sample_bench_hw` | `out/s100/build/samples/cpp/bench_hw/hv_sample_bench_hw` |

> \* 实际是带版本号的 `.so.2.0.0` 文件，source 目录下的 `.so` 是符号链接。拷贝时注意带上目标文件（`cp -L` 或 `cp -a`）。

验证产物是 ARM 二进制：

```bash
file out/s100/build/samples/cpp/get_started/hv_sample_get_started
# 预期: ELF 64-bit LSB executable, ARM aarch64, ...
```

---

## 第二步：部署到 S100 板端

将构建产物拷贝到板端：

```bash
# 在主机上执行
BOARD_IP=<S100_板端_IP>
ssh root@$BOARD_IP "mkdir -p /tmp/hv_test"
scp -r out/s100/build/libshimetapi_*.so.* out/s100/build/samples root@$BOARD_IP:/tmp/hv_test/
```

在板端创建运行脚本 `run_hv.sh`：

```bash
# 在板端执行
cat > /tmp/hv_test/run_hv.sh << 'EOF'
#!/bin/bash
export LD_LIBRARY_PATH=/tmp/hv_test:$LD_LIBRARY_PATH
SAMPLES_DIR=/tmp/hv_test/samples/cpp
EOF
chmod +x /tmp/hv_test/run_hv.sh
source /tmp/hv_test/run_hv.sh
```

---

## 第三步：验证 EVS-only 模式（`Backend::Mipi`）

EVS-only 模式使用 `sensor_index` 定位单个 LINEAR sensor 配置，走单 VC 管线（VIN raw8），只输出 EVS 事件。验证时用 `--mipi` 参数。

### 3.1 get_started（EVS-only MIPI）

```bash
cd /tmp/hv_test && \
LD_LIBRARY_PATH=$(pwd):$LD_LIBRARY_PATH \
./samples/cpp/get_started/hv_sample_get_started --mipi
```

**通过标准：**
- 输出 `using MIPI backend (sensor_index=9)`，无 crash。
- 每帧输出 `frame N evs=XXXXXX bytes` 且 `evs.size > 0`。
- 正常情况下 10 帧全部返回数据。

**如果 Init 失败：**
- `sensor_index=9` 可能不对应板端的 apx003 LINEAR 配置 → 查看 `vp_sensor_config_list` 中 `linear_4096x256_raw8_30fps_4lane.c`（或对应的 linear 配置）的实际索引，修改 `get_started/main.cpp` 中的 `cfg.sensor_index` 并重新构建。

### 3.2 callback（EVS-only MIPI）

```bash
cd /tmp/hv_test && \
LD_LIBRARY_PATH=$(pwd):$LD_LIBRARY_PATH \
./samples/cpp/callback/hv_sample_callback --mipi
```

**通过标准：**
- 输出 `callback: events=N images=0`（APS 未实现，`images` 恒为 0，因为 EVS-only 模式下 `readImageFrame` 返回 `ErrUnsupportedFormat`）。
- `events > 0`（2 秒内 EVS 事件回调正常触发）。
- 无 crash。

### 3.3 viewer（EVS-only MIPI，RAW8 解码验证）

```bash
cd /tmp/hv_test && \
LD_LIBRARY_PATH=$(pwd):$LD_LIBRARY_PATH \
./samples/cpp/viewer/hv_sample_viewer --mipi
```

**通过标准：**
- 输出 `viewer: decoded NNNNNN events` 且 `NNNNNN > 0`。
- 证明 `MipiRaw8Decoder` 在板端能正确将 EVS raw8 子帧解码为 `EventCD` 事件。

### 3.4 record（EVS-only MIPI，RAW 写入）

```bash
cd /tmp/hv_test && \
LD_LIBRARY_PATH=$(pwd):$LD_LIBRARY_PATH \
./samples/cpp/record/hv_sample_record --mipi
```

**通过标准：**
- 输出 `record: wrote /tmp/hv_record.raw`。
- `/tmp/hv_record.raw` 文件存在且大小 > 0。
- 文件可被 `EventReader` 回读（可选：把文件拷回主机用 `hv_sample_viewer` 验证）。

---

## 第四步：验证 HVS 双 VC 模式（`Backend::MipiHvs`）

HVS 模式使用两个 sensor 配置 `hvs_aps_binning_evs_240fps_4lane_evs_vc0.c`（VC0 EVS）和 `hvs_aps_binning_evs_240fps_4lane.c`（VC1 APS），双 VC 同时工作。验证时用 `--mipi-hvs` 参数。

### 4.1 get_started（HVS）

```bash
cd /tmp/hv_test && \
LD_LIBRARY_PATH=$(pwd):$LD_LIBRARY_PATH \
./samples/cpp/get_started/hv_sample_get_started --mipi-hvs
```

> **注意**：当前 `get_started` 未内置 `--mipi-hvs` 支持（仅支持 `--mipi`）。如需验证，需先修改 `samples/cpp/get_started/main.cpp` 加入 `--mipi-hvs` 分支，或直接用下面 callback/record/viewer 验证 HVS。

### 4.2 callback（HVS — 主要验证点）

这是 HVS 模式的核心验证：

```bash
cd /tmp/hv_test && \
LD_LIBRARY_PATH=$(pwd):$LD_LIBRARY_PATH \
./samples/cpp/callback/hv_sample_callback --mipi-hvs
```

**通过标准（关键）：**
- 输出 `callback: events=N images=M` 且 **`N > 0` 且 `M > 0`**。
  - `events > 0`：VC0 EVS 管线正常出数。
  - `images > 0`：VC1 APS 管线正常出数（ISP→YNR→PYM → NV12 帧），证明 `readImageFrame` 不再是 stub。
- 输出日志中应有：
  - `MIPI-HVS: VC0 started: sensor=..., config_file=hvs_aps_binning_evs_240fps_4lane_evs_vc0.c`
  - `MIPI-HVS: VC1 APS started: sensor=..., config_file=hvs_aps_binning_evs_240fps_4lane.c`
  - `MIPI-HVS: VC1 APS ISP started successfully.`（或 `ISP bypass is active`）
- 无 crash，2 秒后正常退出。

**如果 Init 失败：**
1. 确认 `vp_sensor_config_list` 中存在两个配置 `hvs_aps_binning_evs_240fps_4lane_evs_vc0.c` 和 `hvs_aps_binning_evs_240fps_4lane.c`（代码按文件名查找，不依赖索引）。
2. 确认 I2C 总线匹配：`DeviceConfig.i2c_bus` 默认为 1。若板端安全芯片不在 I2C-1，需修改 `callback/main.cpp` 中 HVS 分支的 `cfg.i2c_bus` 值。
3. 确认 GPIO 502/503 对应板端的相机 GPIO（若板端 GPIO 编号不同，需修改 `mipi_hvs_device_impl.cpp` 中的 `kCameraGpio0`/`kCameraGpio1`）。

### 4.3 viewer（HVS，EVS 解码 + 时间戳检查）

```bash
cd /tmp/hv_test && \
LD_LIBRARY_PATH=$(pwd):$LD_LIBRARY_PATH \
./samples/cpp/viewer/hv_sample_viewer --mipi-hvs
```

**通过标准：**
- 输出 `viewer: decoded NNNNNN events` 且 `NNNNNN > 0`。
- 证明 HVS 模式下 `MipiRaw8Decoder` 解码 EVS raw8 正常。

### 4.4 record（HVS，EVS 录制）

```bash
cd /tmp/hv_test && \
LD_LIBRARY_PATH=$(pwd):$LD_LIBRARY_PATH \
./samples/cpp/record/hv_sample_record --mipi-hvs
```

**通过标准：**
- 输出 `record: wrote /tmp/hv_record.raw`。
- `/tmp/hv_record.raw` 文件存在且大小 > 0。

---

## 第五步：稳定性验证

### 5.1 长时间浸泡（soak test）

```bash
# 修改 get_started main.cpp 循环次数从 10 改为 1000，或用 callback 长时间运行
# 在板端运行 callback 至少 5 分钟
cd /tmp/hv_test && \
LD_LIBRARY_PATH=$(pwd):$LD_LIBRARY_PATH \
timeout 300 ./samples/cpp/callback/hv_sample_callback --mipi-hvs
```

**通过标准：**
- 5 分钟内无 crash、无内存持续增长（另开终端用 `top`/`htop` 观察 `/tmp/hv_test` 下的进程）。
- 正常退出（timeout 后 `StopStream` → `Destroy` 路径无卡死）。
- `events` 和 `images` 的值合理（与 2 秒运行的数值按比例缩放一致）。

### 5.2 多次重启

连续运行 5 次 callback：

```bash
cd /tmp/hv_test && \
for i in $(seq 1 5); do
  echo "=== Run $i ===" && \
  LD_LIBRARY_PATH=$(pwd):$LD_LIBRARY_PATH \
  ./samples/cpp/callback/hv_sample_callback --mipi-hvs || break
done
```

**通过标准：**
- 每次运行 `events > 0` 且 `images > 0`。
- 每次 Init→StartStream→StopStream→Destroy 完整走完，无残留资源（`cam_fd`/`vflow_fd`/`vin_node_handle`/`pym_node_handle` 全部关闭）。

---

## 通过标准总表

| 步骤 | 用例 | 关键指标 | 判定 |
|------|------|----------|------|
| 3.1 | EVS get_started | `evs.size > 0` 所有 10 帧 | ✅ |
| 3.2 | EVS callback | `events > 0`, `images = 0` | ✅ |
| 3.3 | EVS viewer | `decoded > 0` events | ✅ |
| 3.4 | EVS record | `/tmp/hv_record.raw` 大小 > 0 | ✅ |
| **4.2** | **HVS callback** | **`events > 0` 且 `images > 0`** | ✅ **核心** |
| 4.3 | HVS viewer | `decoded > 0` events | ✅ |
| 4.4 | HVS record | `/tmp/hv_record.raw` 大小 > 0 | ✅ |
| 5.1 | 浸泡 5 分钟 | 无 crash，无内存泄漏 | ✅ |
| 5.2 | 5 次重启 | 每次 HVS callback 正常 | ✅ |

---

## 常见问题排查

### Init 失败：`ErrDeviceNotFound`

- 检查 MIPI 相机排线是否插好。
- 确认 `vp_sensor_config_list` 包含所需的 sensor 配置文件（HVS 需要 VC0 + VC1 两个文件都在列表中）。
- 检查 GPIO 初始化（502/503）—— 板端 sysfs GPIO 编号是否与此一致。

### Init 失败：`ErrPermissionDenied`

- S100 的 rjgt102 安全芯片 I2C 总线与 RK3588 不同 → 检查 `DeviceConfig.i2c_bus` 的值。
- 尝试不认证直接启动（注释掉 `common::authenticate` 调用，仅限调试）。

### readImageFrame 返回 `ErrUnsupportedFormat`（HVS 下）

- 确认使用了 `--mipi-hvs`（`Backend::MipiHvs`），而非 `--mipi`（`Backend::Mipi`）。
- 检查 VC1 PYM pipeline 是否正确初始化（看 stderr 是否有 `MIPI-HVS: VC1 APS started` 日志）。

### 事件解码为 0

- 检查 `MipiRaw8Decoder` 的输入数据是否有效（`f.evs.size > 0` 但 decoder 返回 0 → 数据格式不对或 sensor 输出非 RAW8）。
- EVS-only 和 HVS 的 EVS 管线输出都是 RAW8 格式，`MipiRaw8Decoder` 对两种模式通用。
