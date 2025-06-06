# HV Toolkit C++ API 文档

**中文** | [English](API_EN.md)
## hv_evt2_codec.h

### evt2::EVT2Header
EVT2格式文件头结构体。

```cpp
struct EVT2Header {
    std::string format_line;    // 格式行
    std::string integrator;     // 集成器名称
    std::string date;           // 创建日期
    uint32_t width;             // 传感器宽度
    uint32_t height;            // 传感器高度
    uint64_t start_timestamp;   // 起始时间戳（微秒）
};
```

### evt2::EventCDEncoder
CD事件编码器，将EventCD事件编码为EVT2原始格式。

```cpp
class EventCDEncoder {
public:
    void setEvent(unsigned short x, unsigned short y, short polarity, Timestamp timestamp);
    void encode(RawEvent* raw_event);
    
    unsigned short x, y;
    short p;
    Timestamp t;
};
```

### evt2::EventTriggerEncoder
外部触发事件编码器。

```cpp
class EventTriggerEncoder {
public:
    void setEvent(short polarity, short trigger_id, Timestamp timestamp);
    void encode(RawEvent* raw_event);
    
    short p, id;
    Timestamp t;
};
```

### evt2::EventTimeEncoder
时间高位编码器。

```cpp
class EventTimeEncoder {
public:
    explicit EventTimeEncoder(Timestamp base);
    void encode(RawEvent* raw_event);
    Timestamp getNextTimeHigh() const;
    void reset(Timestamp base = 0);
};
```

### evt2::EVT2Decoder
EVT2格式解码器。

```cpp
class EVT2Decoder {
public:
    EVT2Decoder();
    size_t decode(const uint8_t* buffer, size_t buffer_size, 
                  std::vector<Metavision::EventCD>& cd_events,
                  std::vector<std::tuple<short, short, Timestamp>>* trigger_events = nullptr);
    void reset();
    Timestamp getCurrentTimeBase() const;
};
```

**类型定义：** `using Timestamp = uint64_t;`

---



---

## hv_camera.h

### 类 hv::HV_Camera
用于事件相机（DVS）的事件流与图像流采集。

#### 继承关系
- 无继承关系，独立类

#### 功能描述
实现HV事件相机的数据采集功能，支持事件流和图像流的异步采集，基于USB设备通信。

#### 构造函数/析构函数

**构造函数**
```cpp
HV_Camera(uint16_t vendor_id, uint16_t product_id)
```
- **参数说明**：
  - `vendor_id` (uint16_t, 必填): USB设备厂商ID
  - `product_id` (uint16_t, 必填): USB设备产品ID
- **功能说明**：初始化相机对象，创建USB设备实例，设置默认参数
- **示例**：
```cpp
hv::HV_Camera camera(0x1d6b, 0x0105);
```

**析构函数**
```cpp
~HV_Camera()
```
- **功能说明**：自动停止事件和图像采集，关闭USB设备，释放资源
- **资源释放行为**：停止所有采集线程，关闭USB连接，清理内存缓冲区

#### 成员函数

**设备控制**

```cpp
bool open()
```
- **功能描述**：打开USB相机设备并初始化端点
- **返回值**：成功返回true，失败返回false
- **异常抛出**：无异常抛出，通过返回值指示状态
- **注意事项**：必须在启动采集前调用，确保设备未被其他程序占用

```cpp
bool isOpen() const
```
- **功能描述**：检查设备是否已成功打开
- **返回值**：设备已打开返回true，否则返回false
- **注意事项**：线程安全的查询函数

```cpp
void close()
```
- **功能描述**：关闭USB设备连接
- **返回值**：无返回值
- **注意事项**：自动停止所有采集操作

**事件采集**

```cpp
bool startEventCapture(EventCallback callback)
```
- **功能描述**：启动事件数据采集，通过回调函数异步返回事件数据
- **参数说明**：
  - `callback` (EventCallback, 必填): 事件处理回调函数
- **返回值**：启动成功返回true，失败返回false
- **异常抛出**：无异常抛出
- **注意事项**：不支持多线程并行调用，回调函数在独立线程中执行

```cpp
void stopEventCapture()
```
- **功能描述**：停止事件数据采集
- **返回值**：无返回值
- **注意事项**：等待采集线程安全退出

**图像采集**

```cpp
bool startImageCapture(ImageCallback callback)
```
- **功能描述**：启动图像数据采集，通过回调函数异步返回图像数据
- **参数说明**：
  - `callback` (ImageCallback, 必填): 图像处理回调函数
- **返回值**：启动成功返回true，失败返回false
- **异常抛出**：无异常抛出
- **注意事项**：不支持多线程并行调用，回调函数在独立线程中执行

```cpp
void stopImageCapture()
```
- **功能描述**：停止图像数据采集
- **返回值**：无返回值
- **注意事项**：等待采集线程安全退出

```cpp
cv::Mat getLatestImage() const
```
- **功能描述**：获取最新采集的图像
- **返回值**：最新的OpenCV图像对象
- **注意事项**：线程安全，返回图像副本

#### 回调函数类型
```cpp
using EventCallback = std::function<void(const std::vector<Metavision::EventCD>&)>
using ImageCallback = std::function<void(const cv::Mat&)>
```

#### 成员变量
- **private成员**：
  - `usb_device_` (std::unique_ptr<USBDevice>): USB设备管理对象
  - `event_endpoint_` (uint8_t): 事件数据端点地址
  - `image_endpoint_` (uint8_t): 图像数据端点地址
  - `event_running_` (bool): 事件采集状态标志
  - `image_running_` (bool): 图像采集状态标志
  - `event_callback_` (EventCallback): 事件回调函数
  - `image_callback_` (ImageCallback): 图像回调函数
  - `latest_image_` (cv::Mat): 最新图像缓存
  - `image_mutex_` (std::mutex): 图像访问互斥锁

