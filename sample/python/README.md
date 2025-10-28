# HV Toolkit Python 示例程序

## 📦 模块说明

所有 Python 模块已编译并存放在 `../../lib/Python/` 目录：
- `hv_camera_python.so` - 相机控制模块
- `hv_evt2_codec_python.so` - EVT2 编解码模块  
- `hv_event_reader_python.so` - 事件读取模块
- `hv_event_writer_python.so` - 事件写入模块

## 🚀 运行示例

### 1. 编解码器测试 (无需硬件)
```bash
cd hv_toolkit_encoder/
python3 test_codec.py
```

### 2. 相机入门示例 (需要相机)
```bash
cd hv_toolkit_get_started/
python3 hv_toolkit_get_started.py
```

### 3. 事件读取示例 (需要事件文件)
```bash
cd hv_toolkit_reader_test/
python3 hv_toolkit_reader_test.py <event_file.raw>
```

### 4. 录制示例 (需要相机)
```bash
cd hv_toolkit_record/
python3 recode_test.py
```

## ✅ 所有示例程序已配置完成，可直接运行！
