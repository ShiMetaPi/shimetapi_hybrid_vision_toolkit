add_executable(hv_sample_get_started main.cpp)
target_link_libraries(hv_sample_get_started PRIVATE
    HVToolkit::shimetapi_hv HVToolkit::shimetapi_codec HVToolkit::shimetapi_io)
