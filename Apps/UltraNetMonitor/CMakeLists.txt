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

# One icon, everywhere the app is drawn. main.cpp names this same file for the
# window and taskbar icon; UCAPP_ICON_PATH is the fallback the core applies
# when an app sets no icon of its own (UltraCanvasApplicationBase::Initialize),
# so the two agreeing is what keeps a window from ever coming up unbranded. The
# PNG is the 256 px render of media/appicon/UltraNetMonitor.svg, the scalable
# half of the pair.
target_compile_definitions(UltraNetMonitor PRIVATE
    UCAPP_ICON_PATH="media/appicon/UltraNetMonitor.png")

# Embed the app icon in the .exe (Windows only). This is the icon Explorer and
# the Windows taskbar read off the binary itself — neither of them looks at
# _NET_WM_ICON or at a desktop entry.
ultracanvas_embed_app_icon(UltraNetMonitor
    "${CMAKE_SOURCE_DIR}/media/appicon/UltraNetMonitor.png")

# The freedesktop shortcut and the icon it names. `Icon=UltraNetMonitor` in the
# .desktop is resolved through the installed icon themes, so the files have to
# land in one: this is what lets UltraFiler (and the application menu, and a
# taskbar that matches windows to desktop entries) show UltraNetMonitor's own
# icon for the app rather than a generic executable glyph.
if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN)
    # Exec=UltraNetMonitor / TryExec=UltraNetMonitor are resolved against PATH,
    # so the entry is only true if the binary is installed with it.
    install(TARGETS UltraNetMonitor RUNTIME DESTINATION bin)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/UltraNetMonitor.desktop
            DESTINATION share/applications)
    install(FILES ${CMAKE_SOURCE_DIR}/media/appicon/UltraNetMonitor.png
            DESTINATION share/icons/hicolor/256x256/apps)
    install(FILES ${CMAKE_SOURCE_DIR}/media/appicon/UltraNetMonitor.svg
            DESTINATION share/icons/hicolor/scalable/apps)
    message(STATUS "  UltraNetMonitor desktop entry: share/applications/UltraNetMonitor.desktop")
endif()

message(STATUS "  [✓] UltraNetMonitor app - ENABLED (target: UltraNetMonitor)")
message(STATUS "      Version ${ULTRANETMONITOR_VERSION} (Docs/UltraNetMonitor/CHANGELOG.md)")
