# Host playback of board recordings, with Qt-player-style controls.
if(CMAKE_CROSSCOMPILING)
    message(STATUS "recording_player: host desktop sample; skipping cross-build")
    return()
endif()
include(CheckCXXSourceCompiles)
set(CMAKE_REQUIRED_INCLUDES "${HV_TOOLKIT_INCLUDE_DIR}")
set(CMAKE_REQUIRED_LIBRARIES
    "${HV_TOOLKIT_LIB_DIR}/libshimetapi_codec.so"
    "${HV_TOOLKIT_LIB_DIR}/libshimetapi_core.so")
unset(HV_RECORDING_PLAYER_CODEC_API CACHE)
check_cxx_source_compiles("#include <shimetapi/codec/evs_packet_codec.h>
int main() { return int(Shimeta::codec::evsPacketSize(nullptr, 0)); }"
    HV_RECORDING_PLAYER_CODEC_API)
unset(CMAKE_REQUIRED_INCLUDES)
unset(CMAKE_REQUIRED_LIBRARIES)
if(NOT HV_RECORDING_PLAYER_CODEC_API)
    message(STATUS "recording_player: prebuilt codec needs evsPacketSize; skipping")
    return()
endif()
find_package(OpenCV QUIET COMPONENTS core imgproc highgui imgcodecs videoio)
if(OpenCV_FOUND)
    add_executable(hv_sample_recording_player main.cpp recording.cpp)
    target_include_directories(hv_sample_recording_player PRIVATE ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(hv_sample_recording_player PRIVATE
        HVToolkit::shimetapi_io HVToolkit::shimetapi_codec ${OpenCV_LIBS})
    set_target_properties(hv_sample_recording_player PROPERTIES
        BUILD_RPATH_USE_ORIGIN TRUE BUILD_RPATH "$ORIGIN/../../../")
else()
    message(STATUS "recording_player: OpenCV desktop components unavailable; skipping")
endif()
