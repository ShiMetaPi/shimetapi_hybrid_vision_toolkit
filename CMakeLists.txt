cmake_minimum_required(VERSION 3.16)
project(hv_toolkit VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)

# ---- 预编译库选择（按目标架构）----
# x86_64: USB + Ethernet 后端；s100(aarch64): MIPI + Ethernet 后端。
# HV_TOOLKIT_ARCH 可显式覆盖（如交叉编译时指定 s100）；缺省按 CMAKE_SYSTEM_PROCESSOR 推断。
set(HV_TOOLKIT_ARCH "" CACHE STRING "Prebuilt arch override: x86_64 or s100")
if(HV_TOOLKIT_ARCH)
    if(NOT HV_TOOLKIT_ARCH MATCHES "^(x86_64|s100)$")
        message(FATAL_ERROR "Invalid HV_TOOLKIT_ARCH='${HV_TOOLKIT_ARCH}' (expected x86_64 or s100)")
    endif()
    set(HV_TOOLKIT_ARCH_DIR "${HV_TOOLKIT_ARCH}")
elseif("${CMAKE_SYSTEM_PROCESSOR}" MATCHES "x86_64|amd64|AMD64")
    set(HV_TOOLKIT_ARCH_DIR "x86_64")
elseif("${CMAKE_SYSTEM_PROCESSOR}" MATCHES "aarch64|arm64")
    set(HV_TOOLKIT_ARCH_DIR "s100")
else()
    message(FATAL_ERROR "Unsupported architecture: ${CMAKE_SYSTEM_PROCESSOR} (prebuilt libs: x86_64, s100/aarch64; or set HV_TOOLKIT_ARCH)")
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

# s100 预编译 hv 依赖 RDK 平台库（libcam/vpf/hbmem 等），仅板端存在。
# 交叉编样例时允许未定义符号（与源码构建版同策略），运行时由板端解析。
if(HV_TOOLKIT_ARCH_DIR STREQUAL "s100")
    set_target_properties(shimetapi_hv PROPERTIES
        INTERFACE_LINK_OPTIONS "-Wl,--allow-shlib-undefined")
endif()

# 样例 rpath：宿主机 lib/ 路径（就地运行）+ $ORIGIN 相对路径（整个 build 目录
# 拷上板后自包含运行，见下方 bundle_libs；相对路径在板上依然有效）。
# 样例位于 build/samples/cpp/<name>/，上 3 层即 build 根（库捆绑处）。
set(CMAKE_BUILD_RPATH "\$ORIGIN/../../..")

# ---- 样例 ----
option(BUILD_SAMPLES "Build sample programs" ON)
if(BUILD_SAMPLES)
    add_subdirectory(samples)
endif()

# ---- 构建目录自包含：把预编译库拷进 out/<arch>/build ----
# 源码仓的构建目录天然含 .so（就地编译产物）；预编译版的库在 lib/<arch>/，
# 拷一份进构建目录让两边布局一致——整个 build 目录拷上板即可运行
# （当前目录在库搜索路径内，无需 LD_LIBRARY_PATH）。
add_custom_target(bundle_libs
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${HV_TOOLKIT_LIB_DIR}/libshimetapi_core.so"
            "${HV_TOOLKIT_LIB_DIR}/libshimetapi_core.so.2"
            "${HV_TOOLKIT_LIB_DIR}/libshimetapi_core.so.2.0.0"
            "${HV_TOOLKIT_LIB_DIR}/libshimetapi_codec.so"
            "${HV_TOOLKIT_LIB_DIR}/libshimetapi_codec.so.2"
            "${HV_TOOLKIT_LIB_DIR}/libshimetapi_codec.so.2.0.0"
            "${HV_TOOLKIT_LIB_DIR}/libshimetapi_io.so"
            "${HV_TOOLKIT_LIB_DIR}/libshimetapi_io.so.2"
            "${HV_TOOLKIT_LIB_DIR}/libshimetapi_io.so.2.0.0"
            "${HV_TOOLKIT_LIB_DIR}/libshimetapi_hv.so"
            "${HV_TOOLKIT_LIB_DIR}/libshimetapi_hv.so.2"
            "${HV_TOOLKIT_LIB_DIR}/libshimetapi_hv.so.2.0.0"
            "${CMAKE_CURRENT_BINARY_DIR}"
    COMMENT "Bundling prebuilt libs into build dir (self-contained)"
)
# 样例构建后自动捆绑（样例链接的 IMPORTED 库在 lib/，运行时才需要 build/ 里的拷贝）
if(BUILD_SAMPLES)
    add_dependencies(bundle_libs
        hv_sample_get_started hv_sample_callback hv_sample_record hv_sample_viewer
        hv_sample_bench_hw hv_sample_live_record_display hv_sample_player)
endif()
add_custom_target(all_samples ALL DEPENDS bundle_libs)

# ---- 安装规则 ----
install(DIRECTORY "${HV_TOOLKIT_INCLUDE_DIR}/shimetapi/"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/shimetapi
        FILES_MATCHING PATTERN "*.h")

file(GLOB _hv_libs "${HV_TOOLKIT_LIB_DIR}/*.so*")
install(FILES ${_hv_libs}
        DESTINATION ${CMAKE_INSTALL_LIBDIR})
