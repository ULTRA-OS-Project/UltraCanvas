# Apps/DeviceExplorer/CMakeLists.txt
# DeviceExplorer — the devices connected to this computer, as IODeviceManager
# finds them: a tree on the left, the selected device's details on the right.
#
# One target, the GUI application (with a headless --list mode), on top of the
# IODeviceManager module, which is compiled into the UltraCanvas core library
# together with whichever device backends this build found (CUPS, SANE, V4L2,
# the Windows spooler, eSCL) — UltraCanvas/CMakeLists.txt decides that, so this
# file adds nothing for it.
#
# Version: 0.1.0
# Author: UltraCanvas Framework / ULTRA OS

# Needs the UltraCanvas UI library, built by add_subdirectory(UltraCanvas)
# before this directory, so ${ULTRACANVAS_LIBRARY} / ${ULTRACANVAS_INCLUDE_DIRS}
# are in scope and carry the platform dependencies as PUBLIC.
if(NOT ULTRACANVAS_LIBRARY)
    message(STATUS "  [−] DeviceExplorer app - SKIPPED (UltraCanvas UI library not built)")
    return()
endif()

add_executable(DeviceExplorer
    main.cpp
    ui/DeviceExplorerModel.cpp
    ui/DeviceExplorerWindow.cpp
)

# Export dynamic symbols so on-demand plugin modules resolve core symbols
# when the core is linked statically, as the other applications do.
set_target_properties(DeviceExplorer PROPERTIES ENABLE_EXPORTS ON)

target_include_directories(DeviceExplorer PRIVATE
    ${ULTRACANVAS_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/ui)

target_link_libraries(DeviceExplorer PRIVATE ${ULTRACANVAS_LIBRARY})
if(TARGET fmt::fmt)
    target_link_libraries(DeviceExplorer PRIVATE fmt::fmt)
endif()
find_package(Threads REQUIRED)
target_link_libraries(DeviceExplorer PRIVATE Threads::Threads)
target_compile_features(DeviceExplorer PRIVATE cxx_std_20)

# DeviceExplorer versions itself, independently of the framework: the number
# comes from the first line of Docs/DeviceExplorer/CHANGELOG.md through
# cmake/UltraCanvasVersion.cmake — never a literal of its own.
target_compile_definitions(DeviceExplorer PRIVATE
    DEVICEEXPLORER_VERSION="${DEVICEEXPLORER_VERSION}"
    $<$<CONFIG:Debug>:ULTRACANVAS_DEBUG=1>
)

# Embed the application manifest + version info (Windows only).
ultracanvas_embed_windows_manifest(DeviceExplorer)

# One icon, everywhere the app is drawn. main.cpp names this same file for the
# window and taskbar icon; UCAPP_ICON_PATH is the fallback the core applies
# when an app sets no icon of its own. The PNG is the 256 px render of
# media/appicon/DeviceExplorer.svg, the scalable half of the pair.
target_compile_definitions(DeviceExplorer PRIVATE
    UCAPP_ICON_PATH="media/appicon/DeviceExplorer.png")

# Embed the app icon in the .exe (Windows only): the icon Explorer and the
# Windows taskbar read off the binary itself.
ultracanvas_embed_app_icon(DeviceExplorer
    "${CMAKE_SOURCE_DIR}/media/appicon/DeviceExplorer.png")

# The freedesktop shortcut and the icon it names. `Icon=DeviceExplorer` in the
# .desktop is resolved through the installed icon themes, so the files have to
# land in one.
if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN)
    # Exec=DeviceExplorer / TryExec=DeviceExplorer are resolved against PATH,
    # so the entry is only true if the binary is installed with it.
    install(TARGETS DeviceExplorer RUNTIME DESTINATION bin)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/DeviceExplorer.desktop
            DESTINATION share/applications)
    install(FILES ${CMAKE_SOURCE_DIR}/media/appicon/DeviceExplorer.png
            DESTINATION share/icons/hicolor/256x256/apps)
    install(FILES ${CMAKE_SOURCE_DIR}/media/appicon/DeviceExplorer.svg
            DESTINATION share/icons/hicolor/scalable/apps)
    message(STATUS "  DeviceExplorer desktop entry: share/applications/DeviceExplorer.desktop")
endif()

message(STATUS "  [✓] DeviceExplorer app - ENABLED (target: DeviceExplorer)")
message(STATUS "      Version ${DEVICEEXPLORER_VERSION} (Docs/DeviceExplorer/CHANGELOG.md)")
