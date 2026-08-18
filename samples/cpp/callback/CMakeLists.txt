add_executable(hv_sample_callback main.cpp)
target_link_libraries(hv_sample_callback PRIVATE
    HVToolkit::shimetapi_hv HVToolkit::shimetapi_codec HVToolkit::shimetapi_io)
