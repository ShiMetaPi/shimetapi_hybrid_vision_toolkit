find_package(OpenCV QUIET)
if(NOT OpenCV_FOUND)
    message(STATUS "OpenCV not found — skipping hv_sample_ts_sync")
    return()
endif()
add_executable(hv_sample_ts_sync main.cpp)
target_include_directories(hv_sample_ts_sync PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(hv_sample_ts_sync PRIVATE
    HVToolkit::shimetapi_hv HVToolkit::shimetapi_codec HVToolkit::shimetapi_io ${OpenCV_LIBS})
