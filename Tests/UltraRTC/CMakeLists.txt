# Tests/UltraRTC/CMakeLists.txt
# Builds UltraRTCTests: two peers in one process connect over host
# candidates and exchange data-channel messages. Skips itself in a build
# without an engine.

add_executable(UltraRTCTests UltraRTCTests.cpp)
target_compile_features(UltraRTCTests PRIVATE cxx_std_20)
target_link_libraries(UltraRTCTests PRIVATE UltraRTC)
add_test(NAME UltraRTCTests COMMAND UltraRTCTests)
set_tests_properties(UltraRTCTests PROPERTIES TIMEOUT 120)

# UltraRtcApiStatus: one probe per UltraRtc_* function, reporting WORKING /
# IMPLEMENTED / NOT IMPLEMENTED / BROKEN for this build on this machine (see
# Docs/Modules/UltraRTC/ApiStatus.md). The registry, the runner and the
# renderers are UltraNetApiStatus's, compiled in here: one report framework.
add_executable(UltraRtcApiStatus
    ApiStatus/ApiStatusMain.cpp
    ApiStatus/ProbeSupport.cpp
    ApiStatus/ProbeLifecycle.cpp
    ApiStatus/ProbePeers.cpp
    ApiStatus/ProbeDataChannels.cpp
    ApiStatus/ProbeAudio.cpp
    ApiStatus/ProbeVideo.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../UltraNet/ApiStatus/ApiStatusReport.cpp)
target_compile_features(UltraRtcApiStatus PRIVATE cxx_std_20)
target_link_libraries(UltraRtcApiStatus PRIVATE UltraRTC)
# Fails on BROKEN only: a machine without a camera reports IMPLEMENTED.
add_test(NAME UltraRtcApiStatus COMMAND UltraRtcApiStatus)
set_tests_properties(UltraRtcApiStatus PROPERTIES TIMEOUT 240)
# Every function in the header has a probe, and only those.
add_test(NAME UltraRtcApiStatusCoverage
    COMMAND ${CMAKE_COMMAND}
        -DHEADER=${CMAKE_CURRENT_SOURCE_DIR}/../../UltraCanvas/include/UltraRTC/UltraRTCCore.h
        -DPROBE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/ApiStatus
        -P ${CMAKE_CURRENT_SOURCE_DIR}/ApiStatus/CheckProbeCoverage.cmake)
