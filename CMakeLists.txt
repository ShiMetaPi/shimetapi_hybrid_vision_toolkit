cmake_minimum_required(VERSION 3.16)
project(hv_toolkit VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)

# ---- 预编译库选择（按目标架构）----
# x86_64: USB + Ethernet 后端；s100(aarch64): MIPI + Ethernet 后端。
if("${CMAKE_SYSTEM_PROCESSOR}" MATCHES "x86_64|amd64|AMD64")
    set(HV_TOOLKIT_ARCH_DIR "x86_64")
elseif("${CMAKE_SYSTEM_PROCESSOR}" MATCHES "aarch64|arm64")
    set(HV_TOOLKIT_ARCH_DIR "s100")
else()
    message(FATAL_ERROR "Unsupported architecture: ${CMAKE_SYSTEM_PROCESSOR} (prebuilt libs: x86_64, s100/aarch64)")
endif()

set(HV_TOOLKIT_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib/${HV_TOOLKIT_ARCH_DIR}" CACHE PATH "Prebuilt library directory")
set(HV_TOOLKIT_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")

message(STATUS "HV Toolkit prebuilt: arch=${HV_TOOLKIT_ARCH_DIR} libs=${HV_TOOLKIT_LIB_DIR}")

# ---- IMPORTED 目标（与源码构建版同名同接口，用户 CMake 无缝切换）----
foreach(_comp core codec io hv)
    set(_lib "${HV_TOOLKIT_LIB_DIR}/libshimetapi_${_comp}.so")
    if(NOT EXISTS "${_lib}")
        message(FATAL_ERROR "Prebuilt library not found: ${_lib}")
    endif()
    add_library(shimetapi_${_comp} SHARED IMPORTED)
    set_target_properties(shimetapi_${_comp} PROPERTIES
        IMPORTED_LOCATION "${_lib}"
        INTERFACE_INCLUDE_DIRECTORIES "${HV_TOOLKIT_INCLUDE_DIR}"
    )
    add_library(HVToolkit::shimetapi_${_comp} ALIAS shimetapi_${_comp})
endforeach()
# 依赖链与源码构建版一致：io -> core+codec；hv -> core+codec
set_target_properties(shimetapi_io PROPERTIES
    INTERFACE_LINK_LIBRARIES "HVToolkit::shimetapi_core;HVToolkit::shimetapi_codec")
set_target_properties(shimetapi_hv PROPERTIES
    INTERFACE_LINK_LIBRARIES "HVToolkit::shimetapi_core;HVToolkit::shimetapi_codec")

# 运行时 rpath 指向预编译库目录，样例免设 LD_LIBRARY_PATH（install 后由系统 ldconfig / rpath 解析）
foreach(_comp core codec io hv)
    set_target_properties(shimetapi_${_comp} PROPERTIES
        INTERFACE_LINK_DIRECTORIES "${HV_TOOLKIT_LIB_DIR}")
endforeach()

# ---- 样例 ----
option(BUILD_SAMPLES "Build sample programs" ON)
if(BUILD_SAMPLES)
    add_subdirectory(samples)
endif()

# ---- 安装规则 ----
install(DIRECTORY "${HV_TOOLKIT_INCLUDE_DIR}/shimetapi/"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/shimetapi
        FILES_MATCHING PATTERN "*.h")

file(GLOB _hv_libs "${HV_TOOLKIT_LIB_DIR}/*.so*")
install(FILES ${_hv_libs}
        DESTINATION ${CMAKE_INSTALL_LIBDIR})
