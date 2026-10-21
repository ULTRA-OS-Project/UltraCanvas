# Tests/EmojiTests.cmake
# EmojiSettingsTest: Emoji.conf read and written, the generic font aliases,
# and "👤 Someone" laid out with the bundled fonts alone - its emoji from the
# bundled font the setting picks (Noto Color Emoji, Noto Emoji), never a box
# of hex digits. Included by Tests/CMakeLists.txt under BUILD_TESTS, and by the
# top-level CMakeLists.txt on its own under ULTRACANVAS_BUILD_EMOJI_TESTS - so
# the Windows CI row, which builds no full test suite, runs it on the platform
# where the emoji drew as boxes. fontconfig only: macOS draws Apple Color Emoji.
# Paths are relative to this file, so either includer works.

set(_ET_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(_ET_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/../UltraCanvas/include")
# On Windows beside the core DLL (libUltraCanvas.dll, at the build root like
# the apps), where an executable finds it; build/bin is on no search path.
if(WIN32)
    set(_ET_BIN_DIR "$<TARGET_FILE_DIR:UltraCanvas>")
else()
    set(_ET_BIN_DIR "${CMAKE_BINARY_DIR}/bin")
endif()

if(TARGET UltraCanvas AND NOT APPLE AND NOT EMSCRIPTEN)
    message(STATUS "  Building EmojiSettingsTest...")
    add_executable(EmojiSettingsTest
        ${_ET_DIR}/EmojiSettingsTest.cpp
    )
    target_include_directories(EmojiSettingsTest PRIVATE ${_ET_INCLUDE_DIR})
    target_compile_features(EmojiSettingsTest PRIVATE cxx_std_20)
    target_link_libraries(EmojiSettingsTest PRIVATE UltraCanvas)
    set_target_properties(EmojiSettingsTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${_ET_BIN_DIR}
    )
    add_test(NAME EmojiSettingsTest COMMAND EmojiSettingsTest
             WORKING_DIRECTORY ${_ET_BIN_DIR})
    message(STATUS "    Test registered: EmojiSettingsTest")
else()
    message(STATUS "  EmojiSettingsTest skipped (UltraCanvas target not present, or macOS)")
endif()
