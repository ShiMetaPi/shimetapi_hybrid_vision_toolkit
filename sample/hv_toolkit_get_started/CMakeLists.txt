cmake_minimum_required(VERSION 3.10)
project(HVToolkitExample VERSION 1.0.0 LANGUAGES CXX)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找OpenCV
find_package(OpenCV REQUIRED)
# 查找HV Toolkit 
find_package(HVToolkit REQUIRED)

# 创建示例可执行文件
add_executable(hv_example hv_toolkit_get_started.cpp)

# 链接HV Toolkit 
target_link_libraries(hv_example
    PRIVATE
        HVToolkit::hv_camera
        ${OpenCV_LIBS}
)