#### 常量定义
- `HV_EVS_WIDTH` = 768 (事件流宽度)
- `HV_EVS_HEIGHT` = 608 (事件流高度)
- `HV_APS_WIDTH` = 768 (图像流宽度)
- `HV_APS_HEIGHT` = 608 (图像流高度)

#### 说明
- 事件类型：`Metavision::EventCD`
- 线程安全，支持异步回调
- 自动管理USB设备连接
- 禁止拷贝构造和赋值操作

---


- **返回值**：无返回值
- **注意事项**：需要先调用setEvent设置事件数据

```cpp
void setEvent(unsigned short x_coord, unsigned short y_coord, short polarity, Timestamp timestamp)
```
- **功能描述**：设置要编码的事件数据
- **参数说明**：
  - `x_coord` (unsigned short, 必填): X坐标
  - `y_coord` (unsigned short, 必填): Y坐标
  - `polarity` (short, 必填): 极性（0或1）
  - `timestamp` (Timestamp, 必填): 时间戳（微秒）
- **返回值**：无返回值

### 类 evt2::EventTriggerEncoder
外部触发事件编码器，将触发事件编码为EVT2格式。

#### 继承关系
- 无继承关系，独立类

#### 功能描述
实现外部触发事件到EVT2格式的编码，支持触发ID和极性编码。

#### 成员变量
- **public成员**：
  - `p` (short): 极性
  - `t` (Timestamp): 时间戳（微秒）
  - `id` (short): 触发器ID

#### 成员函数

```cpp
void encode(RawEvent* raw_event)
```
- **功能描述**：将当前触发事件编码为EVT2格式
- **参数说明**：
  - `raw_event` (RawEvent*, 必填): 输出的原始事件数据指针
- **返回值**：无返回值

```cpp
void setEvent(short polarity, short trigger_id, Timestamp timestamp)
```
- **功能描述**：设置要编码的触发事件数据
- **参数说明**：
  - `polarity` (short, 必填): 极性
  - `trigger_id` (short, 必填): 触发器ID
  - `timestamp` (Timestamp, 必填): 时间戳（微秒）
- **返回值**：无返回值

### 类 evt2::EventTimeEncoder
时间高位编码器，处理EVT2格式的时间戳高位编码。

#### 继承关系
- 无继承关系，独立类

#### 功能描述
实现EVT2格式的时间戳高位编码，自动管理时间基准和时间高位事件的生成。

#### 构造函数

```cpp
explicit EventTimeEncoder(Timestamp base)
```
- **参数说明**：
  - `base` (Timestamp, 必填): 第一个事件的基准时间（微秒）
- **功能说明**：初始化时间编码器，设置时间基准

#### 成员函数

```cpp
void encode(RawEvent* raw_event)
```
- **功能描述**：编码时间高位事件
- **参数说明**：
  - `raw_event` (RawEvent*, 必填): 输出的原始事件数据指针
- **返回值**：无返回值

```cpp
Timestamp getNextTimeHigh() const
```
- **功能描述**：获取下一个时间高位值
- **返回值**：下一个时间高位时间戳

```cpp
void reset(Timestamp base = 0)
```
- **功能描述**：重置时间编码器到新的基准时间戳
- **参数说明**：
  - `base` (Timestamp, 可选, 默认值=0): 新的基准时间戳
- **返回值**：无返回值

#### 常量定义
- `N_LOWER_BITS_TH = 6`: 时间高位低位比特数
- `REDUNDANCY_FACTOR = 4`: 冗余因子
- `TH_STEP`: 时间高位步长
- `TH_NEXT_STEP`: 下一个时间高位步长

### 类 evt2::EVT2Decoder
EVT2格式解码器，将原始EVT2数据解码为事件。

#### 继承关系
- 无继承关系，独立类

#### 功能描述
实现EVT2原始数据到EventCD和触发事件的解码，支持批量解码和状态管理。

#### 构造函数

```cpp
EVT2Decoder()
```
- **功能说明**：初始化解码器，设置初始状态

#### 成员函数

```cpp
size_t decode(const uint8_t* buffer, size_t buffer_size, std::vector<Metavision::EventCD>& cd_events, std::vector<std::tuple<short, short, Timestamp>>* trigger_events = nullptr)
```
- **功能描述**：解码原始事件缓冲区
- **参数说明**：
  - `buffer` (const uint8_t*, 必填): 原始事件缓冲区指针
  - `buffer_size` (size_t, 必填): 缓冲区大小（字节）
  - `cd_events` (std::vector<Metavision::EventCD>&, 必填): 输出CD事件向量
  - `trigger_events` (std::vector<std::tuple<short, short, Timestamp>>*, 可选): 输出触发事件向量
- **返回值**：解码的事件数量
- **注意事项**：自动处理时间基准更新

```cpp
void reset()
```
- **功能描述**：重置解码器状态
- **返回值**：无返回值

```cpp
Timestamp getCurrentTimeBase() const
```
- **功能描述**：获取当前时间基准
- **返回值**：当前时间基准值

### 类型定义
- `Timestamp` (uint64_t): 时间戳类型，单位为微秒

---



---

## hv_event_reader.h

### 类 hv::HVEventReader
用于读取EVT2格式的raw事件文件并转换为EventCD事件。

#### 继承关系
- 无继承关系，独立类

#### 功能描述
实现EVT2格式事件文件的读取和解码功能，支持批量读取、流式读取和全量读取，内存使用优化。

#### 构造函数/析构函数

**构造函数**
```cpp
HVEventReader()
```
- **参数说明**：无参数
- **功能说明**：初始化读取器，预分配1MB读取缓冲区
- **示例**：
```cpp
hv::HVEventReader reader;
```

**析构函数**
```cpp
~HVEventReader()
```
- **功能说明**：自动关闭文件，释放资源
- **资源释放行为**：关闭文件流，清理缓冲区

#### 成员函数

