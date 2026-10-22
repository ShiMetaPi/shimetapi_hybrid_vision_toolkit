add_executable(hv_sample_fps_control main.cpp)
target_link_libraries(hv_sample_fps_control PRIVATE
    HVToolkit::shimetapi_hv HVToolkit::shimetapi_codec HVToolkit::shimetapi_io)
