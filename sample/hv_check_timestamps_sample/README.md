# CSV 时间戳分析工具

Toolkit SDK生成的csv数据庞大，时间戳太多难以分析，本程序用于分析CSV文件中时间戳数据，检测时间间隔异常和数据异常。每两个相邻的不同的时间戳之间的差值应该是基本相近的。

## 功能特性

- 📊 **时间戳分析**：读取使用Toolkit SDK处理后的CSV文件时间戳数据
- 🔍 **间隔检测**：自动检测大于1000μs以及小于50μs的时间间隔
- 📈 **数据统计**：生成完整的时间戳差值分析报告
- 📄 **报告输出**：将分析结果保存到timestamps.txt文件

## 系统要求

- C++17 或更高版本的编译器
- 支持标准库的C++环境

## 编译方法

```bash
./build.sh
```

## 使用方法

1. **运行程序**：

   ```bash
   cd ./build
   ./hv_check_timestamps_sample  <input_file_name>.csv
   ```
2. **查看结果**：
   程序会在当前目录生成 `timestamps.txt`文件，包含完整的分析报告。

## CSV文件格式要求

可以利用目录下metavision_evt2_raw_file_decoder将raw数据解码成csv文件

Usage : ./metavision_evt2_raw_file_decoder INPUT_FILENAME CD_OUTPUTFILE (TRIGGER_OUTPUTFILE)

比如

./metavision_evt2_raw_file_decoder ../hv_camera_live_record/build/live_events_20250913_172536_083.raw ./output.csv

- 文件格式：逗号分隔值(CSV)
- 时间戳位置：第四列
- 支持的行：
  - 普通数据行
  - 空行（会被跳过）
  - 以 `%`开头的注释行（会被跳过）

### 示例CSV格式

```csv
% 这是注释行
data1,data2,data3,1234567890,data5
data1,data2,data3,1234568000,data5
data1,data2,data3,1234590121,data5
```

## 输出报告说明

生成的 `timestamps.txt`文件包含三个部分：

### 1. 大间隔检测

```
Large gaps (> 1000 μs) detected:
Row,Gap (seconds)
3,22121
```

显示时间间隔超过1000μs的记录及其行号。

### 2. 异常数值检测

```
Rows where 4th column < 50:
  5
  12
Maximum row index among them: 12
```

列出时间戳差值小于50的所有行号，并显示最大行号。

### 3. 完整时间戳列表

```
Index,Timestamp,Difference (seconds)
1,1234567890,N/A
2,1234568000,110
3,1234590121,1001
```

显示所有时间戳的排序列表及相邻时间戳的差值。

## 程序特点

- **自动去重**：相同的时间戳会被自动去除
- **自动排序**：时间戳按升序排列进行分析
- **错误处理**：对无效数据和文件错误进行适当处理
- **内存优化**：使用向量容器高效存储数据

## 错误处理

程序会处理以下错误情况：

- 文件不存在或无法打开
- 空文件或无有效时间戳
- 无法创建输出文件
- 数据格式错误（会跳过无效行）

## 注意事项

1. 确保CSV文件的第四列包含有效的数字时间戳
2. 程序会跳过空行和以 `%`开头的注释行
3. 输出文件 `timestamps.txt`会覆盖同名的现有文件

## 示例使用场景

- 日志文件时间戳分析
- 传感器数据时间间隔检测
- 数据采集系统的时间连续性验证
- 时间序列数据的质量检查