**文件操作**

```cpp
bool open(const std::string& filename)
```
- **功能描述**：打开EVT2格式的事件文件并读取文件头
- **参数说明**：
  - `filename` (const std::string&, 必填): 事件文件路径
- **返回值**：成功返回true，失败返回false
- **异常抛出**：无异常抛出，通过返回值指示状态
- **注意事项**：文件必须为有效的EVT2格式，支持相对路径和绝对路径

```cpp
void close()
```
- **功能描述**：关闭当前打开的文件
- **返回值**：无返回值
- **注意事项**：自动在析构函数中调用

```cpp
bool isOpen() const
```
- **功能描述**：检查文件是否已成功打开
- **返回值**：文件已打开返回true，否则返回false
- **注意事项**：线程安全的查询函数

```cpp
const evt2::EVT2Header& getHeader() const
```
- **功能描述**：获取EVT2文件头信息
- **返回值**：EVT2文件头结构体的常量引用
- **注意事项**：必须在文件打开后调用

**事件读取**

```cpp
size_t readEvents(size_t num_events, std::vector<Metavision::EventCD>& events)
```
- **功能描述**：读取指定数量的事件
- **参数说明**：
  - `num_events` (size_t, 必填): 要读取的事件数量
  - `events` (std::vector<Metavision::EventCD>&, 必填): 输出的事件向量
- **返回值**：实际读取的事件数量
- **异常抛出**：无异常抛出
- **注意事项**：如果文件中剩余事件不足，返回实际读取数量

```cpp
size_t readAllEvents(std::vector<Metavision::EventCD>& events)
```
- **功能描述**：读取文件中的所有事件
- **参数说明**：
  - `events` (std::vector<Metavision::EventCD>&, 必填): 输出的事件向量
- **返回值**：读取的事件总数
- **异常抛出**：无异常抛出
- **注意事项**：大文件可能消耗大量内存，建议使用streamEvents

```cpp
size_t streamEvents(size_t batch_size, EventCallback callback)
```
- **功能描述**：流式读取事件，适用于大文件处理
- **参数说明**：
  - `batch_size` (size_t, 必填): 每批次读取的事件数量
  - `callback` (EventCallback, 必填): 事件处理回调函数
- **返回值**：总共处理的事件数量
- **异常抛出**：无异常抛出
- **注意事项**：内存友好，推荐用于大文件处理

```cpp
void reset()
```
- **功能描述**：重置读取位置到文件开始
- **返回值**：无返回值
- **注意事项**：用于重新读取文件

```cpp
std::pair<uint32_t, uint32_t> getImageSize() const
```
- **功能描述**：获取传感器图像尺寸
- **返回值**：包含宽度和高度的pair对象
- **注意事项**：从文件头中提取尺寸信息

#### 回调函数类型
```cpp
using EventCallback = std::function<void(const std::vector<Metavision::EventCD>&)>
```

#### 成员变量
- **private成员**：
  - `file_` (std::ifstream): 文件输入流
  - `header_` (evt2::EVT2Header): EVT2文件头信息
  - `decoder_` (evt2::EVT2Decoder): EVT2解码器
  - `is_open_` (bool): 文件打开状态标志
  - `data_start_pos_` (std::streampos): 数据起始位置
  - `read_buffer_` (std::vector<uint8_t>): 读取缓冲区

#### 说明
- 支持EVT2格式文件读取
- 自动解码为Metavision::EventCD格式
- 支持流式读取，内存友好
- 内置1MB读取缓冲区优化性能

---


- **返回值**：无返回值
- **注意事项**：需要先调用setEvent设置事件数据

```cpp
void setEvent(unsigned short x_coord, unsigned short y_coord, short polarity, Timestamp timestamp)
```
- **功能描述**：设置要编码的事件数据
- **参数说明**：
  - `x_coord` (unsigned short, 必填): X坐标
  - `y_coord` (unsigned short, 必填): Y坐标
  - `polarity` (short, 必填): 极性（0或1）
  - `timestamp` (Timestamp, 必填): 时间戳（微秒）
- **返回值**：无返回值

### 类 evt2::EventTriggerEncoder
外部触发事件编码器，将触发事件编码为EVT2格式。

#### 继承关系
- 无继承关系，独立类

#### 功能描述
实现外部触发事件到EVT2格式的编码，支持触发ID和极性编码。

#### 成员变量
- **public成员**：
  - `p` (short): 极性
  - `t` (Timestamp): 时间戳（微秒）
  - `id` (short): 触发器ID

#### 成员函数

```cpp
void encode(RawEvent* raw_event)
```
- **功能描述**：将当前触发事件编码为EVT2格式
- **参数说明**：
  - `raw_event` (RawEvent*, 必填): 输出的原始事件数据指针
- **返回值**：无返回值

```cpp
void setEvent(short polarity, short trigger_id, Timestamp timestamp)
```
- **功能描述**：设置要编码的触发事件数据
- **参数说明**：
  - `polarity` (short, 必填): 极性
  - `trigger_id` (short, 必填): 触发器ID
  - `timestamp` (Timestamp, 必填): 时间戳（微秒）
- **返回值**：无返回值

### 类 evt2::EventTimeEncoder
时间高位编码器，处理EVT2格式的时间戳高位编码。

#### 继承关系
- 无继承关系，独立类

#### 功能描述
实现EVT2格式的时间戳高位编码，自动管理时间基准和时间高位事件的生成。

#### 构造函数

```cpp
explicit EventTimeEncoder(Timestamp base)
```
- **参数说明**：
  - `base` (Timestamp, 必填): 第一个事件的基准时间（微秒）
- **功能说明**：初始化时间编码器，设置时间基准

#### 成员函数

```cpp
void encode(RawEvent* raw_event)
```
- **功能描述**：编码时间高位事件
- **参数说明**：
  - `raw_event` (RawEvent*, 必填): 输出的原始事件数据指针
