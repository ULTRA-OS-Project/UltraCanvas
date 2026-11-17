# Tests/UltraSite/CMakeLists.txt
# Builds UltraSiteTests. Enabled by ULTRACANVAS_BUILD_ULTRASITE_TESTS. The
# builder is headless: the suite opens no window and needs no display.
cmake_minimum_required(VERSION 3.16)

# On Windows beside the core DLL, which an executable finds in its own
# directory; elsewhere build/bin (as Tests/UltraPress does).
if(WIN32)
    set(_UST_BIN_DIR "$<TARGET_FILE_DIR:${ULTRACANVAS_LIBRARY}>")
else()
    set(_UST_BIN_DIR "${CMAKE_BINARY_DIR}/bin")
endif()

add_executable(UltraSiteTests UltraSiteTests.cpp)
target_compile_features(UltraSiteTests PRIVATE cxx_std_20)
target_link_libraries(UltraSiteTests PRIVATE UltraSite ${ULTRACANVAS_LIBRARY})
set_target_properties(UltraSiteTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${_UST_BIN_DIR})
# A photograph of the repository's (1000 x 563) for the picture variants.
target_compile_definitions(UltraSiteTests PRIVATE ULTRASITE_TEST_PICTURE="${CMAKE_SOURCE_DIR}/media/images/landscape.jpg")
include(CTest)
add_test(NAME UltraSite COMMAND UltraSiteTests WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
