# Tests/UltraRTC/CMakeLists.txt
# Builds UltraRTCTests: two peers in one process connect over host
# candidates and exchange data-channel messages. Skips itself in a build
# without an engine.

add_executable(UltraRTCTests UltraRTCTests.cpp)
target_compile_features(UltraRTCTests PRIVATE cxx_std_20)
target_link_libraries(UltraRTCTests PRIVATE UltraRTC)
add_test(NAME UltraRTCTests COMMAND UltraRTCTests)
set_tests_properties(UltraRTCTests PROPERTIES TIMEOUT 120)
