# Tests/UltraPress/CMakeLists.txt
# Builds UltraPressEngineTests. Enabled by ULTRACANVAS_BUILD_ULTRAPRESS_TESTS.
# The engine links the core library for UltraCanvasJSON and the HTMLReader,
# so the suite does too; it opens no window and needs no display.
cmake_minimum_required(VERSION 3.16)

add_executable(UltraPressEngineTests UltraPressEngineTests.cpp)
target_compile_features(UltraPressEngineTests PRIVATE cxx_std_20)
target_link_libraries(UltraPressEngineTests PRIVATE UltraPressEngine ${ULTRACANVAS_LIBRARY})
target_include_directories(UltraPressEngineTests PRIVATE ${CMAKE_SOURCE_DIR}/UltraCanvas/include)
target_compile_definitions(UltraPressEngineTests PRIVATE
    ULTRAPRESS_EXAMPLE_DIR="${CMAKE_SOURCE_DIR}/Apps/UltraPress/examples/ultraos"
    ULTRAPRESS_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
set_target_properties(UltraPressEngineTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
include(CTest)
add_test(NAME UltraPressEngine COMMAND UltraPressEngineTests
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# The renderer: pages on real elements, built without a window.
# The editor window's model side (ui/UltraPressWindow.cpp) is compiled in
# too: without Build() it edits the site and saves, and that is tested.
add_executable(UltraPressRenderTests UltraPressRenderTests.cpp
    ${CMAKE_SOURCE_DIR}/Apps/UltraPress/ui/UltraPressWindow.cpp)
target_compile_features(UltraPressRenderTests PRIVATE cxx_std_20)
target_link_libraries(UltraPressRenderTests PRIVATE UltraPressRender ${ULTRACANVAS_LIBRARY})
target_include_directories(UltraPressRenderTests PRIVATE
    ${CMAKE_SOURCE_DIR}/UltraCanvas/include
    ${CMAKE_SOURCE_DIR}/Apps/UltraPress/ui)
target_compile_definitions(UltraPressRenderTests PRIVATE
    ULTRAPRESS_EXAMPLE_DIR="${CMAKE_SOURCE_DIR}/Apps/UltraPress/examples/ultraos")
set_target_properties(UltraPressRenderTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
add_test(NAME UltraPressRender COMMAND UltraPressRenderTests
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
