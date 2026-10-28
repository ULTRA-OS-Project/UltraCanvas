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
    # libheif's own encoder list, for UC_HEIF_EXPECT's encoder= and
    # no-encoder= (which HEVC encoder this build has: x265 is GPL). The
    # compiler's own directories are left out of the include path, as for
    # ImageCairo.cpp in UltraCanvas/CMakeLists.txt.
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(UC_HEIF_TEST_LIBHEIF QUIET libheif)
    endif()
    if(UC_HEIF_TEST_LIBHEIF_FOUND)
        set(_heif_test_includes ${UC_HEIF_TEST_LIBHEIF_INCLUDE_DIRS})
        if(_heif_test_includes AND CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES)
            list(REMOVE_ITEM _heif_test_includes ${CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES})
        endif()
        target_include_directories(ImageHeifTest PRIVATE ${_heif_test_includes})
        target_link_directories(ImageHeifTest PRIVATE ${UC_HEIF_TEST_LIBHEIF_LIBRARY_DIRS})
        target_link_libraries(ImageHeifTest PRIVATE ${UC_HEIF_TEST_LIBHEIF_LIBRARIES})
        target_compile_definitions(ImageHeifTest PRIVATE HAS_LIBHEIF=1)
    endif()
    set_target_properties(ImageHeifTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${_HEIF_BIN_DIR}
    )
    add_test(NAME ImageHeifTest COMMAND ImageHeifTest
             WORKING_DIRECTORY ${_HEIF_BIN_DIR})
    message(STATUS "    Test registered: ImageHeifTest")
else()
    message(STATUS "  ImageHeifTest skipped (UltraCanvas target not present)")
endif()
