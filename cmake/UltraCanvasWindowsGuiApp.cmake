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
# --list, UltraFIBU's commands) needs the prompt to wait for it and see its
# exit code, which a prompt does only for a console program. Such an app
# calls ultracanvas_windows_console_companion() instead: a GUI program all the
# same, plus <App>.com beside <App>.exe - a small console program
# (OS/MSWindows/AppEntry/UltraCanvasConsoleCompanion.cpp) that starts the .exe
# with the same command line, waits for it and returns its exit code. cmd and
# PowerShell find the .com first when the name is typed without an extension
# (PATHEXT lists .COM before .EXE), so `UltraCleaner --scan` behaves as a
# console program does, while the Start menu, Explorer and the sign-in start
# the .exe without a console window. Typed as `UltraCleaner.exe`, it is the
# GUI program: the prompt does not wait. See the list under "Hide console
# window on Windows Release builds" in the root CMakeLists.txt.

set(_ULTRACANVAS_ATTACH_CONSOLE_SOURCE
    "${CMAKE_CURRENT_LIST_DIR}/../UltraCanvas/OS/MSWindows/AppEntry/UltraCanvasWindowsAttachConsole.cpp")
set(_ULTRACANVAS_CONSOLE_COMPANION_SOURCE
    "${CMAKE_CURRENT_LIST_DIR}/../UltraCanvas/OS/MSWindows/AppEntry/UltraCanvasConsoleCompanion.cpp")

function(ultracanvas_windows_gui_app TARGET_NAME)
    if(NOT WIN32 OR NOT CMAKE_BUILD_TYPE STREQUAL "Release")
        return()
    endif()
    set_target_properties(${TARGET_NAME} PROPERTIES WIN32_EXECUTABLE TRUE)
    target_sources(${TARGET_NAME} PRIVATE "${_ULTRACANVAS_ATTACH_CONSOLE_SOURCE}")
endfunction()

# The GUI program and its console companion: <App>.com, built to the same
# folder under the same name as the .exe, and with it (a dependency of the
# app's target). It links nothing of the framework and its C++ runtime
# statically, like UltraCanvasShellGuard.
function(ultracanvas_windows_console_companion TARGET_NAME)
    if(NOT WIN32 OR NOT CMAKE_BUILD_TYPE STREQUAL "Release")
        return()
    endif()
    ultracanvas_windows_gui_app(${TARGET_NAME})
    get_target_property(_uc_name ${TARGET_NAME} OUTPUT_NAME)
    if(NOT _uc_name)
        set(_uc_name ${TARGET_NAME})
    endif()
    set(_uc_companion ${TARGET_NAME}ConsoleCompanion)
    add_executable(${_uc_companion} "${_ULTRACANVAS_CONSOLE_COMPANION_SOURCE}")
    set_target_properties(${_uc_companion} PROPERTIES OUTPUT_NAME "${_uc_name}" SUFFIX ".com")
    get_target_property(_uc_dir ${TARGET_NAME} RUNTIME_OUTPUT_DIRECTORY)
    if(_uc_dir)
        set_target_properties(${_uc_companion} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${_uc_dir}")
    endif()
    target_compile_features(${_uc_companion} PRIVATE cxx_std_20)
    if(MINGW)
        target_link_options(${_uc_companion} PRIVATE -static -s)
    elseif(MSVC)
        set_property(TARGET ${_uc_companion} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
    endif()
    add_dependencies(${TARGET_NAME} ${_uc_companion})
endfunction()
