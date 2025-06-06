# HV Toolkit C++ API Documentation

[中文](API.md) | **English**

## hv_evt2_codec.h

### evt2::EVT2Header
EVT2 format file header structure.

```cpp
struct EVT2Header {
    std::string format_line;    // Format line
    std::string integrator;     // Integrator name
    std::string date;           // Creation date
    uint32_t width;             // Sensor width
    uint32_t height;            // Sensor height
    uint64_t start_timestamp;   // Start timestamp (microseconds)
};
```

### evt2::EventCDEncoder
CD event encoder that encodes EventCD events into EVT2 raw format.

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
External trigger event encoder.

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
Time high-order encoder.

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
EVT2 format decoder.

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

**Type Definition:** `using Timestamp = uint64_t;`

---

## hv_camera.h

### Class hv::HV_Camera
Used for event stream and image stream acquisition from event cameras (DVS).

#### Inheritance
- No inheritance, standalone class

#### Description
Implements data acquisition functionality for HV event cameras, supporting asynchronous acquisition of event streams and image streams based on USB device communication.

#### Constructors/Destructors

**Constructor**
```cpp
HV_Camera(uint16_t vendor_id, uint16_t product_id)
```
- **Parameters**:
  - `vendor_id` (uint16_t, required): USB device vendor ID
  - `product_id` (uint16_t, required): USB device product ID
- **Description**: Initialize camera object, create USB device instance, set default parameters
- **Example**:
```cpp
hv::HV_Camera camera(0x1d6b, 0x0105);
```

**Destructor**
```cpp
~HV_Camera()
```
- **Description**: Automatically stop event and image acquisition, close USB device, release resources
- **Resource Release Behavior**: Stop all acquisition threads, close USB connection, clean memory buffers

#### Member Functions

**Device Control**

```cpp
bool open()
```
- **Description**: Open USB camera device and initialize endpoints
- **Return Value**: Returns true on success, false on failure
- **Exception Handling**: No exceptions thrown, status indicated by return value
- **Notes**: Must be called before starting acquisition, ensure device is not occupied by other programs

```cpp
bool isOpen() const
```
- **Description**: Check if device has been successfully opened
- **Return Value**: Returns true if device is open, false otherwise
- **Notes**: Thread-safe query function

```cpp
void close()
```
- **Description**: Close USB device connection
- **Return Value**: No return value
- **Notes**: Automatically stops all acquisition operations

**Event Acquisition**

```cpp
bool startEventCapture(EventCallback callback)
```
- **Description**: Start event data acquisition, return event data asynchronously through callback function
- **Parameters**:
  - `callback` (EventCallback, required): Event processing callback function
- **Return Value**: Returns true on successful start, false on failure
- **Exception Handling**: No exceptions thrown
- **Notes**: Does not support multi-threaded parallel calls, callback function executes in separate thread

```cpp
void stopEventCapture()
```
- **Description**: Stop event data acquisition
- **Return Value**: No return value
- **Notes**: Waits for acquisition thread to exit safely

**Image Acquisition**

```cpp
bool startImageCapture(ImageCallback callback)
```
- **Description**: Start image data acquisition, return image data asynchronously through callback function
- **Parameters**:
  - `callback` (ImageCallback, required): Image processing callback function
- **Return Value**: Returns true on successful start, false on failure
- **Exception Handling**: No exceptions thrown
- **Notes**: Does not support multi-threaded parallel calls, callback function executes in separate thread

```cpp
void stopImageCapture()
```
- **Description**: Stop image data acquisition
- **Return Value**: No return value
- **Notes**: Waits for acquisition thread to exit safely

```cpp
cv::Mat getLatestImage() const
```
- **Description**: Get the latest acquired image
- **Return Value**: Latest OpenCV image object
- **Notes**: Thread-safe, returns image copy

#### Callback Function Types
```cpp
using EventCallback = std::function<void(const std::vector<Metavision::EventCD>&)>
using ImageCallback = std::function<void(const cv::Mat&)>
```

