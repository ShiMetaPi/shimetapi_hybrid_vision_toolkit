# HV Camera Frequency Demo

## 功能描述

这个示例程序结合了HV相机的实时显示和频率计算功能，提供了一个交互式的界面来控制数据采集和频率计算。

## 操作说明

### 键盘控制

- **R键**：开始/停止频率计算
- **ESC键** 或 **Q键**：退出程序
- **Ctrl+C**：强制退出程序

### 显示界面

程序会打开一个窗口：
- **EVS Data**：显示事件相机数据

每个窗口左上角会显示：
- 频率计算状态（"Freq: 0.00 Hz" 或 "ROI: 0.00Hz"）

## 编译和运行

### 编译

```bash
# 创建构建目录
mkdir build
cd build

# 配置和编译
cmake ..
make
```
或者可以使用hv_camera_frequency目录下的`build.sh`脚本进行编译
也可以选择sample文件夹下`build.sh`脚本编译所有demo

### 运行

```bash
# 使用默认USB设备ID
./hv_camera_frequency

# 或指定USB设备ID（十六进制）
./hv_camera_frequency 1d6b 0105
```

## 依赖项

- OpenCV 4.x
- Metavision SDK
- HV Camera Toolkit
- C++17 编译器

## 注意事项

1. 确保HV相机已正确连接并安装驱动，若出现无法打开HV相机，尝试命令 `sudo chmod 777 /dev/video*` 后重新启动程序
2. 请确保USB为3.0以上版本，Ubuntu可使用`lsusb`命令查看USB版本，使用USB2.0可能导致丢帧。
3. 虚拟机使用USB3.0时可能出现蓝屏的情况，尽量使用Ubuntu主机连接相机
4. 录制过程中避免频繁开关录制功能，以免影响数据完整性
5. 若evs窗口出现画面不同步的情况，是因为evs数据处理还没跟上，请等待画面稳定或重启相机
6. 若终端出现`Event queue full, dropping oldest data`，可等待队列提示信息不再打印，实时显示画面稳定时再开启录制，或者直接给相机重新上电，防止丢帧。
7. 长时间录制时注意磁盘空间
8. 确保没有其他程序占用相机设备

## 故障排除

### 常见问题

1. **无法打开相机**
   - 检查USB连接
   - 确认设备ID是否正确
   - 检查是否有权限访问USB设备

2. **编译错误**
   - 确认所有依赖库已正确安装
   - 检查CMakeLists.txt中的路径设置

3. **录制文件无法创建**
   - 检查当前目录的写入权限
   - 确认磁盘空间充足


