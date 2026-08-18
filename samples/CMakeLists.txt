# 统一样例（get_started / callback / record / viewer / bench_hw / live_record_display / player）。
# 预编译发布版：链接根 CMakeLists 定义的 IMPORTED 目标 HVToolkit::shimetapi_*。
# player 与 live_record_display 需系统 OpenCV（缺失时自动跳过）。
# 注：源码构建版另有 bench 样例，依赖内部 StreamSession/MockDevice，不随预编译版发布。
add_subdirectory(cpp/get_started)
add_subdirectory(cpp/callback)
add_subdirectory(cpp/record)
add_subdirectory(cpp/viewer)
add_subdirectory(cpp/bench_hw)
add_subdirectory(cpp/live_record_display)
add_subdirectory(cpp/player)
