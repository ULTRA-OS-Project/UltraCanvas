# Apps/UltraNotifications/CMakeLists.txt
# UltraNotifications — the notifications of ULTRA OS after their toast has
# gone: every notification, chat message and mail on the UltraMessage feed,
# to read, search, open and dismiss. The view is the framework's
# UltraCanvasMessageCenter (library UltraMessageCenter).
# Version: 0.1.0
# Author: UltraCanvas Framework / ULTRA OS

if(NOT ULTRACANVAS_LIBRARY OR NOT TARGET UltraMessageCenter)
    message(STATUS "  [−] UltraNotifications - SKIPPED (framework or UltraMessageCenter not built)")
    return()
endif()

add_executable(UltraNotifications
    main.cpp
    ui/UltraNotificationsWindow.cpp
)

# One instance per user: a second start brings the running one forward
# (UltraCanvasSingleInstance.h).
ultracanvas_single_instance(UltraNotifications)

# Export dynamic symbols so on-demand plugin modules resolve core symbols
# when the core is linked statically, as the other applications do.
set_target_properties(UltraNotifications PROPERTIES ENABLE_EXPORTS ON)

# The window title and --version read the release recorded in
# Docs/UltraNotifications/CHANGELOG.md.
target_compile_definitions(UltraNotifications PRIVATE
    ULTRANOTIFICATIONS_VERSION="${ULTRANOTIFICATIONS_VERSION}")

target_include_directories(UltraNotifications PRIVATE
    ${ULTRACANVAS_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/ui)

target_link_libraries(UltraNotifications PRIVATE
    UltraMessageCenter
    ${ULTRACANVAS_LIBRARY})
if(TARGET fmt::fmt)
    target_link_libraries(UltraNotifications PRIVATE fmt::fmt)
endif()
if(ULTRACANVAS_PLUGIN_TARGETS)
    target_link_libraries(UltraNotifications PRIVATE ${ULTRACANVAS_PLUGIN_TARGETS})
endif()
target_compile_features(UltraNotifications PRIVATE cxx_std_20)

# Built next to UltraDesktop (the build tree's top), not into bin/: the
# desktop starts it by name and looks beside its own executable first.
set_target_properties(UltraNotifications PROPERTIES OUTPUT_NAME "UltraNotifications")

# One icon everywhere the app is drawn: main.cpp names the same file for the
# window, and the core falls back to UCAPP_ICON_PATH when an app sets none.
target_compile_definitions(UltraNotifications PRIVATE
    UCAPP_ICON_PATH="media/appicon/UltraNotifications.png")

ultracanvas_embed_windows_manifest(UltraNotifications)
ultracanvas_embed_app_icon(UltraNotifications
    "${CMAKE_SOURCE_DIR}/media/appicon/UltraNotifications.png")

install(TARGETS UltraNotifications RUNTIME DESTINATION bin)

if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/UltraNotifications.desktop
            DESTINATION share/applications)
    install(FILES ${CMAKE_SOURCE_DIR}/media/appicon/UltraNotifications.png
            DESTINATION share/icons/hicolor/256x256/apps)
    install(FILES ${CMAKE_SOURCE_DIR}/media/appicon/UltraNotifications.svg
            DESTINATION share/icons/hicolor/scalable/apps)
endif()

message(STATUS "  [+] UltraNotifications")
