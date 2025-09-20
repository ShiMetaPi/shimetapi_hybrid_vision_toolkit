cmake_minimum_required(VERSION 3.10)
project(hv_evs_recorder_sample)

# 设置C++标准
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找依赖库
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED libusb-1.0)

# 包含目录
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${LIBUSB_INCLUDE_DIRS}
)

# 链接目录
link_directories(
    ${LIBUSB_LIBRARY_DIRS}
)

# 源文件
set(SOURCES
    hv_evs_recorder_sample.cpp
    ../../src/hv_evs_recorder.cpp
    ../../src/hv_usb_device.cpp
)

# 创建可执行文件
add_executable(${PROJECT_NAME} ${SOURCES})

# 链接库
target_link_libraries(${PROJECT_NAME}
    ${LIBUSB_LIBRARIES}
    pthread
)

# 编译选项
target_compile_options(${PROJECT_NAME} PRIVATE
    ${LIBUSB_CFLAGS_OTHER}
    -Wall
    -Wextra
    -O2
)

# 安装
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)
