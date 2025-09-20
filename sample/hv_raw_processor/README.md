# Raw数据处理器

这是一个独立的C++程序，用于处理从下位机获取的raw数据文件。它实现了与`HV_Camera::processEventData`相同的处理逻辑，可以将raw数据转换为事件数据。

## 依赖要求

### 必需依赖
- C++17兼容的编译器
- CMake 3.10+
- OpenCV

### 可选依赖
- Metavision SDK (如果没有安装，程序会尝试使用头文件)

## 编译方法

### Linux/macOS
```bash
# 给脚本执行权限
chmod +x build.sh

# 运行编译脚本
./build.sh
```

### 手动编译
```bash
# 创建构建目录
mkdir build
cd build

# 复制文件
cp ../CMakeLists.txt ./CMakeLists.txt
cp ../hv_raw_data_processor.cpp ./

# 配置和编译
cmake . -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)  # Linux/macOS
# 或
cmake --build . --config Release  # Windows
```

## 使用方法

### 基本用法
```bash
# 只处理数据，不输出文件
./hv_raw_data_processor data.raw

# 处理数据并输出CSV文件
./hv_raw_data_processor data.raw events.csv
```

### 参数说明
- `<raw_file_path>` - 输入的raw数据文件路径（必需）
- `[output_csv_file]` - 输出的CSV文件路径（可选）

### 输出格式
如果指定了输出文件，程序会生成CSV格式的事件数据：
```csv
x,y,polarity,timestamp
100,200,1,1234567
150,300,0,1234568
...
```

## 常见问题

### 编译错误
1. **找不到Metavision SDK**: 程序会尝试在常见路径查找头文件，如果仍然失败，请确保安装了Metavision SDK或将头文件放在正确位置。

2. **OpenCV未找到**: 请确保已正确安装OpenCV并设置了环境变量。

### 运行时错误
1. **文件无法打开**: 检查raw文件路径是否正确，文件是否存在。

2. **内存不足**: 对于非常大的文件，确保系统有足够的内存。

## 数据验证

这个程序的主要用途是验证从下位机获取的raw数据。通过与原始`HV_Camera`处理结果对比，可以：

1. 验证USB传输数据的完整性
2. 分析时间戳跳跃问题
3. 检查事件数据的正确性
4. 性能基准测试

## 示例输出

```
Raw数据处理器
输入文件: data.raw
输出文件: events.csv
开始处理...
文件大小: 52428800 字节
预计数据块数量: 100
已处理 100 个数据块, 总事件数: 12345, 耗时: 1500ms

处理完成!
总数据块数: 100
总事件数: 12345
总耗时: 1500ms
平均处理速度: 66.67 块/秒
事件数据已保存到: events.csv
```
