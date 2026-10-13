# Apps/UltraPress/CMakeLists.txt
# UltraPress - a website and shop builder that publishes WebAssembly
# (Docs/Research/UltraPressDesignProposal.md). Phase 1 begins with the
# headless engine - the content model, the block registry, the HTML shadow
# emitter and the static publisher - and the `ultrapress` command line on
# top of it. The editor with the element renderer follows.
# Version: 0.1.0
# Author: UltraCanvas Framework / ULTRA OS

set(ULTRAPRESS_ENGINE_SOURCES
    engine/UltraPressModel.cpp
    engine/UltraPressBlocks.cpp
    engine/UltraPressHtml.cpp
    engine/UltraPressPublish.cpp
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

# The version comes from the first line of Docs/UltraPress/CHANGELOG.md
# through cmake/UltraCanvasVersion.cmake - never a literal in the source.
# Attached to the one source that prints it.
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/UltraCanvasVersion.cmake")
add_executable(ultrapress cli/main.cpp cli/UltraPressCli.cpp)
target_compile_features(ultrapress PRIVATE cxx_std_20)
target_include_directories(ultrapress PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/cli)
target_link_libraries(ultrapress PRIVATE UltraPressEngine)
set_source_files_properties(cli/UltraPressCli.cpp PROPERTIES
    COMPILE_DEFINITIONS "ULTRAPRESS_VERSION=\"${ULTRAPRESS_VERSION}\"")
set_target_properties(ultrapress PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

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

# ── UltraPress: the window ──────────────────────────────────────────────────
add_executable(UltraPress ui/main.cpp ui/UltraPressWindow.cpp)
target_compile_features(UltraPress PRIVATE cxx_std_20)
target_include_directories(UltraPress PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ui)
target_link_libraries(UltraPress PRIVATE UltraPressRender)
# ULTRAPRESS_VERSION (the first line of Docs/UltraPress/CHANGELOG.md) is shown
# in the window title; UCAPP_ICON_PATH is the window and taskbar icon.
target_compile_definitions(UltraPress PRIVATE
    ULTRAPRESS_VERSION="${ULTRAPRESS_VERSION}"
    UCAPP_ICON_PATH="media/appicon/UltraWeb.png")
set_target_properties(UltraPress PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

message(STATUS "  [✓] UltraPress - ENABLED (targets: UltraPressEngine, UltraPressRender, ultrapress, UltraPress)")
