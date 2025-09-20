cmake_minimum_required(VERSION 3.16)
project(hv_camera_record VERSION 1.0.0 LANGUAGES CXX)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置库文件路径
set(HV_TOOLKIT_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../lib")
set(HV_TOOLKIT_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../include")

# 添加头文件路径
include_directories(${HV_TOOLKIT_INCLUDE_DIR})

# 查找OpenCV
find_package(OpenCV REQUIRED)

# 查找Metavision SDK
find_package(MetavisionSDK COMPONENTS core base REQUIRED)

# 注释掉HVToolkit的find_package调用，直接使用本地库文件
# find_package(HVToolkit REQUIRED)

# 查找libusb
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED libusb-1.0)

# 创建录制示例可执行文件
add_executable(hv_camera_record hv_camera_record.cpp)

# 链接库文件
target_link_libraries(hv_camera_record
    PRIVATE
        ${HV_TOOLKIT_LIB_DIR}/libhv_camera.so
        ${HV_TOOLKIT_LIB_DIR}/libhv_event_writer.so
        ${OpenCV_LIBS}
        MetavisionSDK::core
        MetavisionSDK::base
        ${LIBUSB_LIBRARIES}
)

# 添加编译选项
target_include_directories(hv_camera_record PRIVATE ${LIBUSB_INCLUDE_DIRS})
target_compile_options(hv_camera_record PRIVATE ${LIBUSB_CFLAGS_OTHER})

