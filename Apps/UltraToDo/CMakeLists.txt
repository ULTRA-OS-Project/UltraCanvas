# Apps/UltraToDo/CMakeLists.txt
# UltraToDo — the task list of ULTRA OS: tasks with a planned end, an
# estimated time, a description and attached files (with thumbnails), the
# most urgent first, a reminder before a task has to be started and an alarm
# at its planned end.
# Version: 0.1.0
# Author: UltraCanvas Framework / ULTRA OS

if(NOT ULTRACANVAS_LIBRARY)
    message(STATUS "  [−] UltraToDo - SKIPPED (framework not built)")
    return()
endif()

# The tasks, their order, the reminders and the store: no UI, so the tests
# build them on their own (UltraToDoTests).
add_library(UltraToDoCore STATIC
    core/ToDoTask.cpp
    core/ToDoStore.cpp
    core/ToDoSettings.cpp
)
target_include_directories(UltraToDoCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${ULTRACANVAS_INCLUDE_DIRS})
target_link_libraries(UltraToDoCore PUBLIC ${ULTRACANVAS_LIBRARY})
target_compile_features(UltraToDoCore PUBLIC cxx_std_20)

add_executable(UltraToDo
    main.cpp
    ui/UltraToDoWindow.cpp
    ui/TaskDialog.cpp
    ui/SettingsDialog.cpp
    ui/ToDoFormat.cpp
)

# One instance per user: a second start brings the running one forward
# (UltraCanvasSingleInstance.h).
ultracanvas_single_instance(UltraToDo)

# Export dynamic symbols so on-demand plugin modules resolve core symbols
# when the core is linked statically, as the other applications do.
set_target_properties(UltraToDo PROPERTIES ENABLE_EXPORTS ON)

# The window title and --version read the release recorded in
# Docs/UltraToDo/CHANGELOG.md.
target_compile_definitions(UltraToDo PRIVATE
    ULTRATODO_VERSION="${ULTRATODO_VERSION}")

target_include_directories(UltraToDo PRIVATE
    ${ULTRACANVAS_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/ui)

target_link_libraries(UltraToDo PRIVATE
    UltraToDoCore
    ${ULTRACANVAS_LIBRARY})
# Reminders and alarms go to the desktop as notifications on the UltraMessage
# bus; without the module they are shown in the window alone.
if(TARGET UltraMessage)
    target_link_libraries(UltraToDo PRIVATE UltraMessage)
    target_compile_definitions(UltraToDo PRIVATE ULTRATODO_HAVE_ULTRAMESSAGE=1)
endif()
if(TARGET fmt::fmt)
    target_link_libraries(UltraToDo PRIVATE fmt::fmt)
endif()
if(ULTRACANVAS_PLUGIN_TARGETS)
    target_link_libraries(UltraToDo PRIVATE ${ULTRACANVAS_PLUGIN_TARGETS})
endif()
target_compile_features(UltraToDo PRIVATE cxx_std_20)

set_target_properties(UltraToDo PROPERTIES OUTPUT_NAME "UltraToDo")

# One icon everywhere the app is drawn: main.cpp names the same file for the
# window, and the core falls back to UCAPP_ICON_PATH when an app sets none.
target_compile_definitions(UltraToDo PRIVATE
    UCAPP_ICON_PATH="media/appicon/UltraToDo.png")

ultracanvas_embed_windows_manifest(UltraToDo)
ultracanvas_embed_app_icon(UltraToDo
    "${CMAKE_SOURCE_DIR}/media/appicon/UltraToDo.png")

install(TARGETS UltraToDo RUNTIME DESTINATION bin)

if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/UltraToDo.desktop
            DESTINATION share/applications)
    install(FILES ${CMAKE_SOURCE_DIR}/media/appicon/UltraToDo.png
            DESTINATION share/icons/hicolor/256x256/apps)
    install(FILES ${CMAKE_SOURCE_DIR}/media/appicon/UltraToDo.svg
            DESTINATION share/icons/hicolor/scalable/apps)
endif()

message(STATUS "  [+] UltraToDo")
