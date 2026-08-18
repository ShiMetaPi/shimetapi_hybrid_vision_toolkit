# player: 离线回放 EVS(.raw) + APS(.avi) 录制文件。
# 依赖：shimetapi_io（EventReader）、OpenCV（显示窗口）。
if(CMAKE_CROSSCOMPILING)
    # 交叉编译时直接使用 toolkit 自带的 aarch64 OpenCV
    set(_AARCH64_OPENCV_DIR "${CMAKE_SOURCE_DIR}/third_party/aarch64_opencv")
    if(EXISTS "${_AARCH64_OPENCV_DIR}/include/opencv4/opencv2" AND
       EXISTS "${_AARCH64_OPENCV_DIR}/lib/aarch64-linux-gnu/libopencv_core.so")
        add_executable(hv_sample_player main.cpp player_widgets.cpp)
        target_include_directories(hv_sample_player PRIVATE
            "${_AARCH64_OPENCV_DIR}/include/opencv4")
        target_link_directories(hv_sample_player PRIVATE
            "${_AARCH64_OPENCV_DIR}/lib/aarch64-linux-gnu")
        target_link_libraries(hv_sample_player PRIVATE
            HVToolkit::shimetapi_io
            HVToolkit::shimetapi_codec
            opencv_core opencv_imgproc opencv_highgui)
        # opencv_highgui 依赖 GTK/GStreamer，板端运行时提供，交叉链接时忽略未定义符号
        target_link_options(hv_sample_player PRIVATE -Wl,--allow-shlib-undefined)
        message(STATUS "player: using aarch64 OpenCV from third_party")
    else()
        message(STATUS "aarch64 OpenCV not found in third_party — skipping hv_sample_player")
    endif()
else()
    # 本地编译：通过 find_package 查找系统 OpenCV
    find_package(OpenCV QUIET)
    if(OpenCV_FOUND)
        add_executable(hv_sample_player main.cpp player_widgets.cpp)
        target_link_libraries(hv_sample_player PRIVATE
            HVToolkit::shimetapi_io
            ${OpenCV_LIBS})
        target_include_directories(hv_sample_player PRIVATE ${OpenCV_INCLUDE_DIRS})
    else()
        message(STATUS "OpenCV not found — skipping hv_sample_player")
    endif()
endif()