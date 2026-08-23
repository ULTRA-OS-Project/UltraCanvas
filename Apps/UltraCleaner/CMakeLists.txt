# Apps/UltraCleaner/CMakeLists.txt
# UltraCleaner — removes the files macOS, Windows and Linux leave behind.
#
# Two targets:
#   UltraCleanerEngine  a headless static library (rule table, path safety
#                       guard, scanner, remover). No UltraCanvas dependency,
#                       so the CLI and the test suite link it on its own.
#   UltraCleaner        the GUI application on top of it.
#
# Version: 0.1.0
# Author: UltraCanvas Framework / ULTRA OS

set(ULTRACLEANER_ENGINE_SOURCES
    engine/UltraCleanerTypes.cpp
    engine/UltraCleanerPaths.cpp
    engine/UltraCleanerRules.cpp
    engine/UltraCleanerSafety.cpp
    engine/UltraCleanerScanner.cpp
    engine/UltraCleanerRemover.cpp
)

add_library(UltraCleanerEngine STATIC ${ULTRACLEANER_ENGINE_SOURCES})
set_target_properties(UltraCleanerEngine PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME "ultracleaner_engine")
target_compile_features(UltraCleanerEngine PUBLIC cxx_std_20)
target_include_directories(UltraCleanerEngine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/engine)

# The trash goes through the shell on Windows (SHFileOperationW /
# SHEmptyRecycleBinW / SHQueryRecycleBinW); everywhere else the engine is
# pure std::filesystem. Older GCC/Clang runtimes keep <filesystem> in a
# separate library.
if(WIN32)
    target_link_libraries(UltraCleanerEngine PUBLIC shell32)
    # Same hygiene the core library applies: keep windows.h from defining
    # min/max macros, and from dragging in headers the engine never uses.
    # The engine does not link UltraCanvas, so it does not inherit these.
    target_compile_definitions(UltraCleanerEngine PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND
       CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.0)
    target_link_libraries(UltraCleanerEngine PUBLIC stdc++fs)
endif()

message(STATUS "  [✓] UltraCleaner engine - ENABLED (target: UltraCleanerEngine)")

# ── UltraCleaner album engine ────────────────────────────────────────────────
# Finding duplicate and near-duplicate photographs needs to decode images, so
# unlike the rule engine this half links the UltraCanvas core (for
# UCImageRaster). Kept as its own target so the headless rule engine, the CLI
# and the rule tests stay free of that dependency.
if(ULTRACANVAS_LIBRARY)
    add_library(UltraCleanerAlbum STATIC
        album/UltraCleanerImageDescriptor.cpp
        album/UltraCleanerAlbumScanner.cpp
    )
    set_target_properties(UltraCleanerAlbum PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        OUTPUT_NAME "ultracleaner_album")
    target_compile_features(UltraCleanerAlbum PUBLIC cxx_std_20)
    target_include_directories(UltraCleanerAlbum PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/album
        ${CMAKE_CURRENT_SOURCE_DIR}/engine)
    target_include_directories(UltraCleanerAlbum PRIVATE ${ULTRACANVAS_INCLUDE_DIRS})
    target_link_libraries(UltraCleanerAlbum PUBLIC UltraCleanerEngine ${ULTRACANVAS_LIBRARY})
    message(STATUS "  [✓] UltraCleaner album engine - ENABLED (target: UltraCleanerAlbum)")
else()
    message(STATUS "  [−] UltraCleaner album engine - SKIPPED (needs the UltraCanvas core)")
endif()

# ── UltraCleaner GUI application ────────────────────────────────────────────
# Needs the UltraCanvas UI library, built by add_subdirectory(UltraCanvas)
# before this directory, so ${ULTRACANVAS_LIBRARY} / ${ULTRACANVAS_INCLUDE_DIRS}
# are in scope and carry the platform dependencies as PUBLIC.
if(NOT ULTRACANVAS_LIBRARY)
    message(STATUS "  [−] UltraCleaner GUI app - SKIPPED (UltraCanvas UI library not built)")
    return()
endif()

add_executable(UltraCleaner
    main.cpp
    ui/UltraCleanerWindow.cpp
    ui/UltraCleanerCategoryPanel.cpp
)

# Export dynamic symbols so on-demand plugin modules resolve core symbols
# when the core is linked statically, as the other applications do.
set_target_properties(UltraCleaner PROPERTIES ENABLE_EXPORTS ON)

target_include_directories(UltraCleaner PRIVATE
    ${ULTRACANVAS_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/ui)

# Engine before the core on the link line: GNU ld resolves left to right.
target_link_libraries(UltraCleaner PRIVATE
    UltraCleanerAlbum UltraCleanerEngine ${ULTRACANVAS_LIBRARY})
if(TARGET fmt::fmt)
    target_link_libraries(UltraCleaner PRIVATE fmt::fmt)
endif()
target_compile_features(UltraCleaner PRIVATE cxx_std_20)

# The version comes from Docs/UltraCanvas/CHANGELOG.md through
# cmake/UltraCanvasVersion.cmake — never a literal of its own.
target_compile_definitions(UltraCleaner PRIVATE
    ULTRACLEANER_VERSION="${ULTRACANVAS_VERSION}"
    $<$<CONFIG:Debug>:ULTRACANVAS_DEBUG=1>
)

# Embed the application manifest + version info (Windows only).
ultracanvas_embed_windows_manifest(UltraCleaner)

message(STATUS "  [✓] UltraCleaner GUI app - ENABLED (target: UltraCleaner)")
