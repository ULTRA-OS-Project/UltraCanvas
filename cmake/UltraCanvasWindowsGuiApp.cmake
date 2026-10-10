# cmake/UltraCanvasWindowsGuiApp.cmake
# Reusable CMake function that makes an application a Windows GUI-subsystem
# program in a Release build, so no empty console window opens beside its
# window. Debug builds keep the console, where debugOutput writes.
#
# A GUI-subsystem program is given no console, so what main() prints before
# the window opens - `--help`, `--version`, a bad argument - went nowhere,
# even when the program was typed at a prompt. The function therefore also
# compiles OS/MSWindows/AppEntry/UltraCanvasWindowsAttachConsole.cpp into the
# executable, which attaches to the prompt's console before main() runs. A
# prompt does not wait for a GUI program, so the text appears after the prompt
# has come back; redirected to a file (`app --help > help.txt`) it is complete.
#
# That source is compiled into the executable, never into the core library:
# the core is a DLL in the shared build, and a DLL's static initialisers run
# under the loader lock, which is no place to attach a console and reopen
# stdio. MinGW's startup code enters main() under either subsystem, so the
# application needs no WinMain.
#
# An app whose command-line modes print into the prompt (DeviceExplorer
# --list, UltraFIBU's commands) must stay a console program instead, so the
# prompt waits for it and sees its exit code; see the list under "Hide console
# window on Windows Release builds" in the root CMakeLists.txt.

set(_ULTRACANVAS_ATTACH_CONSOLE_SOURCE
    "${CMAKE_CURRENT_LIST_DIR}/../UltraCanvas/OS/MSWindows/AppEntry/UltraCanvasWindowsAttachConsole.cpp")

function(ultracanvas_windows_gui_app TARGET_NAME)
    if(NOT WIN32 OR NOT CMAKE_BUILD_TYPE STREQUAL "Release")
        return()
    endif()
    set_target_properties(${TARGET_NAME} PROPERTIES WIN32_EXECUTABLE TRUE)
    target_sources(${TARGET_NAME} PRIVATE "${_ULTRACANVAS_ATTACH_CONSOLE_SOURCE}")
endfunction()
