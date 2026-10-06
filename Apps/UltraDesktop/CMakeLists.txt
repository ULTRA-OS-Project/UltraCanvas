# Apps/UltraDesktop/CMakeLists.txt
# UltraDesktop — the ULTRA OS desktop: taskbar, desktop organiser, info panel
# and wallpaper on one screen-sized window, built on the UltraCanvasDesktopShell
# module (UltraCanvas/include/UltraCanvasDesktopShell.h).
#
# One target, the desktop application; the headless modes (--windows, --apps,
# --devices, --screenshot) come from the module and need nothing this file adds.
#
# Version: 0.1.0
# Author: UltraCanvas Framework / ULTRA OS

# Needs the UltraCanvas UI library, built by add_subdirectory(UltraCanvas)
# before this directory, so ${ULTRACANVAS_LIBRARY} / ${ULTRACANVAS_INCLUDE_DIRS}
# are in scope and carry the platform dependencies as PUBLIC.
if(NOT ULTRACANVAS_LIBRARY)
    message(STATUS "  [−] UltraDesktop app - SKIPPED (UltraCanvas UI library not built)")
    return()
endif()

add_executable(UltraDesktop
    main.cpp
    ui/UltraDesktopAppStarter.cpp
    ui/UltraDesktopClipboardPanel.cpp
    ui/UltraDesktopSettings.cpp
    ui/UltraDesktopStickerboard.cpp
    ui/UltraDesktopTasksWindow.cpp
    ui/UltraDesktopWindow.cpp
)

# Export dynamic symbols so on-demand plugin modules resolve core symbols
# when the core is linked statically, as the other applications do.
set_target_properties(UltraDesktop PROPERTIES ENABLE_EXPORTS ON)

target_include_directories(UltraDesktop PRIVATE
    ${ULTRACANVAS_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/ui)

# The clipboard history it records and its quick panel shows.
target_link_libraries(UltraDesktop PRIVATE UltraClipboardHistory ${ULTRACANVAS_LIBRARY})
if(TARGET fmt::fmt)
    target_link_libraries(UltraDesktop PRIVATE fmt::fmt)
endif()
find_package(Threads REQUIRED)
target_link_libraries(UltraDesktop PRIVATE Threads::Threads)
target_compile_features(UltraDesktop PRIVATE cxx_std_20)

# UltraDesktop versions itself, independently of the framework: the number
# comes from the first line of Docs/UltraDesktop/CHANGELOG.md through
# cmake/UltraCanvasVersion.cmake — never a literal of its own.
target_compile_definitions(UltraDesktop PRIVATE
    ULTRADESKTOP_VERSION="${ULTRADESKTOP_VERSION}"
    $<$<CONFIG:Debug>:ULTRACANVAS_DEBUG=1>
)

# Embed the application manifest + version info (Windows only).
ultracanvas_embed_windows_manifest(UltraDesktop)

# One icon, everywhere the app is drawn. main.cpp names this same file for the
# window icon; UCAPP_ICON_PATH is the fallback the core applies when an app
# sets no icon of its own. The PNG is the 256 px render of
# media/appicon/UltraDesktop.svg, the scalable half of the pair.
target_compile_definitions(UltraDesktop PRIVATE
    UCAPP_ICON_PATH="media/appicon/UltraDesktop.png")

# Embed the app icon in the .exe (Windows only).
ultracanvas_embed_app_icon(UltraDesktop
    "${CMAKE_SOURCE_DIR}/media/appicon/UltraDesktop.png")

# The freedesktop shortcut and the icon it names, so the desktop's own window
# carries the UltraDesktop icon and the taskbar of another desktop can match it.
if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN)
    install(TARGETS UltraDesktop RUNTIME DESTINATION bin)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/UltraDesktop.desktop
            DESTINATION share/applications)
    install(FILES ${CMAKE_SOURCE_DIR}/media/appicon/UltraDesktop.png
            DESTINATION share/icons/hicolor/256x256/apps)
    install(FILES ${CMAKE_SOURCE_DIR}/media/appicon/UltraDesktop.svg
            DESTINATION share/icons/hicolor/scalable/apps)
    message(STATUS "  UltraDesktop desktop entry: share/applications/UltraDesktop.desktop")
endif()

message(STATUS "  [✓] UltraDesktop app - ENABLED (target: UltraDesktop)")
message(STATUS "      Version ${ULTRADESKTOP_VERSION} (Docs/UltraDesktop/CHANGELOG.md)")