- **返回值**：无返回值

```cpp
Timestamp getNextTimeHigh() const
```
- **功能描述**：获取下一个时间高位值
- **返回值**：下一个时间高位时间戳

```cpp
void reset(Timestamp base = 0)
```
- **功能描述**：重置时间编码器到新的基准时间戳
- **参数说明**：
  - `base` (Timestamp, 可选, 默认值=0): 新的基准时间戳
- **返回值**：无返回值

#### 常量定义
- `N_LOWER_BITS_TH = 6`: 时间高位低位比特数
- `REDUNDANCY_FACTOR = 4`: 冗余因子
- `TH_STEP`: 时间高位步长
- `TH_NEXT_STEP`: 下一个时间高位步长

### 类 evt2::EVT2Decoder
EVT2格式解码器，将原始EVT2数据解码为事件。

#### 继承关系
- 无继承关系，独立类

#### 功能描述
实现EVT2原始数据到EventCD和触发事件的解码，支持批量解码和状态管理。

#### 构造函数

```cpp
EVT2Decoder()
```
- **功能说明**：初始化解码器，设置初始状态

#### 成员函数

```cpp
size_t decode(const uint8_t* buffer, size_t buffer_size, std::vector<Metavision::EventCD>& cd_events, std::vector<std::tuple<short, short, Timestamp>>* trigger_events = nullptr)
```
- **功能描述**：解码原始事件缓冲区
- **参数说明**：
  - `buffer` (const uint8_t*, 必填): 原始事件缓冲区指针
  - `buffer_size` (size_t, 必填): 缓冲区大小（字节）
  - `cd_events` (std::vector<Metavision::EventCD>&, 必填): 输出CD事件向量
  - `trigger_events` (std::vector<std::tuple<short, short, Timestamp>>*, 可选): 输出触发事件向量
- **返回值**：解码的事件数量
- **注意事项**：自动处理时间基准更新

```cpp
void reset()
```
- **功能描述**：重置解码器状态
- **返回值**：无返回值

```cpp
Timestamp getCurrentTimeBase() const
```
- **功能描述**：获取当前时间基准
- **返回值**：当前时间基准值

### 类型定义
- `Timestamp` (uint64_t): 时间戳类型，单位为微秒

---



---

## hv_event_writer.h

### 类 hv::HVEventWriter
用于将EventCD事件编码并写入EVT2格式的raw文件。

#### 继承关系
- 无继承关系，独立类

#### 功能描述
实现EventCD事件到EVT2格式文件的编码和写入功能，支持批量写入和缓冲优化，确保数据完整性。

#### 构造函数/析构函数

**构造函数**
```cpp
HVEventWriter()
```
- **参数说明**：无参数
- **功能说明**：初始化写入器，预分配1MB写入缓冲区
- **示例**：
```cpp
hv::HVEventWriter writer;
```

**析构函数**
```cpp
~HVEventWriter()
```
- **功能说明**：自动关闭文件，刷新缓冲区，释放资源
- **资源释放行为**：刷新所有缓冲数据到磁盘，关闭文件流

#### 成员函数

**文件操作**

```cpp
bool open(const std::string& filename, uint32_t width, uint32_t height, uint64_t start_timestamp = 0)
```
- **功能描述**：创建新的EVT2格式文件并写入文件头
- **参数说明**：
  - `filename` (const std::string&, 必填): 输出文件路径
  - `width` (uint32_t, 必填): 传感器图像宽度
  - `height` (uint32_t, 必填): 传感器图像高度
  - `start_timestamp` (uint64_t, 可选, 默认值=0): 起始时间戳（微秒）
- **返回值**：成功返回true，失败返回false
- **异常抛出**：无异常抛出，通过返回值指示状态
- **注意事项**：如果文件已存在将被覆盖，确保有写入权限

```cpp
void close()
```
- **功能描述**：关闭文件并刷新所有缓冲数据
- **返回值**：无返回值
- **注意事项**：自动在析构函数中调用，确保数据完整性

```cpp
bool isOpen() const
```
- **功能描述**：检查文件是否已成功打开
- **返回值**：文件已打开返回true，否则返回false
- **注意事项**：线程安全的查询函数

**事件写入**

```cpp
size_t writeEvents(const std::vector<Metavision::EventCD>& events)
```
- **功能描述**：批量写入EventCD事件到文件
- **参数说明**：
  - `events` (const std::vector<Metavision::EventCD>&, 必填): 要写入的事件向量
- **返回值**：成功写入的事件数量
- **异常抛出**：无异常抛出
- **注意事项**：自动处理时间戳编码，支持大批量写入

```cpp
void flush()
```
- **功能描述**：强制刷新缓冲区数据到磁盘
- **返回值**：无返回值
- **注意事项**：用于确保数据及时写入，避免数据丢失

```cpp
uint64_t getWrittenEventCount() const
```
- **功能描述**：获取已写入的事件总数
- **返回值**：已写入的事件数量
- **注意事项**：统计从文件打开以来的累计写入数量

```cpp
size_t getFileSize() const
```
- **功能描述**：获取当前文件大小（包含缓冲区）
- **返回值**：文件大小（字节）
- **注意事项**：包含未刷新的缓冲区数据大小

#### 成员变量
- **private成员**：
  - `file_` (std::ofstream): 文件输出流
  - `header_` (evt2::EVT2Header): EVT2文件头信息
  - `time_encoder_` (std::unique_ptr<evt2::EventTimeEncoder>): 时间编码器
  - `is_open_` (bool): 文件打开状态标志
  - `event_count_` (uint64_t): 已写入事件计数
  - `write_buffer_` (std::vector<uint8_t>): 写入缓冲区

#### 说明
- 自动编码为EVT2格式
- 支持批量写入，性能优化
- 内置缓冲机制
- 自动处理时间戳高位编码
- 确保数据完整性和一致性

