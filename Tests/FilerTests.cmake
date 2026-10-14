# Tests/FilerTests.cmake
# FilerFolderPreviewTest, FilerTextPreviewTest, FilerNameEncodingTest,
# FilerHostIconsTest, FilerShortcutEntryTest, FilerHistoryTest and
# FilerPaneDragTest. Included by
# Tests/CMakeLists.txt under BUILD_TESTS, and by the top-level CMakeLists.txt
# on its own under ULTRACANVAS_BUILD_FILER_TESTS - so the Windows CI row,
# which builds no full test suite, runs the file display against Thai, CJK and
# emoji names on the platform where a name goes through UTF-16 and a code page
# (the runner's is 1252), asks the Windows shell for its icons, reads
# .lnk shortcuts where they are native, and keeps UltraFiler's history under
# %APPDATA%. Paths are relative to this file, so either includer
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

# ===== FILER TEXT PREVIEW TEST =====
# The page a document's preview card shows (TextPreviewLines): an .html file
# as a browser lays it out (HTML::ExtractPlainText's Lines layout) - a line
# per paragraph, list item and table row, the head, scripts and hidden text
# left out - plain text line for line, CSV as rows of cells.
if(TARGET UltraCanvas)
    message(STATUS "  Building FilerTextPreviewTest...")
    add_executable(FilerTextPreviewTest
        ${_FT_DIR}/FilerTextPreviewTest.cpp
    )
    target_include_directories(FilerTextPreviewTest PRIVATE ${_FT_INCLUDE_DIR})
    target_compile_features(FilerTextPreviewTest PRIVATE cxx_std_20)
    target_link_libraries(FilerTextPreviewTest PRIVATE UltraCanvas)
    set_target_properties(FilerTextPreviewTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${_FT_BIN_DIR}
    )
    add_test(NAME FilerTextPreviewTest COMMAND FilerTextPreviewTest
             WORKING_DIRECTORY ${_FT_BIN_DIR})
    message(STATUS "    Test registered: FilerTextPreviewTest")
else()
    message(STATUS "  FilerTextPreviewTest skipped (UltraCanvas target not present)")
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

# ===== FILER HOST ICONS TEST =====
# Display > File icons: the setting, and the cache key the host icon service
# answers by - the key that decides whether a folder of four thousand ".txt"
# files costs one icon lookup or four thousand. The lookups themselves are the
# host's, so the test asserts that asking is harmless rather than what any one
# desktop answers; it therefore passes on a build machine with no icon theme -
# and on Windows it asks the real shell (SHGetFileInfo) the same questions.
# It also reads UltraFiler's saved choice back from a temporary config folder
# (Apps/UltraFiler/UltraFilerSettings.h, header-only), which defaults to the
# host's icons.
if(TARGET UltraCanvas)
    message(STATUS "  Building FilerHostIconsTest...")
    add_executable(FilerHostIconsTest
        ${_FT_DIR}/FilerHostIconsTest.cpp
    )
    target_include_directories(FilerHostIconsTest PRIVATE ${_FT_INCLUDE_DIR}
        ${_FT_DIR}/../Apps/UltraFiler)
    target_compile_features(FilerHostIconsTest PRIVATE cxx_std_20)
    target_link_libraries(FilerHostIconsTest PRIVATE UltraCanvas)
    set_target_properties(FilerHostIconsTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${_FT_BIN_DIR}
    )
    add_test(NAME FilerHostIconsTest COMMAND FilerHostIconsTest
             WORKING_DIRECTORY ${_FT_BIN_DIR})
    message(STATUS "    Test registered: FilerHostIconsTest")
else()
    message(STATUS "  FilerHostIconsTest skipped (UltraCanvas target not present)")
endif()

