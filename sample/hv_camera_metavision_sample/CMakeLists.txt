cmake_minimum_required(VERSION 3.10)
project(hv_camera_metavision_sample VERSION 1.0.0 LANGUAGES CXX)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找HV Toolkit 
find_package(HVToolkit REQUIRED)

# 查找Metavision SDK
find_package(MetavisionSDK REQUIRED COMPONENTS core ui stream)

# 添加可执行文件
add_executable(hv_camera_metavision_sample hv_camera_metavision_sample.cpp)

# 添加Metavision SDK头文件搜索路径
target_include_directories(hv_camera_metavision_sample PRIVATE
    ${MetavisionSDK_INCLUDE_DIRS}
)

# 链接库 
target_link_libraries(hv_camera_metavision_sample
    PRIVATE
        HVToolkit::hv_camera
        ${MetavisionSDK_LIBRARIES}
        pthread
)