---


- **返回值**：无返回值
- **注意事项**：需要先调用setEvent设置事件数据

```cpp
void setEvent(unsigned short x_coord, unsigned short y_coord, short polarity, Timestamp timestamp)
```
- **功能描述**：设置要编码的事件数据
- **参数说明**：
  - `x_coord` (unsigned short, 必填): X坐标
  - `y_coord` (unsigned short, 必填): Y坐标
  - `polarity` (short, 必填): 极性（0或1）
  - `timestamp` (Timestamp, 必填): 时间戳（微秒）
- **返回值**：无返回值

### 类 evt2::EventTriggerEncoder
外部触发事件编码器，将触发事件编码为EVT2格式。

#### 继承关系
- 无继承关系，独立类

#### 功能描述
实现外部触发事件到EVT2格式的编码，支持触发ID和极性编码。

#### 成员变量
- **public成员**：
  - `p` (short): 极性
  - `t` (Timestamp): 时间戳（微秒）
  - `id` (short): 触发器ID

#### 成员函数

```cpp
void encode(RawEvent* raw_event)
```
- **功能描述**：将当前触发事件编码为EVT2格式
- **参数说明**：
  - `raw_event` (RawEvent*, 必填): 输出的原始事件数据指针
- **返回值**：无返回值

```cpp
void setEvent(short polarity, short trigger_id, Timestamp timestamp)
```
- **功能描述**：设置要编码的触发事件数据
- **参数说明**：
  - `polarity` (short, 必填): 极性
  - `trigger_id` (short, 必填): 触发器ID
  - `timestamp` (Timestamp, 必填): 时间戳（微秒）
- **返回值**：无返回值

### 类 evt2::EventTimeEncoder
时间高位编码器，处理EVT2格式的时间戳高位编码。

#### 继承关系
- 无继承关系，独立类

#### 功能描述
实现EVT2格式的时间戳高位编码，自动管理时间基准和时间高位事件的生成。

#### 构造函数

```cpp
explicit EventTimeEncoder(Timestamp base)
```
- **参数说明**：
  - `base` (Timestamp, 必填): 第一个事件的基准时间（微秒）
- **功能说明**：初始化时间编码器，设置时间基准

#### 成员函数

```cpp
void encode(RawEvent* raw_event)
```
- **功能描述**：编码时间高位事件
- **参数说明**：
  - `raw_event` (RawEvent*, 必填): 输出的原始事件数据指针
- **返回值**：无返回值

```cpp
Timestamp getNextTimeHigh() const
```
- **功能描述**：获取下一个时间高位值
- **返回值**：下一个时间高位时间戳

```cpp
void reset(Timestamp base = 0)
```
- **功能描述**：重置时间编码器到新的基准时间戳
- **参数说明**：
  - `base` (Timestamp, 可选, 默认值=0): 新的基准时间戳
- **返回值**：无返回值

#### 常量定义
- `N_LOWER_BITS_TH = 6`: 时间高位低位比特数
- `REDUNDANCY_FACTOR = 4`: 冗余因子
- `TH_STEP`: 时间高位步长
- `TH_NEXT_STEP`: 下一个时间高位步长

### 类 evt2::EVT2Decoder
EVT2格式解码器，将原始EVT2数据解码为事件。

#### 继承关系
- 无继承关系，独立类

#### 功能描述
实现EVT2原始数据到EventCD和触发事件的解码，支持批量解码和状态管理。

#### 构造函数

```cpp
EVT2Decoder()
```
- **功能说明**：初始化解码器，设置初始状态

#### 成员函数

```cpp
size_t decode(const uint8_t* buffer, size_t buffer_size, std::vector<Metavision::EventCD>& cd_events, std::vector<std::tuple<short, short, Timestamp>>* trigger_events = nullptr)
```
- **功能描述**：解码原始事件缓冲区
- **参数说明**：
  - `buffer` (const uint8_t*, 必填): 原始事件缓冲区指针
  - `buffer_size` (size_t, 必填): 缓冲区大小（字节）
  - `cd_events` (std::vector<Metavision::EventCD>&, 必填): 输出CD事件向量
  - `trigger_events` (std::vector<std::tuple<short, short, Timestamp>>*, 可选): 输出触发事件向量
- **返回值**：解码的事件数量
- **注意事项**：自动处理时间基准更新

```cpp
void reset()
```
- **功能描述**：重置解码器状态
- **返回值**：无返回值

```cpp
Timestamp getCurrentTimeBase() const
```
- **功能描述**：获取当前时间基准
- **返回值**：当前时间基准值

### 类型定义
- `Timestamp` (uint64_t): 时间戳类型，单位为微秒

---



---

## hv_events_format.h

### 类型定义
- `using HVEventsFormat = std::uint64_t` 优化的64位事件编码格式
- `struct HVRawHeader` 事件文件头结构体

### 编码格式
- **时间戳**: 43位 (支持更长的时间戳)
- **X坐标**: 10位 (支持0-1023像素)
- **Y坐标**: 10位 (支持0-1023像素)
- **极性**: 1位 (0或1)

### 编码/解码函数
- `encode_hv_event(HVEventsFormat &encoded_ev, unsigned short x, unsigned short y, short p, Metavision::timestamp t)` 单事件编码
- `decode_hv_event(HVEventsFormat encoded_ev, Metavision::EventCD &ev, Metavision::timestamp t_shift = 0)` 单事件解码
- `encode_hv_events_batch(const std::vector<Metavision::EventCD>& events, std::vector<HVEventsFormat>& encoded_events)` 批量编码
- `decode_hv_events_batch(const std::vector<HVEventsFormat>& encoded_events, std::vector<Metavision::EventCD>& events, Metavision::timestamp t_shift = 0)` 批量解码

