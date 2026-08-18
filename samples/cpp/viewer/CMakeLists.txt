add_executable(hv_sample_viewer main.cpp)
target_link_libraries(hv_sample_viewer PRIVATE
    HVToolkit::shimetapi_hv HVToolkit::shimetapi_codec HVToolkit::shimetapi_io)
