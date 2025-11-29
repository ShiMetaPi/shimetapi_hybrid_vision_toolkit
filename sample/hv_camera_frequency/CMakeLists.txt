cmake_minimum_required(VERSION 3.10)
project(hv_camera_frequency)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找必要的包
find_package(OpenCV REQUIRED)
find_package(PkgConfig REQUIRED)

# 查找Metavision SDK
find_package(MetavisionSDK COMPONENTS core REQUIRED)

# 设置包含目录
include_directories(
    ${CMAKE_SOURCE_DIR}/../../include
    ${OpenCV_INCLUDE_DIRS}
    ${MetavisionSDK_INCLUDE_DIRS}
)

# 设置库目录
link_directories(
    ${CMAKE_SOURCE_DIR}/../../lib
)

# 创建可执行文件
add_executable(${PROJECT_NAME} hv_camera_frequency.cpp)

# 链接库
target_link_libraries(${PROJECT_NAME}
    hv_camera
    ${OpenCV_LIBS}
    ${MetavisionSDK_LIBRARIES}
    pthread
)

# 设置编译选项
target_compile_options(${PROJECT_NAME} PRIVATE
    -Wall
    -Wextra
    -O2
)

# 设置输出目录
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

# 打印一些有用的信息
message(STATUS "OpenCV version: ${OpenCV_VERSION}")
message(STATUS "OpenCV include dirs: ${OpenCV_INCLUDE_DIRS}")
message(STATUS "OpenCV libraries: ${OpenCV_LIBS}")
message(STATUS "MetavisionSDK include dirs: ${MetavisionSDK_INCLUDE_DIRS}")
message(STATUS "MetavisionSDK libraries: ${MetavisionSDK_LIBRARIES}")
