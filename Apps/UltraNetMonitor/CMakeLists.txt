# Apps/UltraNetMonitor/CMakeLists.txt
# UltraNetMonitor — which processes hold which network connections, live.
#
# One target, the GUI application, on top of the NetworkMonitor module
# (UltraCanvas/CMakeLists.txt); the module is what --list / --by-app use, so
# the headless modes need nothing this file adds.
#
# Version: 0.3.0
# Author: UltraCanvas Framework / ULTRA OS

# Needs the UltraCanvas UI library, built by add_subdirectory(UltraCanvas)
# before this directory, so ${ULTRACANVAS_LIBRARY} / ${ULTRACANVAS_INCLUDE_DIRS}
# are in scope and carry the platform dependencies as PUBLIC.
if(NOT ULTRACANVAS_LIBRARY)
    message(STATUS "  [−] UltraNetMonitor app - SKIPPED (UltraCanvas UI library not built)")
    return()
endif()
if(NOT TARGET NetworkMonitor)
    message(STATUS "  [−] UltraNetMonitor app - SKIPPED (NetworkMonitor module not built)")
    return()
endif()

add_executable(UltraNetMonitor
    main.cpp
    ui/UltraNetMonitorModels.cpp
    ui/UltraNetMonitorPaths.cpp
    ui/UltraNetMonitorWindow.cpp
)

# Export dynamic symbols so on-demand plugin modules resolve core symbols
# when the core is linked statically, as the other applications do.
set_target_properties(UltraNetMonitor PROPERTIES ENABLE_EXPORTS ON)

target_include_directories(UltraNetMonitor PRIVATE
    ${ULTRACANVAS_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/ui)

# Module before the core on the link line: GNU ld resolves left to right.
target_link_libraries(UltraNetMonitor PRIVATE NetworkMonitor ${ULTRACANVAS_LIBRARY})
if(TARGET fmt::fmt)
    target_link_libraries(UltraNetMonitor PRIVATE fmt::fmt)
endif()
find_package(Threads REQUIRED)
target_link_libraries(UltraNetMonitor PRIVATE Threads::Threads)
target_compile_features(UltraNetMonitor PRIVATE cxx_std_20)

# UltraNetMonitor versions itself, independently of the framework: the number
# comes from the first line of Docs/UltraNetMonitor/CHANGELOG.md through
# cmake/UltraCanvasVersion.cmake — never a literal of its own.
target_compile_definitions(UltraNetMonitor PRIVATE
    ULTRANETMONITOR_VERSION="${ULTRANETMONITOR_VERSION}"
    $<$<CONFIG:Debug>:ULTRACANVAS_DEBUG=1>
)

# Embed the application manifest + version info (Windows only).
ultracanvas_embed_windows_manifest(UltraNetMonitor)

message(STATUS "  [✓] UltraNetMonitor app - ENABLED (target: UltraNetMonitor)")
message(STATUS "      Version ${ULTRANETMONITOR_VERSION} (Docs/UltraNetMonitor/CHANGELOG.md)")
