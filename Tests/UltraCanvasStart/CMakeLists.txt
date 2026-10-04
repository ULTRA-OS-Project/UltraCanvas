# Tests/UltraCanvasStart/CMakeLists.txt
# Builds the UltraCanvasStartEngineTests binary. Enabled by
# ULTRACANVAS_BUILD_ULTRACANVASSTART_TESTS.

cmake_minimum_required(VERSION 3.16)

add_executable(UltraCanvasStartEngineTests
    test_main.cpp
    test_types.cpp
    test_system.cpp
    test_packages.cpp
    test_plan.cpp
    test_project.cpp
)
target_compile_features(UltraCanvasStartEngineTests PRIVATE cxx_std_20)
target_include_directories(UltraCanvasStartEngineTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${ULTRACANVAS_INCLUDE_DIRS})
target_link_libraries(UltraCanvasStartEngineTests PRIVATE UltraCanvasStartEngine)

include(CTest)
add_test(NAME UltraCanvasStartEngine COMMAND UltraCanvasStartEngineTests)