#### Member Variables
- **private members**:
  - `usb_device_` (std::unique_ptr<USBDevice>): USB device management object
  - `event_endpoint_` (uint8_t): Event data endpoint address
  - `image_endpoint_` (uint8_t): Image data endpoint address
  - `event_running_` (bool): Event acquisition status flag
  - `image_running_` (bool): Image acquisition status flag
  - `event_callback_` (EventCallback): Event callback function
  - `image_callback_` (ImageCallback): Image callback function
  - `latest_image_` (cv::Mat): Latest image cache
  - `image_mutex_` (std::mutex): Image access mutex

#### Constants
- `HV_EVS_WIDTH` = 768 (Event stream width)
- `HV_EVS_HEIGHT` = 608 (Event stream height)
- `HV_APS_WIDTH` = 768 (Image stream width)
- `HV_APS_HEIGHT` = 608 (Image stream height)

#### Notes
- Event type: `Metavision::EventCD`
- Thread-safe, supports asynchronous callbacks
- Automatically manages USB device connection
- Copy constructor and assignment operations are disabled

---

## hv_event_reader.h

### Class hv::HVEventReader
Used to read EVT2 format raw event files and convert them to EventCD events.

#### Inheritance
- No inheritance, standalone class

#### Description
Implements reading and decoding functionality for EVT2 format event files, supporting batch reading, streaming reading, and full reading with memory usage optimization.

#### Constructors/Destructors

**Constructor**
```cpp
HVEventReader()
```
- **Parameters**: No parameters
- **Description**: Initialize reader, pre-allocate 1MB read buffer
- **Example**:
```cpp
hv::HVEventReader reader;
```

**Destructor**
```cpp
~HVEventReader()
```
- **Description**: Automatically close file, release resources
- **Resource Release Behavior**: Close file stream, clean buffers

#### Member Functions

**File Operations**

```cpp
bool open(const std::string& filename)
```
- **Description**: Open EVT2 format event file and read file header
- **Parameters**:
  - `filename` (const std::string&, required): Event file path
- **Return Value**: Returns true on success, false on failure
- **Exception Handling**: No exceptions thrown, status indicated by return value
- **Notes**: File must be valid EVT2 format, supports relative and absolute paths

```cpp
void close()
```
- **Description**: Close currently opened file
- **Return Value**: No return value
- **Notes**: Automatically called in destructor

```cpp
bool isOpen() const
```
- **Description**: Check if file has been successfully opened
- **Return Value**: Returns true if file is open, false otherwise
- **Notes**: Thread-safe query function

```cpp
const evt2::EVT2Header& getHeader() const
```
- **Description**: Get EVT2 file header information
- **Return Value**: Constant reference to EVT2 file header structure
- **Notes**: Must be called after file is opened

**Event Reading**

```cpp
size_t readEvents(size_t num_events, std::vector<Metavision::EventCD>& events)
```
- **Description**: Read specified number of events
- **Parameters**:
  - `num_events` (size_t, required): Number of events to read
  - `events` (std::vector<Metavision::EventCD>&, required): Output event vector
- **Return Value**: Actual number of events read
- **Exception Handling**: No exceptions thrown
- **Notes**: If insufficient events remain in file, returns actual number read

```cpp
size_t readAllEvents(std::vector<Metavision::EventCD>& events)
```
- **Description**: Read all events in the file
- **Parameters**:
  - `events` (std::vector<Metavision::EventCD>&, required): Output event vector
- **Return Value**: Total number of events read
- **Exception Handling**: No exceptions thrown
- **Notes**: Large files may consume significant memory, recommend using streamEvents

```cpp
size_t streamEvents(size_t batch_size, EventCallback callback)
```
- **Description**: Stream read events, suitable for large file processing
- **Parameters**:
  - `batch_size` (size_t, required): Number of events per batch
  - `callback` (EventCallback, required): Event processing callback function
- **Return Value**: Total number of events processed
- **Exception Handling**: No exceptions thrown
- **Notes**: Memory-friendly, recommended for large file processing

```cpp
void reset()
```
- **Description**: Reset read position to file beginning
- **Return Value**: No return value
- **Notes**: Used to re-read file

