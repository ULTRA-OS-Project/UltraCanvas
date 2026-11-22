# Apps/UltraAccount/CMakeLists.txt
# UltraAccount - the one ULTRA OS account: registration, the email and the
# phone number verified, the subscription, the recovery keys applications
# leave with it.
#
# Two targets:
#   UltraAccountCore  static lib: the client of the account service
#                     (Server/UltraStoreAccount) and the session every
#                     application on the machine shares. Linked by the
#                     UltraAccount app and by the applications that rest on
#                     the account - UltraAuthenticator's recovery today.
#   UltraAccount      the window.
# Version: 0.1.0
# Author: UltraCanvas Framework / ULTRA OS

if(NOT ULTRACANVAS_LIBRARY)
    message(STATUS "  [−] UltraAccount - SKIPPED (framework not built)")
    return()
endif()

add_library(UltraAccountCore STATIC
    core/UltraAccountClient.cpp
)
set_target_properties(UltraAccountCore PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME "ultraaccount_core")
target_compile_features(UltraAccountCore PUBLIC cxx_std_20)
target_include_directories(UltraAccountCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/core)
target_include_directories(UltraAccountCore PRIVATE
    ${ULTRACANVAS_INCLUDE_DIRS})
# UltraCanvasJSON and the localisation live in the core; UltraNet is a module
# home that resolves to the shared core, or to its archive under a static one.
target_link_libraries(UltraAccountCore PUBLIC ${ULTRACANVAS_LIBRARY} UltraNet)

add_executable(UltraAccount
    main.cpp
    ui/AccountWindow.cpp
)
# One copy per user (UltraCanvasSingleInstance.h).
ultracanvas_single_instance(UltraAccount)
set_target_properties(UltraAccount PROPERTIES ENABLE_EXPORTS ON)

# The window title and --version read the release recorded in
# Docs/UltraAccount/CHANGELOG.md.
target_compile_definitions(UltraAccount PRIVATE
    ULTRAACCOUNT_VERSION="${ULTRAACCOUNT_VERSION}"
    UCAPP_ICON_PATH="media/appicon/UltraAccount.png")

target_include_directories(UltraAccount PRIVATE
    ${ULTRACANVAS_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(UltraAccount PRIVATE UltraAccountCore ${ULTRACANVAS_LIBRARY})
if(TARGET fmt::fmt)
    target_link_libraries(UltraAccount PRIVATE fmt::fmt)
endif()
if(ULTRACANVAS_PLUGIN_TARGETS)
    target_link_libraries(UltraAccount PRIVATE ${ULTRACANVAS_PLUGIN_TARGETS})
endif()
target_compile_features(UltraAccount PRIVATE cxx_std_20)
set_target_properties(UltraAccount PROPERTIES
    OUTPUT_NAME "UltraAccount"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

ultracanvas_embed_windows_manifest(UltraAccount)
ultracanvas_embed_app_icon(UltraAccount
    "${CMAKE_SOURCE_DIR}/media/appicon/UltraAccount.png")

install(TARGETS UltraAccount RUNTIME DESTINATION bin)

if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/UltraAccount.desktop
            DESTINATION share/applications)
    install(FILES ${CMAKE_SOURCE_DIR}/media/appicon/UltraAccount.png
            DESTINATION share/icons/hicolor/256x256/apps)
    install(FILES ${CMAKE_SOURCE_DIR}/media/appicon/UltraAccount.svg
            DESTINATION share/icons/hicolor/scalable/apps)
endif()

message(STATUS "  [+] UltraAccount - GUI + core")
