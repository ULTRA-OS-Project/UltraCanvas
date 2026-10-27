# Tests/ImageJxlTests.cmake
# ImageJxlTest: JPEG XL through the framework's image pipeline - the format
# inventory, a lossless and a lossy .jxl from the export encoder read back
# through UCImageRaster, the quality option, and UCImageRaster::Save.
# Headless; reports SKIP on a libvips built without libjxl. Included by
# Tests/CMakeLists.txt under BUILD_TESTS, and by the top-level CMakeLists.txt
# on its own under ULTRACANVAS_BUILD_JXL_TESTS - so the macOS and Windows CI
# rows, which build no full test suite, check that the libvips they ship
# writes and reads JPEG XL. Paths are relative to this file, so either
# includer works.

set(_JXL_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(_JXL_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/../UltraCanvas/include")
# On Windows beside the core DLL (libUltraCanvas.dll, at the build root like
# the apps), where an executable finds it; build/bin is on no search path.
if(WIN32)
    set(_JXL_BIN_DIR "$<TARGET_FILE_DIR:UltraCanvas>")
else()
    set(_JXL_BIN_DIR "${CMAKE_BINARY_DIR}/bin")
endif()

if(TARGET UltraCanvas AND NOT EMSCRIPTEN)
    message(STATUS "  Building ImageJxlTest...")
    add_executable(ImageJxlTest
        ${_JXL_DIR}/ImageJxlTest.cpp
    )
    target_include_directories(ImageJxlTest PRIVATE ${_JXL_INCLUDE_DIR})
    target_compile_features(ImageJxlTest PRIVATE cxx_std_20)
    target_link_libraries(ImageJxlTest PRIVATE UltraCanvas)
    set_target_properties(ImageJxlTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${_JXL_BIN_DIR}
    )
    add_test(NAME ImageJxlTest COMMAND ImageJxlTest
             WORKING_DIRECTORY ${_JXL_BIN_DIR})
    message(STATUS "    Test registered: ImageJxlTest")
else()
    message(STATUS "  ImageJxlTest skipped (UltraCanvas target not present)")
endif()
