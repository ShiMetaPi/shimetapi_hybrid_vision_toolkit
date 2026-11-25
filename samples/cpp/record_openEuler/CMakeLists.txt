# APX014BA USB recording policy tested on openEuler RK3588.
# Included only for OPENEULER_PLATFORM builds.
add_executable(hv_sample_record_openEuler main.cpp)
target_link_libraries(hv_sample_record_openEuler PRIVATE
    HVToolkit::shimetapi_hv HVToolkit::shimetapi_codec HVToolkit::shimetapi_io)

# Resolve shared SDK libraries from the build root.
set_target_properties(hv_sample_record_openEuler PROPERTIES
    BUILD_RPATH_USE_ORIGIN TRUE BUILD_RPATH "$ORIGIN/../../../")
