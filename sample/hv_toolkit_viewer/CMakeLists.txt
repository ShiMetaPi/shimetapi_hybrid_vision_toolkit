cmake_minimum_required(VERSION 3.16)
project(hv_toolkit_viewer VERSION 1.0.0 LANGUAGES CXX)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找HVToolkit包
find_package(HVToolkit REQUIRED)
# 查找Metavision SDK
find_package(MetavisionSDK REQUIRED COMPONENTS core stream)
# 查找OpenCV
find_package(OpenCV REQUIRED)

# 创建可执行文件
add_executable(hv_toolkit_viewer hv_toolkit_viewer.cpp)


# 链接HVToolkit库
target_link_libraries(hv_toolkit_viewer 
    HVToolkit::hv_event_reader
    HVToolkit::hv_camera
    ${MetavisionSDK_LIBRARIES}
    ${OpenCV_LIBS}
)
