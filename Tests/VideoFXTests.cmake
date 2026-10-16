# Tests/VideoFXTests.cmake
# VideoFXTest, the VideoFX engine's suite. Included by Tests/CMakeLists.txt
# under BUILD_TESTS, and by the top-level CMakeLists.txt on its own under
# ULTRACANVAS_BUILD_VIDEOFX_TESTS - so the Windows CI rows, which build no
# full test suite, run the engine on the FFmpeg the Windows package ships
# (MSYS2's) and through the Windows paths: UTF-16 file names, the per-user
# cache folder, the fonts under C:\Windows\Fonts. Paths are relative to this
# file, so either includer works.

# ===== VIDEOFX TEST =====
# Effect -> filter text translation always; with FFmpeg the engine end to end
# on clips it generates itself (probe, frames, trim, speed, joins, GIF / WAV,
# lossless cut, cancel, background job, UTF-8 names, error codes).
# It goes where VideoFX puts its DLL (VIDEOFX_RUNTIME_DIR: bin/, the build
# root on Windows), so Windows finds the DLL beside it.
if(TARGET VideoFX)
    message(STATUS "  Building VideoFXTest...")
    add_executable(VideoFXTest ${CMAKE_CURRENT_LIST_DIR}/VideoFXTest.cpp)
    target_include_directories(VideoFXTest PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../VideoFX/core            # VideoFXFilterBuilder.h (internal)
        ${CMAKE_CURRENT_LIST_DIR}/../UltraCanvas/include     # UltraCanvasPathUtf8.h (header-only)
    )
    target_link_libraries(VideoFXTest PRIVATE VideoFX::VideoFX)
    # A real face for the face detector's test (public domain, see its README)
    target_compile_definitions(VideoFXTest PRIVATE
        VIDEOFX_TEST_DATA_DIR="${CMAKE_CURRENT_LIST_DIR}/data/videofx")
    set_target_properties(VideoFXTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${VIDEOFX_RUNTIME_DIR}
    )
    add_test(NAME VideoFXTest COMMAND VideoFXTest
             WORKING_DIRECTORY ${VIDEOFX_RUNTIME_DIR})
    message(STATUS "    Test registered: VideoFXTest")
endif()
