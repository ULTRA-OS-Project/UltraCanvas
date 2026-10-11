# Apps/UltraVideo/CMakeLists.txt
# UltraVideo - a video editor on the VideoFX engine and the framework's
# UltraCanvasVideoTimeline element: media bin, viewer with scrubbing,
# inspector, the timeline, undo, .ultravideo projects and an export dialog
# (Docs/Research/UltraVideoDesignProposal.md).
# Version: 0.1.0
# Author: UltraCanvas Framework

if(NOT ULTRACANVAS_LIBRARY OR NOT TARGET VideoFX)
    message(STATUS "  [−] UltraVideo - SKIPPED (framework or VideoFX not built)")
    return()
endif()

add_executable(UltraVideo
    main.cpp
    UltraVideoDialogs.cpp
    UltraVideoDocument.cpp
    UltraVideoHistory.cpp
    UltraVideoInspector.cpp
    UltraVideoMapping.cpp
    UltraVideoPreview.cpp
    UltraVideoStory.cpp
    UltraVideoViewer.cpp
    UltraVideoWindow.cpp
)

# Export dynamic symbols so on-demand plugin modules resolve core symbols
# when the core is linked statically, as the other applications do.
set_target_properties(UltraVideo PROPERTIES ENABLE_EXPORTS ON)

# The window title and --version read the release recorded in
# Docs/UltraVideo/CHANGELOG.md (cmake/UltraCanvasVersion.cmake).
target_compile_definitions(UltraVideo PRIVATE
    ULTRAVIDEO_VERSION="${ULTRAVIDEO_VERSION}"
    UCAPP_ICON_PATH="media/appicon/UltraVideo.png")

target_include_directories(UltraVideo PRIVATE
    ${ULTRACANVAS_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(UltraVideo PRIVATE
    ${ULTRACANVAS_LIBRARY}
    VideoFX::VideoFX)
if(TARGET fmt::fmt)
    target_link_libraries(UltraVideo PRIVATE fmt::fmt)
endif()
if(ULTRACANVAS_PLUGIN_TARGETS)
    target_link_libraries(UltraVideo PRIVATE ${ULTRACANVAS_PLUGIN_TARGETS})
endif()
target_compile_features(UltraVideo PRIVATE cxx_std_20)

ultracanvas_embed_windows_manifest(UltraVideo)
ultracanvas_embed_app_icon(UltraVideo
    "${CMAKE_SOURCE_DIR}/media/appicon/UltraVideo.png")

install(TARGETS UltraVideo RUNTIME DESTINATION bin)

if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN)
    # The desktop entry registers UltraVideo for its projects and the media
    # it edits; the icon lands in the theme folders Icon=UltraVideo looks in.
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/UltraVideo.desktop
            DESTINATION share/applications)
    install(FILES ${CMAKE_SOURCE_DIR}/media/appicon/UltraVideo.png
            DESTINATION share/icons/hicolor/256x256/apps)
    install(FILES ${CMAKE_SOURCE_DIR}/media/appicon/UltraVideo.svg
            DESTINATION share/icons/hicolor/scalable/apps)
endif()

message(STATUS "  [+] UltraVideo")