# ===== FILER SHORTCUT ENTRY TEST =====
# What the file display makes of a .lnk it lists: type, category, info column
# and the resolved target on the entry. Builds its own shortcuts (and, off
# Windows, its own Wine-prefix-shaped tree) inside the test; on Windows the
# links name real paths, which is the system the format comes from.
if(TARGET UltraCanvas)
    message(STATUS "  Building FilerShortcutEntryTest...")
    add_executable(FilerShortcutEntryTest
        ${_FT_DIR}/FilerShortcutEntryTest.cpp
    )
    target_include_directories(FilerShortcutEntryTest PRIVATE
        ${_FT_INCLUDE_DIR} ${_FT_DIR})
    target_compile_features(FilerShortcutEntryTest PRIVATE cxx_std_20)
    target_link_libraries(FilerShortcutEntryTest PRIVATE UltraCanvas)
    set_target_properties(FilerShortcutEntryTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${_FT_BIN_DIR}
    )
    add_test(NAME FilerShortcutEntryTest COMMAND FilerShortcutEntryTest
             WORKING_DIRECTORY ${_FT_BIN_DIR})
    message(STATUS "    Test registered: FilerShortcutEntryTest")
else()
    message(STATUS "  FilerShortcutEntryTest skipped (UltraCanvas target not present)")
endif()

# ===== FILER HISTORY TEST =====
# UltraFiler's recently-used lists (Apps/UltraFiler/UltraFilerHistory.h,
# header-only): the Files / Folders / Apps lists survive a restart, and each
# keeps the number of entries Settings > Extras > History & Favorites asks
# for - while recording, when the file is read back and the moment the limit
# is lowered - and History and Favorites keep a file named in Thai with an
# emoji (UltraFilerFavorites.h too). Writes its config into a temporary
# folder, never a real one.
if(TARGET UltraCanvas)
    message(STATUS "  Building FilerHistoryTest...")
    add_executable(FilerHistoryTest
        ${_FT_DIR}/FilerHistoryTest.cpp
    )
    target_include_directories(FilerHistoryTest PRIVATE ${_FT_INCLUDE_DIR}
        ${_FT_DIR}/../Apps/UltraFiler)
    target_compile_features(FilerHistoryTest PRIVATE cxx_std_20)
    target_link_libraries(FilerHistoryTest PRIVATE UltraCanvas)
    set_target_properties(FilerHistoryTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${_FT_BIN_DIR}
    )
    add_test(NAME FilerHistoryTest COMMAND FilerHistoryTest
             WORKING_DIRECTORY ${_FT_BIN_DIR})
    message(STATUS "    Test registered: FilerHistoryTest")
else()
    message(STATUS "  FilerHistoryTest skipped (UltraCanvas target not present)")
endif()

# ===== FILER PANE DRAG TEST =====
# Files dragged from one file display to another of the same window (the split
# view's two panes) land in the folder under the pointer - or the folder that
# display shows, over its empty space - moved unless Ctrl asks for a copy, and
# after the question a move asks when the drop confirmation covers moves. The
# display shows the folder the drop would land in while the drag is over it,
# and any other element the drag passes is told it entered and left. Opens a
# window, so it runs under Xvfb (xvfb-run -a) and skips itself without a
# DISPLAY.
if(TARGET UltraCanvas)
    message(STATUS "  Building FilerPaneDragTest...")
    add_executable(FilerPaneDragTest
        ${_FT_DIR}/FilerPaneDragTest.cpp
    )
    target_include_directories(FilerPaneDragTest PRIVATE ${_FT_INCLUDE_DIR})
    target_compile_features(FilerPaneDragTest PRIVATE cxx_std_20)
    target_link_libraries(FilerPaneDragTest PRIVATE UltraCanvas)
    set_target_properties(FilerPaneDragTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${_FT_BIN_DIR}
    )
    add_test(NAME FilerPaneDragTest COMMAND FilerPaneDragTest
             WORKING_DIRECTORY ${_FT_BIN_DIR})
    message(STATUS "    Test registered: FilerPaneDragTest")
else()
    message(STATUS "  FilerPaneDragTest skipped (UltraCanvas target not present)")
endif()
