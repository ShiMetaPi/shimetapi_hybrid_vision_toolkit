cmake_minimum_required(VERSION 3.16)
project(hv_camera_record VERSION 1.0.0 LANGUAGES CXX)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找HV Toolkit 
find_package(HVToolkit REQUIRED)

# 创建录制示例可执行文件
add_executable(hv_camera_record hv_camera_record.cpp)

# 链接HV Toolkit 
target_link_libraries(hv_camera_record
    PRIVATE
        HVToolkit::hv_camera
        HVToolkit::hv_event_writer
)
