# Apps/ChatCube/CMakeLists.txt
# ChatCube - the ULTRA OS native messenger, on UltraIM. The application is
# the window; the networks are UltraIM provider modules (ChatCube Direct:
# UltraCanvas/Plugins/UltraIM/direct, built into <build>/Plugins/UltraIM).
#
# Version: 0.1.0
# Author: UltraCanvas Framework / ULTRA OS

if(NOT ULTRACANVAS_LIBRARY)
    message(STATUS "  [−] ChatCube - SKIPPED (UltraCanvas UI library not built)")
    return()
endif()
if(NOT TARGET UltraIM OR NOT TARGET UltraRTC OR NOT TARGET UltraNet)
    message(STATUS "  [−] ChatCube - SKIPPED (needs UltraIM, UltraRTC and UltraNet)")
    return()
endif()

add_executable(ChatCube
    main.cpp
    ui/ChatCubeViews.cpp
    ui/ChatCubeWindow.cpp
)
# One copy per user (UltraCanvasSingleInstance.h).
ultracanvas_single_instance(ChatCube)
set_target_properties(ChatCube PROPERTIES ENABLE_EXPORTS ON)

target_include_directories(ChatCube PRIVATE
    ${ULTRACANVAS_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/ui)
target_link_libraries(ChatCube PRIVATE ${ULTRACANVAS_LIBRARY} UltraIM UltraRTC UltraNet)
find_package(Threads REQUIRED)
target_link_libraries(ChatCube PRIVATE Threads::Threads)
target_compile_features(ChatCube PRIVATE cxx_std_20)
# The provider ChatCube talks through is a module: build it with the app.
if(TARGET UltraIMDirectPlugin)
    add_dependencies(ChatCube UltraIMDirectPlugin)
endif()

# ChatCube versions itself: the number comes from the first line of
# Docs/ChatCube/CHANGELOG.md through cmake/UltraCanvasVersion.cmake.
target_compile_definitions(ChatCube PRIVATE
    CHATCUBE_VERSION="${CHATCUBE_VERSION}"
    UCAPP_ICON_PATH="media/appicon/ChatCube.png"
    $<$<CONFIG:Debug>:ULTRACANVAS_DEBUG=1>
)

ultracanvas_embed_windows_manifest(ChatCube)
ultracanvas_embed_app_icon(ChatCube "${CMAKE_SOURCE_DIR}/media/appicon/ChatCube.png")

if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN)
    install(TARGETS ChatCube RUNTIME DESTINATION bin)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/ChatCube.desktop DESTINATION share/applications)
    install(FILES ${CMAKE_SOURCE_DIR}/media/appicon/ChatCube.png
            DESTINATION share/icons/hicolor/256x256/apps)
    install(FILES ${CMAKE_SOURCE_DIR}/media/appicon/ChatCube.svg
            DESTINATION share/icons/hicolor/scalable/apps)
endif()

message(STATUS "  [✓] ChatCube - ENABLED (target: ChatCube)")
message(STATUS "      Version ${CHATCUBE_VERSION} (Docs/ChatCube/CHANGELOG.md)")

# chatcube-peer: a second ChatCube Direct identity on the command line, for
# trying the app and the relay without a second person (peer/main.cpp).
if(TARGET UltraIMDirectCore)
    add_executable(chatcube-peer peer/main.cpp)
    target_compile_features(chatcube-peer PRIVATE cxx_std_20)
    target_link_libraries(chatcube-peer PRIVATE UltraIMDirectCore)
endif()
