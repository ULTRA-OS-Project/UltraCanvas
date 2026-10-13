# Apps/UltraPress/CMakeLists.txt
# UltraPress - a website and shop builder that publishes WebAssembly
# (Docs/Research/UltraPressDesignProposal.md). Phase 1 begins with the
# headless engine - the content model, the block registry, the HTML shadow
# emitter and the static publisher - and `ultrapress` on top of it: the
# commands and, given a site folder, the editor window on the element
# renderer.
# Version: 0.4.1 - one program: the commands and the window
# Version: 0.1.0
# Author: UltraCanvas Framework / ULTRA OS

set(ULTRAPRESS_ENGINE_SOURCES
    engine/UltraPressModel.cpp
    engine/UltraPressBlocks.cpp
    engine/UltraPressHtml.cpp
    engine/UltraPressPublish.cpp
    engine/UltraPressImport.cpp
)

add_library(UltraPressEngine STATIC ${ULTRAPRESS_ENGINE_SOURCES})
set_target_properties(UltraPressEngine PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME "ultrapress_engine")
target_compile_features(UltraPressEngine PUBLIC cxx_std_20)
target_include_directories(UltraPressEngine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/engine)
# UltraCanvasJSON (the site files) and HTMLReader's EscapeText (every piece
# of text in a page) live in the core library. The engine opens no window.
target_link_libraries(UltraPressEngine PUBLIC ${ULTRACANVAS_LIBRARY})
# The WordPress export is XML: tinyxml2 is a REQUIRED core dependency
# (UltraCanvas/CMakeLists.txt), re-exported by the core; the include path is
# named here too where the configure step knows it, as UltraFIBU does.
if(TINYXML2_FOUND)
    target_include_directories(UltraPressEngine PRIVATE ${TINYXML2_INCLUDE_DIRS})
    target_link_libraries(UltraPressEngine PUBLIC ${TINYXML2_LIBRARIES})
    if(TINYXML2_LIBRARY_DIRS)
        target_link_directories(UltraPressEngine PUBLIC ${TINYXML2_LIBRARY_DIRS})
    endif()
endif()

# The version comes from the first line of Docs/UltraPress/CHANGELOG.md
# through cmake/UltraCanvasVersion.cmake - never a literal in the source.
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/UltraCanvasVersion.cmake")

# ── UltraPressRender: the page on real elements ─────────────────────────────
# The block renderer builds UltraCanvas elements, so it needs the UI library;
# the editor's preview and the Emscripten page share it. The engine above
# stays headless.
add_library(UltraPressRender STATIC render/UltraPressRender.cpp)
set_target_properties(UltraPressRender PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME "ultrapress_render")
target_compile_features(UltraPressRender PUBLIC cxx_std_20)
target_include_directories(UltraPressRender PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/render)
target_link_libraries(UltraPressRender PUBLIC UltraPressEngine ${ULTRACANVAS_LIBRARY})

# ── ultrapress: the one program - the commands and the window ──────────────
# `ultrapress new|check|page|publish|import ...` runs a command; given a site
# folder it opens the editor window. There were two programs, `ultrapress`
# and `UltraPress`, until the macOS build showed why there cannot be: on a
# case-insensitive filesystem (macOS, Windows) the two names are one, so the
# generator wrote both targets' CMakeFiles into one directory and both
# binaries to one path, and make stopped with "No rule to make target
# ultrapress.dir/depend". UltraFIBU made the same move (one `ultrafibu`).
add_executable(ultrapress ui/main.cpp ui/UltraPressWindow.cpp cli/UltraPressCli.cpp)
target_compile_features(ultrapress PRIVATE cxx_std_20)
target_include_directories(ultrapress PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/cli)
target_link_libraries(ultrapress PRIVATE UltraPressRender)
# `ultrapress import --media` fetches the export's attachments through
# UltraNet, where the build has it; without it the command lists them.
if(TARGET UltraNet)
    target_link_libraries(ultrapress PRIVATE UltraNet)
    target_compile_definitions(ultrapress PRIVATE ULTRAPRESS_HAS_ULTRANET=1)
endif()
# ULTRAPRESS_VERSION (the first line of Docs/UltraPress/CHANGELOG.md) is shown
# in the window title; UCAPP_ICON_PATH is the window and taskbar icon - the
# 256 px render of media/appicon/UltraPress.svg, embedded in the Windows
# executable as well, where Explorer and the taskbar read it off the binary.
# Deliberately not a WIN32_EXECUTABLE, like ultrafibu: a GUI-subsystem
# program cannot print into the prompt it was started from, and the
# commands are half of this one.
target_compile_definitions(ultrapress PRIVATE
    ULTRAPRESS_VERSION="${ULTRAPRESS_VERSION}"
    UCAPP_ICON_PATH="media/appicon/UltraPress.png")
set_target_properties(ultrapress PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
ultracanvas_embed_windows_manifest(ultrapress)
ultracanvas_embed_app_icon(ultrapress "${CMAKE_SOURCE_DIR}/media/appicon/UltraPress.png")
if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN)
    install(TARGETS ultrapress RUNTIME DESTINATION bin)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/UltraPress.desktop DESTINATION share/applications)
    install(FILES ${CMAKE_SOURCE_DIR}/media/appicon/UltraPress.png DESTINATION share/icons/hicolor/256x256/apps)
    install(FILES ${CMAKE_SOURCE_DIR}/media/appicon/UltraPress.svg DESTINATION share/icons/hicolor/scalable/apps)
endif()

message(STATUS "  [✓] UltraPress - ENABLED, commands and window (targets: UltraPressEngine, UltraPressRender, ultrapress)")