### 常量定义
- `HV_RAW_MAGIC` = 0x48565241 ("HVRA"魔数)
- `HV_TS_BITS` = 43 (时间戳位数)
- `HV_X_BITS` = 10 (X坐标位数)
- `HV_Y_BITS` = 10 (Y坐标位数)
- `HV_P_BITS` = 1 (极性位数)

### 说明
- 高效的64位事件编码格式
- 支持批量编解码操作
- 内联函数实现，性能优化

---


- **返回值**：无返回值
- **注意事项**：需要先调用setEvent设置事件数据

```cpp
void setEvent(unsigned short x_coord, unsigned short y_coord, short polarity, Timestamp timestamp)
```
- **功能描述**：设置要编码的事件数据
- **参数说明**：
  - `x_coord` (unsigned short, 必填): X坐标
  - `y_coord` (unsigned short, 必填): Y坐标
  - `polarity` (short, 必填): 极性（0或1）
  - `timestamp` (Timestamp, 必填): 时间戳（微秒）
- **返回值**：无返回值

### 类 evt2::EventTriggerEncoder
外部触发事件编码器，将触发事件编码为EVT2格式。

#### 继承关系
- 无继承关系，独立类

#### 功能描述
实现外部触发事件到EVT2格式的编码，支持触发ID和极性编码。

#### 成员变量
- **public成员**：
  - `p` (short): 极性
  - `t` (Timestamp): 时间戳（微秒）
  - `id` (short): 触发器ID

#### 成员函数

```cpp
void encode(RawEvent* raw_event)
```
- **功能描述**：将当前触发事件编码为EVT2格式
- **参数说明**：
  - `raw_event` (RawEvent*, 必填): 输出的原始事件数据指针
- **返回值**：无返回值

```cpp
void setEvent(short polarity, short trigger_id, Timestamp timestamp)
```
- **功能描述**：设置要编码的触发事件数据
- **参数说明**：
  - `polarity` (short, 必填): 极性
  - `trigger_id` (short, 必填): 触发器ID
  - `timestamp` (Timestamp, 必填): 时间戳（微秒）
- **返回值**：无返回值

### 类 evt2::EventTimeEncoder
时间高位编码器，处理EVT2格式的时间戳高位编码。

#### 继承关系
- 无继承关系，独立类

#### 功能描述
实现EVT2格式的时间戳高位编码，自动管理时间基准和时间高位事件的生成。

#### 构造函数

```cpp
explicit EventTimeEncoder(Timestamp base)
```
- **参数说明**：
  - `base` (Timestamp, 必填): 第一个事件的基准时间（微秒）
- **功能说明**：初始化时间编码器，设置时间基准

#### 成员函数

```cpp
void encode(RawEvent* raw_event)
```
- **功能描述**：编码时间高位事件
- **参数说明**：
  - `raw_event` (RawEvent*, 必填): 输出的原始事件数据指针
- **返回值**：无返回值

```cpp
Timestamp getNextTimeHigh() const
```
- **功能描述**：获取下一个时间高位值
- **返回值**：下一个时间高位时间戳

```cpp
void reset(Timestamp base = 0)
```
- **功能描述**：重置时间编码器到新的基准时间戳
- **参数说明**：
  - `base` (Timestamp, 可选, 默认值=0): 新的基准时间戳
- **返回值**：无返回值

#### 常量定义
- `N_LOWER_BITS_TH = 6`: 时间高位低位比特数
- `REDUNDANCY_FACTOR = 4`: 冗余因子
- `TH_STEP`: 时间高位步长
- `TH_NEXT_STEP`: 下一个时间高位步长

### 类 evt2::EVT2Decoder
EVT2格式解码器，将原始EVT2数据解码为事件。

#### 继承关系
- 无继承关系，独立类

#### 功能描述
实现EVT2原始数据到EventCD和触发事件的解码，支持批量解码和状态管理。

#### 构造函数

```cpp
EVT2Decoder()
```
- **功能说明**：初始化解码器，设置初始状态

#### 成员函数

```cpp
size_t decode(const uint8_t* buffer, size_t buffer_size, std::vector<Metavision::EventCD>& cd_events, std::vector<std::tuple<short, short, Timestamp>>* trigger_events = nullptr)
```
- **功能描述**：解码原始事件缓冲区
- **参数说明**：
  - `buffer` (const uint8_t*, 必填): 原始事件缓冲区指针
  - `buffer_size` (size_t, 必填): 缓冲区大小（字节）
  - `cd_events` (std::vector<Metavision::EventCD>&, 必填): 输出CD事件向量
  - `trigger_events` (std::vector<std::tuple<short, short, Timestamp>>*, 可选): 输出触发事件向量
- **返回值**：解码的事件数量
- **注意事项**：自动处理时间基准更新

```cpp
void reset()
```
- **功能描述**：重置解码器状态
- **返回值**：无返回值

```cpp
Timestamp getCurrentTimeBase() const
```
- **功能描述**：获取当前时间基准
- **返回值**：当前时间基准值

### 类型定义
- `Timestamp` (uint64_t): 时间戳类型，单位为微秒

---



---




- **返回值**：无返回值
- **注意事项**：需要先调用setEvent设置事件数据

```cpp
void setEvent(unsigned short x_coord, unsigned short y_coord, short polarity, Timestamp timestamp)
```
- **功能描述**：设置要编码的事件数据
- **参数说明**：
  - `x_coord` (unsigned short, 必填): X坐标
  - `y_coord` (unsigned short, 必填): Y坐标
  - `polarity` (short, 必填): 极性（0或1）
  - `timestamp` (Timestamp, 必填): 时间戳（微秒）
- **返回值**：无返回值

### 类 evt2::EventTriggerEncoder
外部触发事件编码器，将触发事件编码为EVT2格式。

#### 继承关系
- 无继承关系，独立类

#### 功能描述
实现外部触发事件到EVT2格式的编码，支持触发ID和极性编码。