```cpp
std::pair<uint32_t, uint32_t> getImageSize() const
```
- **Description**: Get sensor image dimensions
- **Return Value**: Pair object containing width and height
- **Notes**: Extracts dimensions from file header

#### Callback Function Types
```cpp
using EventCallback = std::function<void(const std::vector<Metavision::EventCD>&)>
```

#### Member Variables
- **private members**:
  - `file_` (std::ifstream): File input stream
  - `header_` (evt2::EVT2Header): EVT2 file header information
  - `decoder_` (evt2::EVT2Decoder): EVT2 decoder
  - `is_open_` (bool): File open status flag
  - `data_start_pos_` (std::streampos): Data start position
  - `read_buffer_` (std::vector<uint8_t>): Read buffer

#### Notes
- Supports EVT2 format file reading
- Automatically decodes to Metavision::EventCD format
- Supports streaming reading, memory-friendly
- Built-in 1MB read buffer for performance optimization

---

## hv_event_writer.h

### Class hv::HVEventWriter
Used to encode EventCD events and write them to EVT2 format raw files.

#### Inheritance
- No inheritance, standalone class

#### Description
Implements encoding and writing functionality from EventCD events to EVT2 format files, supporting batch writing and buffer optimization to ensure data integrity.

#### Constructors/Destructors

**Constructor**
```cpp
HVEventWriter()
```
- **Parameters**: No parameters
- **Description**: Initialize writer, pre-allocate 1MB write buffer
- **Example**:
```cpp
hv::HVEventWriter writer;
```

**Destructor**
```cpp
~HVEventWriter()
```
- **Description**: Automatically close file, flush buffers, release resources
- **Resource Release Behavior**: Flush all buffered data to disk, close file stream

#### Member Functions

**File Operations**

```cpp
bool open(const std::string& filename, uint32_t width, uint32_t height, uint64_t start_timestamp = 0)
```
- **Description**: Create new EVT2 format file and write file header
- **Parameters**:
  - `filename` (const std::string&, required): Output file path
  - `width` (uint32_t, required): Sensor image width
  - `height` (uint32_t, required): Sensor image height
  - `start_timestamp` (uint64_t, optional, default=0): Start timestamp (microseconds)
- **Return Value**: Returns true on success, false on failure
- **Exception Handling**: No exceptions thrown, status indicated by return value
- **Notes**: Existing file will be overwritten, ensure write permissions

```cpp
void close()
```
- **Description**: Close file and flush all buffered data
- **Return Value**: No return value
- **Notes**: Automatically called in destructor, ensures data integrity

```cpp
bool isOpen() const
```
- **Description**: Check if file has been successfully opened
- **Return Value**: Returns true if file is open, false otherwise
- **Notes**: Thread-safe query function

**Event Writing**

```cpp
size_t writeEvents(const std::vector<Metavision::EventCD>& events)
```
- **Description**: Batch write EventCD events to file
- **Parameters**:
  - `events` (const std::vector<Metavision::EventCD>&, required): Event vector to write
- **Return Value**: Number of events successfully written
- **Exception Handling**: No exceptions thrown
- **Notes**: Automatically handles timestamp encoding, supports large batch writing

```cpp
void flush()
```
- **Description**: Force flush buffer data to disk
- **Return Value**: No return value
- **Notes**: Used to ensure timely data writing, avoid data loss

```cpp
uint64_t getWrittenEventCount() const
```
- **Description**: Get total number of events written
- **Return Value**: Total event count
- **Notes**: Thread-safe query function

#### Member Variables
- **private members**:
  - `file_` (std::ofstream): File output stream
  - `header_` (evt2::EVT2Header): EVT2 file header information
  - `cd_encoder_` (evt2::EventCDEncoder): CD event encoder
  - `time_encoder_` (evt2::EventTimeEncoder): Time encoder
  - `is_open_` (bool): File open status flag
  - `written_event_count_` (uint64_t): Written event counter
  - `write_buffer_` (std::vector<uint8_t>): Write buffer

#### Notes
- Supports EVT2 format file writing
- Automatically encodes from Metavision::EventCD format
- Built-in 1MB write buffer for performance optimization
- Ensures data integrity and consistency

---