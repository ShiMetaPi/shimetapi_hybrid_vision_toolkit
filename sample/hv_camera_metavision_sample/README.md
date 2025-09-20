# HV Camera Openeb Sample Demo

## 功能概述

`hv_camera_metavision_sample` 是一个展示如何将 HV Camera SDK 与 Openeb SDK 集成使用的示例程序。该demo演示了如何使用 Openeb SDK 的算法来处理事件相机数据，包括事件数据的处理、可视化和显示。

## 主要功能

### 1. Openeb SDK 集成
- **周期性帧生成**: 使用 `PeriodicFrameGenerationAlgorithm` 将事件数据转换为可视化帧
- **图像处理算法**: 使用 `FlipXAlgorithm` 对事件数据进行X轴翻转处理
- **事件数据处理**: 展示如何使用 Openeb SDK 的算法链处理事件流

## 使用方法

### 编译
```bash
cd /path/to/shimetapi_hybrid_camera_toolkit/sample/hv_camera_metavision_sample
mkdir build && cd build
cmake ..
make
```

### 运行
```bash
# 使用默认设备ID
./hv_camera_metavision_sample

# 指定USB设备ID（十六进制格式）
./hv_camera_metavision_sample 1d6b 0105
```

### 参数说明
- `vendor_id`: USB厂商ID（十六进制，可选）
- `product_id`: USB产品ID（十六进制，可选）
- 默认值: vendor_id=0x1d6b, product_id=0x0105

### 操作说明
- **退出程序**: 按 `Ctrl+C`、`ESC` 键或 `Q` 键
- **窗口操作**: 可以移动和调整显示窗口大小

## 依赖要求

### 必需库
- **HV Camera SDK**: 相机控制和数据采集
- **Openeb SDK**: 事件数据处理算法
- **OpenCV**: 图像处理和显示
- **C++17**: 编译器支持

## 注意事项

1. **设备连接**: 确保HV相机正确连接到USB 3.0接口
2. **权限设置**: Linux下可能需要设置USB设备访问权限
3. **Openeb SDK**: 确保正确安装和配置Openeb SDK
4. **内存使用**: 长时间运行时注意内存使用情况
5. **算法参数**: 可根据需要调整累积时间和帧率参数

## 故障排除

### 常见问题
1. **相机打开失败**: 检查设备连接和USB ID设置
2. **Openeb算法错误**: 确认SDK版本兼容性
3. **显示窗口问题**: 检查OpenCV安装和显示环境
4. **性能问题**: 调整算法参数或检查系统资源