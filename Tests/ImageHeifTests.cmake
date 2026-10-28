# Tests/ImageHeifTests.cmake
# ImageHeifTest: HEIF and AVIF through the framework's image pipeline - the
# repository's samples decoded through UCImageRaster, and a lossy and a
# lossless file from the export encoder and UCImageRaster::Save read back,
# for each format this build's libheif decodes and encodes. UC_HEIF_EXPECT
# (set by CI per row) names what a platform must and must not do. Headless;
# reports SKIP on a libvips built without libheif. Included by
# Tests/CMakeLists.txt under BUILD_TESTS, and by the top-level CMakeLists.txt
# on its own under ULTRACANVAS_BUILD_HEIF_TESTS - so the macOS and Windows CI
# rows, which build no full test suite, check the libheif they ship. Paths
# are relative to this file, so either includer works.

set(_HEIF_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(_HEIF_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/../UltraCanvas/include")
# On Windows beside the core DLL (libUltraCanvas.dll, at the build root like
# the apps), where an executable finds it; build/bin is on no search path.
if(WIN32)
    set(_HEIF_BIN_DIR "$<TARGET_FILE_DIR:UltraCanvas>")
else()
    set(_HEIF_BIN_DIR "${CMAKE_BINARY_DIR}/bin")
endif()

if(TARGET UltraCanvas AND NOT EMSCRIPTEN)
    message(STATUS "  Building ImageHeifTest...")
    add_executable(ImageHeifTest
        ${_HEIF_DIR}/ImageHeifTest.cpp
    )
    target_include_directories(ImageHeifTest PRIVATE ${_HEIF_INCLUDE_DIR})
    target_compile_features(ImageHeifTest PRIVATE cxx_std_20)
    target_link_libraries(ImageHeifTest PRIVATE UltraCanvas)
    # The samples it decodes: media/images/dice.heic and dice.avif.
    target_compile_definitions(ImageHeifTest PRIVATE
        UC_MEDIA_DIR="${CMAKE_CURRENT_LIST_DIR}/../media")
    set_target_properties(ImageHeifTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${_HEIF_BIN_DIR}
    )
    add_test(NAME ImageHeifTest COMMAND ImageHeifTest
             WORKING_DIRECTORY ${_HEIF_BIN_DIR})
    message(STATUS "    Test registered: ImageHeifTest")
else()
    message(STATUS "  ImageHeifTest skipped (UltraCanvas target not present)")
endif()
