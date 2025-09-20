# HV EVS Recorder - EVS数据录制库

这是一个专门用于EVS数据录制的库，基于原有的`hv_camera.cpp`程序简化而来。该库只负责USB数据传输和raw文件保存，不进行事件处理，适用于需要原始数据采集的场景。可配合hv_raw_processor查看数据时间戳验证数据完整性。

## 编译要求

- CMake 3.10+
- C++14编译器
- libusb-1.0开发库
- pthread库

## 编译方法

```bash
# 创建构建目录
mkdir build
cd build

# 配置和编译
cmake ..
make

# 或者使用ninja（如果可用）
cmake -G Ninja ..
ninja
```

## 使用方法

### 基本用法

```bash
# 录制到默认文件 evs_data.raw
./hv_evs_recorder_sample

# 指定输出文件名
./hv_evs_recorder_sample my_evs_data.raw

# 指定输出文件名和录制时长（秒）
./hv_evs_recorder_sample my_evs_data.raw 60
```

### 程序参数

- `参数1`: 输出文件名（可选，默认为 `evs_data.raw`）
- `参数2`: 录制时长（秒，可选，默认为无限录制）

### 停止录制

- 按 `Ctrl+C` 停止录制
- 如果指定了录制时长，程序会自动停止

## 配置设备ID

在使用前，请确保在代码中设置了正确的USB设备ID：

```cpp
const uint16_t VENDOR_ID = 0x1d6b;   // 替换为实际的厂商ID
const uint16_t PRODUCT_ID = 0x0105;  // 替换为实际的产品ID
```

可以使用 `lsusb`（Linux）或设备管理器（Windows）查看设备ID。

## 输出格式

录制的raw文件包含原始的USB传输数据，格式与原始EVS数据流一致。每个数据包的结构：

- 数据包大小: `HV_BUF_LEN` (4096 * 128 = 524288 字节)
- 包含多个子帧数据
- 每个子帧大小: `HV_SUB_FULL_BYTE_SIZE` (32768 字节)

## 故障排除

### 常见问题

1. **设备打开失败**
   - 检查USB设备是否已连接
   - 确认设备ID是否正确
   - 检查USB驱动是否正确安装
   - 确认程序有足够的USB访问权限
   - sudo chmod -R 777 /dev/bus/usb/

2. **录制失败**
   - 检查输出目录是否有写入权限
   - 确认磁盘空间是否充足
   - 检查文件名是否合法

### 调试信息

程序运行时会输出详细的统计信息：

- USB传输时间
- 数据传输量
- 帧率统计
- 平均/最小/最大传输时间

