# Apps/UltraClaude/CMakeLists.txt
# UltraClaude — a desktop chat window for Claude on a Claude Pro / Max
# subscription. It runs the official Claude Code CLI as a child process and
# shows its stream-json output; it holds no API key and no credential.
#
# One target, the GUI application; its --print mode is the same engine
# without a window.
#
# Version: 0.1.0
# Author: UltraCanvas Framework / ULTRA OS

if(NOT ULTRACANVAS_LIBRARY)
    message(STATUS "  [−] UltraClaude - SKIPPED (UltraCanvas UI library not built)")
    return()
endif()

add_executable(UltraClaude
    main.cpp
    engine/ClaudeChatSession.cpp
    engine/ClaudeCliProcess.cpp
    engine/ClaudeStreamParser.cpp
    ui/UltraClaudeWindow.cpp
)

# Export dynamic symbols so on-demand plugin modules resolve core symbols
# when the core is linked statically, as the other applications do.
set_target_properties(UltraClaude PROPERTIES ENABLE_EXPORTS ON)

target_include_directories(UltraClaude PRIVATE
    ${ULTRACANVAS_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/engine
    ${CMAKE_CURRENT_SOURCE_DIR}/ui)

target_link_libraries(UltraClaude PRIVATE ${ULTRACANVAS_LIBRARY})
if(TARGET fmt::fmt)
    target_link_libraries(UltraClaude PRIVATE fmt::fmt)
endif()
find_package(Threads REQUIRED)
target_link_libraries(UltraClaude PRIVATE Threads::Threads)
target_compile_features(UltraClaude PRIVATE cxx_std_20)

# UltraClaude versions itself: the number comes from the first line of
# Docs/UltraClaude/CHANGELOG.md through cmake/UltraCanvasVersion.cmake.
target_compile_definitions(UltraClaude PRIVATE
    ULTRACLAUDE_VERSION="${ULTRACLAUDE_VERSION}"
    UCAPP_ICON_PATH="media/appicon/UltraClaude.png"
    $<$<CONFIG:Debug>:ULTRACANVAS_DEBUG=1>
)

ultracanvas_embed_windows_manifest(UltraClaude)
ultracanvas_embed_app_icon(UltraClaude
    "${CMAKE_SOURCE_DIR}/media/appicon/UltraClaude.png")

if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN)
    install(TARGETS UltraClaude RUNTIME DESTINATION bin)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/UltraClaude.desktop
            DESTINATION share/applications)
    install(FILES ${CMAKE_SOURCE_DIR}/media/appicon/UltraClaude.png
            DESTINATION share/icons/hicolor/256x256/apps)
    install(FILES ${CMAKE_SOURCE_DIR}/media/appicon/UltraClaude.svg
            DESTINATION share/icons/hicolor/scalable/apps)
endif()

message(STATUS "  [✓] UltraClaude - ENABLED (target: UltraClaude)")
message(STATUS "      Version ${ULTRACLAUDE_VERSION} (Docs/UltraClaude/CHANGELOG.md)")
