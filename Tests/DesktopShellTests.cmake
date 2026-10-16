# Tests/DesktopShellTests.cmake
# DesktopShellTest. Included by Tests/CMakeLists.txt under BUILD_TESTS, and by
# the top-level CMakeLists.txt on its own under
# ULTRACANVAS_BUILD_DESKTOPSHELL_TESTS - so the Windows CI row, which builds
# no full test suite, runs the Win32 backend of UltraCanvasDesktopShell
# against a real desktop: its window list, monitor, screen edges, hot keys,
# screenshot, the Start menu, the shell guard and the notification area.
# Paths are relative to this file, so either includer works.
#
# Also ConsoleCompanionTest (Windows Release builds, where console companions
# exist): the .com an application's command line runs through, on real
# Windows - redirected, and on a console of its own with typed input and
# Ctrl+C.

set(_DST_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(_DST_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/../UltraCanvas/include")
# On Windows beside the core DLL (libUltraCanvas.dll, at the build root like
# the apps): an executable finds its DLLs in its own directory, and build/bin
# is on no search path.
if(WIN32)
    set(_DST_BIN_DIR "$<TARGET_FILE_DIR:UltraCanvas>")
else()
    set(_DST_BIN_DIR "${CMAKE_BINARY_DIR}/bin")
endif()

# ===== DESKTOP SHELL TEST =====
# The platform-neutral half of UltraCanvasDesktopShell: the notices an
# application publishes for the desktop, the program lookup, the application
# list from desktop entries, the screenshot file name, and the monitor's
# lifetime. Points XDG_RUNTIME_DIR and XDG_DATA_HOME at a throwaway directory,
# so it never depends on what the machine has installed and needs no display.
# With an X display (the Linux CI rows run it again under xvfb-run) also the
# X11 backend; on Windows the Win32 backend, on the runner's desktop.
if(TARGET UltraCanvas)
    message(STATUS "  Building DesktopShellTest...")
    add_executable(DesktopShellTest
        ${_DST_DIR}/DesktopShellTest.cpp
    )
    target_include_directories(DesktopShellTest PRIVATE ${_DST_INCLUDE_DIR})
    target_compile_features(DesktopShellTest PRIVATE cxx_std_20)
    target_link_libraries(DesktopShellTest PRIVATE UltraCanvas)
    set_target_properties(DesktopShellTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${_DST_BIN_DIR}
    )
    # On Windows the test starts UltraCanvasShellGuard from beside itself
    # (both are built to the build root): built first.
    if(TARGET UltraCanvasShellGuard)
        add_dependencies(DesktopShellTest UltraCanvasShellGuard)
    endif()
    add_test(NAME DesktopShellTest COMMAND DesktopShellTest
             WORKING_DIRECTORY ${_DST_BIN_DIR})
    message(STATUS "    Test registered: DesktopShellTest")

    # ===== CONSOLE COMPANION TEST (Windows Release) =====
    # ConsoleCompanionProbe.exe / .com are made the way the applications'
    # are (ultracanvas_windows_console_companion()), so the test drives the
    # real companion and the real console attach of the .exe.
    if(WIN32 AND CMAKE_BUILD_TYPE STREQUAL "Release")
        include(${_DST_DIR}/../cmake/UltraCanvasWindowsGuiApp.cmake)
        add_executable(ConsoleCompanionProbe ${_DST_DIR}/ConsoleCompanionProbe.cpp)
        target_compile_features(ConsoleCompanionProbe PRIVATE cxx_std_20)
        target_link_libraries(ConsoleCompanionProbe PRIVATE UltraCanvas)
        set_target_properties(ConsoleCompanionProbe PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${_DST_BIN_DIR})
        ultracanvas_windows_console_companion(ConsoleCompanionProbe)

        add_executable(ConsoleCompanionTest ${_DST_DIR}/ConsoleCompanionTest.cpp)
        target_compile_features(ConsoleCompanionTest PRIVATE cxx_std_20)
        target_link_libraries(ConsoleCompanionTest PRIVATE shell32)
        set_target_properties(ConsoleCompanionTest PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${_DST_BIN_DIR})
        add_dependencies(ConsoleCompanionTest ConsoleCompanionProbe)
        add_test(NAME ConsoleCompanionTest COMMAND ConsoleCompanionTest
                 WORKING_DIRECTORY ${_DST_BIN_DIR})
        message(STATUS "    Test registered: ConsoleCompanionTest")
    endif()
else()
    message(STATUS "  DesktopShellTest skipped (UltraCanvas target not present)")
endif()
