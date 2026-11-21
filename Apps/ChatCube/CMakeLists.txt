# Apps/ChatCube/CMakeLists.txt
# ChatCube - the ULTRA OS native messenger, on UltraIM. The application is
# the window; the networks are UltraIM provider modules (ChatCube Direct,
# Matrix and Telegram: UltraCanvas/Plugins/UltraIM/direct, matrix and
# telegram, built into <build>/Plugins/UltraIM).
#
# Version: 0.1.7 - Android: libChatCube.so with the providers linked in (android/)
# Version: 0.1.6 - ChatCubeGroupWindow.cpp (the group editor)
# Version: 0.1.5 - the Mastodon module built with ChatCube
# Version: 0.1.4 - ChatCubeControls.cpp, ChatCubeProfileWindow.cpp (Edit profile)
# Version: 0.1.3 - ChatCubeSettings.cpp and ChatCubeSettingsWindow.cpp: the Settings window
# Version: 0.1.2 - VideoFX, when it is built, for the preview pictures of videos sent
# Version: 0.1.1 - ChatCubeTheme.cpp
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

set(CHATCUBE_SOURCES
    main.cpp
    ui/ChatCubeSettings.cpp
    ui/ChatCubeSettingsWindow.cpp
    ui/ChatCubeControls.cpp
    ui/ChatCubeProfileWindow.cpp
    ui/ChatCubeGroupWindow.cpp
    ui/ChatCubeTheme.cpp
    ui/ChatCubeViews.cpp
    ui/ChatCubeWindow.cpp
)

if(ANDROID)
    # On Android the app is a shared library, libChatCube.so, which the
    # activity loads by the manifest's android.app.lib_name
    # (android/AndroidManifest.xml); main.cpp's body is then
    # ultracanvas_app_main(), called by the framework's android_main(), as
    # the demo's is. Nothing is dlopen'ed from outside an APK, and a provider
    # module would carry a second copy of the static core besides: the
    # providers are linked into the library instead and registered with
    # UltraIM at start (android/ChatCubeAndroidProviders.cpp). Telegram
    # (TDLib) and Signal (signal-cli, a Java program ChatCube starts) cannot
    # run on Android and are left out; their buttons are hidden.
    add_library(ChatCube SHARED ${CHATCUBE_SOURCES} android/ChatCubeAndroidProviders.cpp)
    # android/ for the providers' registration; the framework's Android
    # folder for UltraCanvasAndroidPermissions.h (the microphone and camera
    # prompts), which the core does not publish among its headers.
    target_include_directories(ChatCube PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/android
                                                ${CMAKE_SOURCE_DIR}/UltraCanvas/OS/Android)
    foreach(_cc_provider Direct Matrix Mastodon Mattermost Zulip Xmpp Irc)
        if(TARGET UltraIM${_cc_provider}Core)
            target_link_libraries(ChatCube PRIVATE UltraIM${_cc_provider}Core)
            string(TOUPPER "${_cc_provider}" _cc_upper)
            target_compile_definitions(ChatCube PRIVATE CHATCUBE_LINKED_${_cc_upper}=1)
            list(APPEND _cc_linked_providers ${_cc_provider})
        endif()
    endforeach()
    if(NOT TARGET UltraIMDirectCore)
        message(WARNING "ChatCube for Android without ChatCube Direct: UltraDatabase (SQLite) and UltraVault are "
                        "needed - build the sysroot with --with-chatcube and configure with "
                        "-DULTRACANVAS_ENABLE_DATABASE=ON")
    endif()
else()
    add_executable(ChatCube ${CHATCUBE_SOURCES})
    # One copy per user (UltraCanvasSingleInstance.h). An Android activity is
    # one already.
    ultracanvas_single_instance(ChatCube)
    set_target_properties(ChatCube PROPERTIES ENABLE_EXPORTS ON)
endif()

target_include_directories(ChatCube PRIVATE
    ${ULTRACANVAS_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/ui)
target_link_libraries(ChatCube PRIVATE ${ULTRACANVAS_LIBRARY} UltraIM UltraRTC UltraNet)
# The preview picture of a video sent comes from VideoFX; without it, videos
# go without one.
if(TARGET VideoFX)
    target_link_libraries(ChatCube PRIVATE VideoFX)
    target_compile_definitions(ChatCube PRIVATE CHATCUBE_HAS_VIDEOFX=1)
endif()
find_package(Threads REQUIRED)
target_link_libraries(ChatCube PRIVATE Threads::Threads)
target_compile_features(ChatCube PRIVATE cxx_std_20)
# The providers ChatCube talks through are modules: build them with the app
# (on Android they are linked in, above).
if(TARGET UltraIMDirectPlugin AND NOT ANDROID)
    add_dependencies(ChatCube UltraIMDirectPlugin)
    if(TARGET UltraIMMatrixPlugin)
        add_dependencies(ChatCube UltraIMMatrixPlugin)
    endif()
    if(TARGET UltraIMTelegramPlugin)
        add_dependencies(ChatCube UltraIMTelegramPlugin)
    endif()
    if(TARGET UltraIMMastodonPlugin)
        add_dependencies(ChatCube UltraIMMastodonPlugin)
    endif()
    if(TARGET UltraIMMattermostPlugin)
        add_dependencies(ChatCube UltraIMMattermostPlugin)
    endif()
    if(TARGET UltraIMZulipPlugin)
        add_dependencies(ChatCube UltraIMZulipPlugin)
    endif()
    if(TARGET UltraIMSignalPlugin)
        add_dependencies(ChatCube UltraIMSignalPlugin)
    endif()
    if(TARGET UltraIMXmppPlugin)
        add_dependencies(ChatCube UltraIMXmppPlugin)
    endif()
    if(TARGET UltraIMIrcPlugin)
        add_dependencies(ChatCube UltraIMIrcPlugin)
    endif()
    # The local network's discovery (mDNS) is an UltraNet plug-in.
    if(TARGET UltraNetMdnsPlugin)
        add_dependencies(ChatCube UltraNetMdnsPlugin)
    endif()
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

if(ANDROID)
    string(REPLACE ";" ", " _cc_linked_text "${_cc_linked_providers}")
    message(STATUS "  [✓] ChatCube - ENABLED (Android library libChatCube.so; providers linked in: ${_cc_linked_text})")
else()
    message(STATUS "  [✓] ChatCube - ENABLED (target: ChatCube)")
endif()
message(STATUS "      Version ${CHATCUBE_VERSION} (Docs/ChatCube/CHANGELOG.md)")

# chatcube-peer: a second ChatCube Direct identity on the command line, for
# trying the app and the relay without a second person (peer/main.cpp).
if(TARGET UltraIMDirectCore AND NOT ANDROID)
    add_executable(chatcube-peer peer/main.cpp)
    target_compile_features(chatcube-peer PRIVATE cxx_std_20)
    target_link_libraries(chatcube-peer PRIVATE UltraIMDirectCore)
endif()