#### 成员变量
- **public成员**：
  - `p` (short): 极性
  - `t` (Timestamp): 时间戳（微秒）
  - `id` (short): 触发器ID

#### 成员函数

```cpp
void encode(RawEvent* raw_event)
```
- **功能描述**：将当前触发事件编码为EVT2格式
- **参数说明**：
  - `raw_event` (RawEvent*, 必填): 输出的原始事件数据指针
- **返回值**：无返回值

```cpp
void setEvent(short polarity, short trigger_id, Timestamp timestamp)
```
- **功能描述**：设置要编码的触发事件数据
- **参数说明**：
  - `polarity` (short, 必填): 极性
  - `trigger_id` (short, 必填): 触发器ID
  - `timestamp` (Timestamp, 必填): 时间戳（微秒）
- **返回值**：无返回值

### 类 evt2::EventTimeEncoder
时间高位编码器，处理EVT2格式的时间戳高位编码。

#### 继承关系
- 无继承关系，独立类

#### 功能描述
实现EVT2格式的时间戳高位编码，自动管理时间基准和时间高位事件的生成。

#### 构造函数

```cpp
explicit EventTimeEncoder(Timestamp base)
```
- **参数说明**：
  - `base` (Timestamp, 必填): 第一个事件的基准时间（微秒）
- **功能说明**：初始化时间编码器，设置时间基准

#### 成员函数

```cpp
void encode(RawEvent* raw_event)
```
- **功能描述**：编码时间高位事件
- **参数说明**：
  - `raw_event` (RawEvent*, 必填): 输出的原始事件数据指针
- **返回值**：无返回值

```cpp
Timestamp getNextTimeHigh() const
```
- **功能描述**：获取下一个时间高位值
- **返回值**：下一个时间高位时间戳

```cpp
void reset(Timestamp base = 0)
```
- **功能描述**：重置时间编码器到新的基准时间戳
- **参数说明**：
  - `base` (Timestamp, 可选, 默认值=0): 新的基准时间戳
- **返回值**：无返回值

#### 常量定义
- `N_LOWER_BITS_TH = 6`: 时间高位低位比特数
- `REDUNDANCY_FACTOR = 4`: 冗余因子
- `TH_STEP`: 时间高位步长
- `TH_NEXT_STEP`: 下一个时间高位步长

### 类 evt2::EVT2Decoder
EVT2格式解码器，将原始EVT2数据解码为事件。

#### 继承关系
- 无继承关系，独立类

#### 功能描述
实现EVT2原始数据到EventCD和触发事件的解码，支持批量解码和状态管理。

#### 构造函数

```cpp
EVT2Decoder()
```
- **功能说明**：初始化解码器，设置初始状态

#### 成员函数

```cpp
size_t decode(const uint8_t* buffer, size_t buffer_size, std::vector<Metavision::EventCD>& cd_events, std::vector<std::tuple<short, short, Timestamp>>* trigger_events = nullptr)
```
- **功能描述**：解码原始事件缓冲区
- **参数说明**：
  - `buffer` (const uint8_t*, 必填): 原始事件缓冲区指针
  - `buffer_size` (size_t, 必填): 缓冲区大小（字节）
  - `cd_events` (std::vector<Metavision::EventCD>&, 必填): 输出CD事件向量
  - `trigger_events` (std::vector<std::tuple<short, short, Timestamp>>*, 可选): 输出触发事件向量
- **返回值**：解码的事件数量
- **注意事项**：自动处理时间基准更新

```cpp
void reset()
```
- **功能描述**：重置解码器状态
- **返回值**：无返回值

```cpp
Timestamp getCurrentTimeBase() const
```
- **功能描述**：获取当前时间基准
- **返回值**：当前时间基准值

### 类型定义
- `Timestamp` (uint64_t): 时间戳类型，单位为微秒

---



---

## hv_usb_device.h

### 类 hv::USBDevice
USB设备通信接口，基于libusb实现。

#### 继承关系
- 无继承关系，独立类

#### 功能描述
实现USB设备的底层通信功能，包括设备枚举、连接管理和数据传输，专门用于与HV相机设备进行USB通信。

#### 构造函数/析构函数

**构造函数**
```cpp
USBDevice(uint16_t vendor_id, uint16_t product_id)
```
- **参数说明**：
  - `vendor_id` (uint16_t, 必填): USB厂商ID
  - `product_id` (uint16_t, 必填): USB产品ID
- **功能说明**：初始化USB设备对象，设置目标设备的VID/PID，初始化libusb上下文
- **示例**：
```cpp
hv::USBDevice device(0x1234, 0x5678);  // 使用特定VID/PID
```

**析构函数**
```cpp
~USBDevice()
```
- **功能说明**：自动关闭设备连接，释放USB资源
- **资源释放行为**：关闭设备句柄，释放libusb上下文，清理所有USB资源

#### 成员函数

**设备控制**

```cpp
bool open()
```
- **功能描述**：打开指定VID/PID的USB设备
- **返回值**：成功返回true，失败返回false
- **异常抛出**：无异常抛出，通过返回值指示状态
- **注意事项**：自动处理设备枚举、内核驱动分离和接口声明

```cpp
void close()
```
- **功能描述**：关闭USB设备连接
- **返回值**：无返回值
- **注意事项**：自动释放接口，重新附加内核驱动

```cpp
bool isOpen() const
```
- **功能描述**：检查USB设备是否已成功打开
- **返回值**：设备已打开返回true，否则返回false
- **注意事项**：线程安全的状态查询

**数据传输**

```cpp
uint8_t getEndpointAddress(int endpoint_index) const
```
- **功能描述**：获取指定索引的端点地址
- **参数说明**：
  - `endpoint_index` (int, 必填): 端点索引（0-based）
- **返回值**：端点地址，失败返回0
- **注意事项**：需要在设备打开后调用

