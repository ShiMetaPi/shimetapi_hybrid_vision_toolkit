cmake_minimum_required(VERSION 3.10)
project(RawDataProcessor)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找OpenCV
find_package(OpenCV REQUIRED)

# 设置HV Toolkit路径
# 从 sample/hv_raw_processor 到根目录需要 ../../
set(HV_TOOLKIT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(HV_TOOLKIT_INCLUDE_DIR "${HV_TOOLKIT_ROOT}/include")
set(HV_TOOLKIT_LIB_DIR "${HV_TOOLKIT_ROOT}/lib")

# 如果上述路径不存在，尝试从build目录计算
if(NOT EXISTS "${HV_TOOLKIT_INCLUDE_DIR}/hv_event_reader.h")
    # 从 build 目录到根目录需要 ../../../
    set(HV_TOOLKIT_ROOT "${CMAKE_CURRENT_BINARY_DIR}/../../..")
    set(HV_TOOLKIT_INCLUDE_DIR "${HV_TOOLKIT_ROOT}/include")
    set(HV_TOOLKIT_LIB_DIR "${HV_TOOLKIT_ROOT}/lib")
endif()

# 调试信息：打印路径
message(STATUS "CMAKE_CURRENT_SOURCE_DIR: ${CMAKE_CURRENT_SOURCE_DIR}")
message(STATUS "HV_TOOLKIT_ROOT: ${HV_TOOLKIT_ROOT}")
message(STATUS "HV_TOOLKIT_INCLUDE_DIR: ${HV_TOOLKIT_INCLUDE_DIR}")
message(STATUS "HV_TOOLKIT_LIB_DIR: ${HV_TOOLKIT_LIB_DIR}")

# 检查HV Toolkit头文件是否存在
if(NOT EXISTS "${HV_TOOLKIT_INCLUDE_DIR}/hv_event_reader.h")
    message(FATAL_ERROR "HV Toolkit headers not found at: ${HV_TOOLKIT_INCLUDE_DIR}")
else()
    message(STATUS "Found HV Toolkit headers at: ${HV_TOOLKIT_INCLUDE_DIR}")
endif()

# 查找Metavision SDK
# 如果Metavision SDK安装在标准位置，CMake应该能找到它
# 否则可能需要设置Metavision_DIR变量
find_package(MetavisionSDK COMPONENTS base QUIET)

if(NOT MetavisionSDK_FOUND)
    # 如果找不到Metavision SDK，尝试手动设置路径
    message(WARNING "Metavision SDK not found automatically. Please set Metavision_DIR if needed.")
    
    # 尝试常见的安装路径
    set(POSSIBLE_METAVISION_PATHS
        "/usr/local/include"
        "/opt/metavision/include"
        "C:/Program Files/Prophesee/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/../openeb-main/sdk/modules/base/cpp/include"
    )
    
    foreach(PATH ${POSSIBLE_METAVISION_PATHS})
        if(EXISTS "${PATH}/metavision/sdk/base/events/event_cd.h")
            set(METAVISION_INCLUDE_DIR ${PATH})
            message(STATUS "Found Metavision headers at: ${PATH}")
            break()
        endif()
    endforeach()
    
    if(NOT METAVISION_INCLUDE_DIR)
        message(FATAL_ERROR "Could not find Metavision SDK headers. Please install Metavision SDK or set the correct path.")
    endif()
else()
    message(STATUS "Found Metavision SDK")
endif()

# 创建可执行文件
add_executable(hv_raw_data_processor hv_raw_data_processor.cpp)

# 设置包含目录和链接库 - hv_raw_data_processor
target_include_directories(hv_raw_data_processor PRIVATE ${HV_TOOLKIT_INCLUDE_DIR})
if(MetavisionSDK_FOUND)
    target_compile_definitions(hv_raw_data_processor PRIVATE METAVISION_SDK_AVAILABLE)
    target_link_libraries(hv_raw_data_processor ${OpenCV_LIBS} MetavisionSDK::base)
else()
    target_include_directories(hv_raw_data_processor PRIVATE ${METAVISION_INCLUDE_DIR})
    target_link_libraries(hv_raw_data_processor ${OpenCV_LIBS})
endif()



# 编译选项 - hv_raw_data_processor
target_compile_options(hv_raw_data_processor PRIVATE
    -Wall
    -Wextra
    -O3
    -march=native
)



# 如果是Debug模式，添加调试信息
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(hv_raw_data_processor PRIVATE -g)
endif()

message(STATUS "OpenCV version: ${OpenCV_VERSION}")
message(STATUS "OpenCV libraries: ${OpenCV_LIBS}")

