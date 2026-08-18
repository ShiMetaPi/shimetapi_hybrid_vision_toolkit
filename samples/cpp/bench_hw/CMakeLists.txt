# bench_hw: timed real-HW USB bench (Camera public API + Evt2Decoder).
add_executable(hv_sample_bench_hw main.cpp)
target_link_libraries(hv_sample_bench_hw PRIVATE
    HVToolkit::shimetapi_hv
    HVToolkit::shimetapi_codec)
