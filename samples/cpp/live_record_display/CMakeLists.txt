# live_record_display: MIPI-HVS 实时预览 + 按键录制（需 OpenCV）
if(CMAKE_CROSSCOMPILING)
    # 交叉编译时直接使用 toolkit 自带的 aarch64 OpenCV
    set(_AARCH64_OPENCV_DIR "${CMAKE_SOURCE_DIR}/third_party/aarch64_opencv")
    if(EXISTS "${_AARCH64_OPENCV_DIR}/include/opencv4/opencv2" AND
       EXISTS "${_AARCH64_OPENCV_DIR}/lib/aarch64-linux-gnu/libopencv_core.so")
        add_executable(hv_sample_live_record_display main.cpp live_widgets.cpp)
        target_include_directories(hv_sample_live_record_display PRIVATE
            "${_AARCH64_OPENCV_DIR}/include/opencv4")
        target_link_directories(hv_sample_live_record_display PRIVATE
            "${_AARCH64_OPENCV_DIR}/lib/aarch64-linux-gnu")
        target_link_libraries(hv_sample_live_record_display PRIVATE
            HVToolkit::shimetapi_hv
            HVToolkit::shimetapi_codec
            HVToolkit::shimetapi_io
            opencv_core opencv_imgproc opencv_highgui opencv_imgcodecs opencv_videoio)
        message(STATUS "live_record_display: using aarch64 OpenCV from third_party")
    else()
        message(STATUS "aarch64 OpenCV not found in third_party — skipping hv_sample_live_record_display")
    endif()
else()
    # 本地编译：通过 find_package 查找系统 OpenCV
    find_package(OpenCV QUIET)
    if(OpenCV_FOUND)
        add_executable(hv_sample_live_record_display main.cpp live_widgets.cpp)
        target_link_libraries(hv_sample_live_record_display PRIVATE
            HVToolkit::shimetapi_hv
            HVToolkit::shimetapi_codec
            HVToolkit::shimetapi_io
            ${OpenCV_LIBS})
        target_include_directories(hv_sample_live_record_display PRIVATE ${OpenCV_INCLUDE_DIRS})
    else()
        message(STATUS "OpenCV not found — skipping hv_sample_live_record_display")
    endif()
endif()