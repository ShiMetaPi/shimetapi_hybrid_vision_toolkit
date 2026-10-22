# 公开示例；bench 依赖内部接口，不随预编译版发布。
add_subdirectory(cpp/get_started)
add_subdirectory(cpp/callback)
add_subdirectory(cpp/record)
add_subdirectory(cpp/viewer)
add_subdirectory(cpp/bench_hw)
add_subdirectory(cpp/live_record_display)
add_subdirectory(cpp/player)
# 本次 USB 示例与更新后的 x86_64 库一起发布。
if(HV_TOOLKIT_ARCH_DIR STREQUAL "x86_64")
    add_subdirectory(cpp/fps_control)
    add_subdirectory(cpp/ts_sync)
endif()