```cpp
int bulkTransfer(uint8_t endpoint, uint8_t* data, int length, int timeout = 1000)
```
- **功能描述**：执行USB批量传输
- **参数说明**：
  - `endpoint` (uint8_t, 必填): 端点地址
  - `data` (uint8_t*, 必填): 数据缓冲区指针
  - `length` (int, 必填): 数据长度（字节）
  - `timeout` (int, 可选, 默认值=1000): 超时时间（毫秒）
- **返回值**：实际传输的字节数，负值表示错误
- **异常抛出**：无异常抛出
- **注意事项**：支持输入和输出传输，根据端点方向自动判断

#### 成员变量
- **private成员**：
  - `vendor_id_` (uint16_t): USB厂商ID
  - `product_id_` (uint16_t): USB产品ID
  - `ctx_` (libusb_context*): libusb上下文
  - `device_` (libusb_device*): USB设备句柄
  - `handle_` (libusb_device_handle*): USB设备操作句柄
  - `attached_` (bool): 内核驱动附加状态
  - `endpoints_` (std::vector<uint8_t>): 端点地址列表

#### 说明
- 自动处理设备初始化和清理
- 支持批量数据传输
- 内置超时机制
- 自动管理内核驱动分离/重新附加
- 线程安全的状态查询
- 禁止拷贝构造和赋值操作

---


- **返回值**：无返回值
- **注意事项**：需要先调用setEvent设置事件数据

```cpp
void setEvent(unsigned short x_coord, unsigned short y_coord, short polarity, Timestamp timestamp)
```
- **功能描述**：设置要编码的事件数据
- **参数说明**：
  - `x_coord` (unsigned short, 必填): X坐标
  - `y_coord` (unsigned short, 必填): Y坐标
  - `polarity` (short, 必填): 极性（0或1）
  - `timestamp` (Timestamp, 必填): 时间戳（微秒）
- **返回值**：无返回值

### 类 evt2::EventTriggerEncoder
外部触发事件编码器，将触发事件编码为EVT2格式。

#### 继承关系
- 无继承关系，独立类

#### 功能描述
实现外部触发事件到EVT2格式的编码，支持触发ID和极性编码。

#### 成员变量
- **public成员**：
  - `p` (short): 极性
  - `t` (Timestamp): 时间戳（微秒）
  - `id` (short): 触发器ID

#### 成员函数

```cpp
void encode(RawEvent* raw_event)
```
- **功能描述**：将当前触发事件编码为EVT2格式
- **参数说明**：
  - `raw_event` (RawEvent*, 必填): 输出的原始事件数据指针
- **返回值**：无返回值

```cpp
void setEvent(short polarity, short trigger_id, Timestamp timestamp)
```
- **功能描述**：设置要编码的触发事件数据
- **参数说明**：
  - `polarity` (short, 必填): 极性
  - `trigger_id` (short, 必填): 触发器ID
  - `timestamp` (Timestamp, 必填): 时间戳（微秒）
- **返回值**：无返回值

### 类 evt2::EventTimeEncoder
时间高位编码器，处理EVT2格式的时间戳高位编码。

#### 继承关系
- 无继承关系，独立类

#### 功能描述
实现EVT2格式的时间戳高位编码，自动管理时间基准和时间高位事件的生成。

#### 构造函数

```cpp
explicit EventTimeEncoder(Timestamp base)
```
- **参数说明**：
  - `base` (Timestamp, 必填): 第一个事件的基准时间（微秒）
- **功能说明**：初始化时间编码器，设置时间基准

#### 成员函数

```cpp
void encode(RawEvent* raw_event)
```
- **功能描述**：编码时间高位事件
- **参数说明**：
  - `raw_event` (RawEvent*, 必填): 输出的原始事件数据指针
- **返回值**：无返回值

```cpp
Timestamp getNextTimeHigh() const
```
- **功能描述**：获取下一个时间高位值
- **返回值**：下一个时间高位时间戳

```cpp
void reset(Timestamp base = 0)
```
- **功能描述**：重置时间编码器到新的基准时间戳
- **参数说明**：
  - `base` (Timestamp, 可选, 默认值=0): 新的基准时间戳
- **返回值**：无返回值

#### 常量定义
- `N_LOWER_BITS_TH = 6`: 时间高位低位比特数
- `REDUNDANCY_FACTOR = 4`: 冗余因子
- `TH_STEP`: 时间高位步长
- `TH_NEXT_STEP`: 下一个时间高位步长

### 类 evt2::EVT2Decoder
EVT2格式解码器，将原始EVT2数据解码为事件。

#### 继承关系
- 无继承关系，独立类

#### 功能描述
实现EVT2原始数据到EventCD和触发事件的解码，支持批量解码和状态管理。

#### 构造函数

```cpp
EVT2Decoder()
```
- **功能说明**：初始化解码器，设置初始状态

#### 成员函数

```cpp
size_t decode(const uint8_t* buffer, size_t buffer_size, std::vector<Metavision::EventCD>& cd_events, std::vector<std::tuple<short, short, Timestamp>>* trigger_events = nullptr)
```
- **功能描述**：解码原始事件缓冲区
- **参数说明**：
  - `buffer` (const uint8_t*, 必填): 原始事件缓冲区指针
  - `buffer_size` (size_t, 必填): 缓冲区大小（字节）
  - `cd_events` (std::vector<Metavision::EventCD>&, 必填): 输出CD事件向量
  - `trigger_events` (std::vector<std::tuple<short, short, Timestamp>>*, 可选): 输出触发事件向量
- **返回值**：解码的事件数量
- **注意事项**：自动处理时间基准更新

```cpp
void reset()
```
- **功能描述**：重置解码器状态
- **返回值**：无返回值

```cpp
Timestamp getCurrentTimeBase() const
```
- **功能描述**：获取当前时间基准
- **返回值**：当前时间基准值

### 类型定义
- `Timestamp` (uint64_t): 时间戳类型，单位为微秒

---


