# Apps/UltraSnip/CMakeLists.txt
# UltraSnip — the screenshot and screen-recording tool of ULTRA OS: a
# rectangle, a window or the whole screen as a picture (to the clipboard and
# Pictures/Screenshots) or as a video (Videos/Screen Recordings, through
# VideoFX's frame recorder). UltraDesktop's screenshot button and
# Super+Shift+S start it; a second start hands its request to the running
# one. Without VideoFX the pictures still work and a recording says why not.
# Version: 0.1.0
# Author: UltraCanvas Framework / ULTRA OS

if(NOT ULTRACANVAS_LIBRARY)
    message(STATUS "  [−] UltraSnip - SKIPPED (framework not built)")
    return()
endif()

add_executable(UltraSnip
    main.cpp
    ui/UltraSnipCapture.cpp
    ui/UltraSnipOverlay.cpp
    ui/UltraSnipRecorder.cpp
    ui/UltraSnipRecordingBar.cpp
    ui/UltraSnipSettings.cpp
    ui/UltraSnipWindow.cpp
)

# One instance per user: a second start hands its request to the running one
# (UltraCanvasSingleInstance.h).
ultracanvas_single_instance(UltraSnip)

# Export dynamic symbols so on-demand plugin modules resolve core symbols
# when the core is linked statically, as the other applications do.
set_target_properties(UltraSnip PROPERTIES ENABLE_EXPORTS ON)

# The window title and --version read the release recorded in
# Docs/UltraSnip/CHANGELOG.md.
target_compile_definitions(UltraSnip PRIVATE
    ULTRASNIP_VERSION="${ULTRASNIP_VERSION}")

target_include_directories(UltraSnip PRIVATE
    ${ULTRACANVAS_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/ui)

target_link_libraries(UltraSnip PRIVATE
    ${ULTRACANVAS_LIBRARY})
# The recorder needs the engine; without it the same API answers
# NotAvailable (VideoFX builds its stub then), so this always links.
if(TARGET VideoFX)
    target_link_libraries(UltraSnip PRIVATE VideoFX::VideoFX)
else()
    message(FATAL_ERROR "UltraSnip needs the VideoFX target (built with or without FFmpeg)")
endif()
if(TARGET fmt::fmt)
    target_link_libraries(UltraSnip PRIVATE fmt::fmt)
endif()
if(ULTRACANVAS_PLUGIN_TARGETS)
    target_link_libraries(UltraSnip PRIVATE ${ULTRACANVAS_PLUGIN_TARGETS})
endif()
target_compile_features(UltraSnip PRIVATE cxx_std_20)

# Built next to UltraDesktop (the build tree's top), not into bin/: the
# desktop starts it by name and looks beside its own executable first.
set_target_properties(UltraSnip PROPERTIES OUTPUT_NAME "UltraSnip")

# One icon everywhere the app is drawn: main.cpp names the same file for the
# window, and the core falls back to UCAPP_ICON_PATH when an app sets none.
target_compile_definitions(UltraSnip PRIVATE
    UCAPP_ICON_PATH="media/appicon/UltraSnip.png")

ultracanvas_embed_windows_manifest(UltraSnip)
ultracanvas_embed_app_icon(UltraSnip
    "${CMAKE_SOURCE_DIR}/media/appicon/UltraSnip.png")

install(TARGETS UltraSnip RUNTIME DESTINATION bin)

if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/UltraSnip.desktop
            DESTINATION share/applications)
    install(FILES ${CMAKE_SOURCE_DIR}/media/appicon/UltraSnip.png
            DESTINATION share/icons/hicolor/256x256/apps)
    install(FILES ${CMAKE_SOURCE_DIR}/media/appicon/UltraSnip.svg
            DESTINATION share/icons/hicolor/scalable/apps)
endif()

message(STATUS "  [+] UltraSnip")
