# Apps/UltraClipboard/CMakeLists.txt
# UltraClipboard — the clipboard history of ULTRA OS: everything copied, with
# a thumbnail and Copy / Edit / Delete on every entry, search, pins and the
# history's settings. The history itself is the UltraClipboardHistory library
# (UltraCanvas/CMakeLists.txt), which UltraDesktop records into.
# Version: 0.1.0
# Author: UltraCanvas Framework / ULTRA OS

if(NOT ULTRACANVAS_LIBRARY OR NOT TARGET UltraClipboardHistory)
    message(STATUS "  [−] UltraClipboard - SKIPPED (framework or clipboard history not built)")
    return()
endif()

add_executable(UltraClipboard
    main.cpp
    ui/UltraClipboardWindow.cpp
    ui/ClipboardDialogs.cpp
)

# Export dynamic symbols so on-demand plugin modules resolve core symbols
# when the core is linked statically, as the other applications do.
set_target_properties(UltraClipboard PROPERTIES ENABLE_EXPORTS ON)

# The window title and --version read the release recorded in
# Docs/UltraClipboard/CHANGELOG.md.
target_compile_definitions(UltraClipboard PRIVATE
    ULTRACLIPBOARD_VERSION="${ULTRACLIPBOARD_VERSION}")

target_include_directories(UltraClipboard PRIVATE
    ${ULTRACANVAS_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/ui)

target_link_libraries(UltraClipboard PRIVATE
    UltraClipboardHistory
    ${ULTRACANVAS_LIBRARY})
if(TARGET fmt::fmt)
    target_link_libraries(UltraClipboard PRIVATE fmt::fmt)
endif()
if(ULTRACANVAS_PLUGIN_TARGETS)
    target_link_libraries(UltraClipboard PRIVATE ${ULTRACANVAS_PLUGIN_TARGETS})
endif()
target_compile_features(UltraClipboard PRIVATE cxx_std_20)

# Built next to UltraDesktop (the build tree's top), not into bin/: the
# desktop starts it by name and looks beside its own executable first.
set_target_properties(UltraClipboard PROPERTIES OUTPUT_NAME "UltraClipboard")

# One icon everywhere the app is drawn: main.cpp names the same file for the
# window, and the core falls back to UCAPP_ICON_PATH when an app sets none.
target_compile_definitions(UltraClipboard PRIVATE
    UCAPP_ICON_PATH="media/appicon/UltraClipboard.png")

ultracanvas_embed_windows_manifest(UltraClipboard)
ultracanvas_embed_app_icon(UltraClipboard
    "${CMAKE_SOURCE_DIR}/media/appicon/UltraClipboard.png")

install(TARGETS UltraClipboard RUNTIME DESTINATION bin)

if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/UltraClipboard.desktop
            DESTINATION share/applications)
    install(FILES ${CMAKE_SOURCE_DIR}/media/appicon/UltraClipboard.png
            DESTINATION share/icons/hicolor/256x256/apps)
    install(FILES ${CMAKE_SOURCE_DIR}/media/appicon/UltraClipboard.svg
            DESTINATION share/icons/hicolor/scalable/apps)
endif()

message(STATUS "  [+] UltraClipboard")
