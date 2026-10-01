# Tests/FilerTests.cmake
# FilerFolderPreviewTest and FilerNameEncodingTest. Included by
# Tests/CMakeLists.txt under BUILD_TESTS, and by the top-level CMakeLists.txt
# on its own under ULTRACANVAS_BUILD_FILER_TESTS - so the Windows CI row,
# which builds no full test suite, runs the file display against Thai, CJK and
# emoji names on the platform where a name goes through UTF-16 and a code page
# (the runner's is 1252). Paths are relative to this file, so either includer
# works.

set(_FT_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(_FT_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/../UltraCanvas/include")
# Where the test executables go. On Windows beside the core DLL
# (libUltraCanvas.dll, at the build root like the apps): an executable finds
# its DLLs in its own directory, and build/bin is on no search path - a test
# there fails to start with STATUS_DLL_NOT_FOUND (0xc0000135).
if(WIN32)
    set(_FT_BIN_DIR "$<TARGET_FILE_DIR:UltraCanvas>")
else()
    set(_FT_BIN_DIR "${CMAKE_BINARY_DIR}/bin")
endif()

# ===== FILER FOLDER PREVIEW TEST =====
# Display > Folder previews: the geometry of the cards that peek out of a
# folder icon (UltraCanvasFilerWidget::FolderPreviewCardRects) - at most two,
# inside the box, above the front flap, front card right of and lower than the
# back one, and a pure function of the box so the draw and the prefetch ask
# the thumbnail cache for the same size.
if(TARGET UltraCanvas)
    message(STATUS "  Building FilerFolderPreviewTest...")
    add_executable(FilerFolderPreviewTest
        ${_FT_DIR}/FilerFolderPreviewTest.cpp
    )
    target_include_directories(FilerFolderPreviewTest PRIVATE ${_FT_INCLUDE_DIR})
    target_compile_features(FilerFolderPreviewTest PRIVATE cxx_std_20)
    target_link_libraries(FilerFolderPreviewTest PRIVATE UltraCanvas)
    set_target_properties(FilerFolderPreviewTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${_FT_BIN_DIR}
    )
    add_test(NAME FilerFolderPreviewTest COMMAND FilerFolderPreviewTest
             WORKING_DIRECTORY ${_FT_BIN_DIR})
    message(STATUS "    Test registered: FilerFolderPreviewTest")
else()
    message(STATUS "  FilerFolderPreviewTest skipped (UltraCanvas target not present)")
endif()

# ===== FILER NAME ENCODING TEST =====
# The file display draws German umlauts, Thai, Russian and Chinese names
# intact, and a name in a legacy code page (Latin-1, or IBM437 out of a
# Windows ZIP) decoded instead of as U+FFFD ("Namens•nderung"). Its last part
# writes such names to a real folder and lists it through the widget - on
# Windows, through the UTF-16 file APIs a code-page conversion would break.
if(TARGET UltraCanvas)
    message(STATUS "  Building FilerNameEncodingTest...")
    add_executable(FilerNameEncodingTest
        ${_FT_DIR}/FilerNameEncodingTest.cpp
    )
    target_include_directories(FilerNameEncodingTest PRIVATE ${_FT_INCLUDE_DIR})
    target_compile_features(FilerNameEncodingTest PRIVATE cxx_std_20)
    target_link_libraries(FilerNameEncodingTest PRIVATE UltraCanvas)
    # Exported symbols, so the crash backtrace this test prints can name the
    # frames in the test itself, not just the ones in libUltraCanvas.
    set_target_properties(FilerNameEncodingTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${_FT_BIN_DIR}
        ENABLE_EXPORTS ON
    )
    add_test(NAME FilerNameEncodingTest COMMAND FilerNameEncodingTest
             WORKING_DIRECTORY ${_FT_BIN_DIR})
    message(STATUS "    Test registered: FilerNameEncodingTest")
else()
    message(STATUS "  FilerNameEncodingTest skipped (UltraCanvas target not present)")
endif()
