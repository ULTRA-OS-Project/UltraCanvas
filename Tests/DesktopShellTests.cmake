# Tests/DesktopShellTests.cmake
# DesktopShellTest. Included by Tests/CMakeLists.txt under BUILD_TESTS, and by
# the top-level CMakeLists.txt on its own under
# ULTRACANVAS_BUILD_DESKTOPSHELL_TESTS - so the Windows CI row, which builds
# no full test suite, runs the Win32 backend of UltraCanvasDesktopShell
# against a real desktop: its window list, monitor, screen edges, hot keys,
# screenshot, the Start menu, the shell guard and the notification area.
# Paths are relative to this file, so either includer works.

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
# On Windows also the Win32 backend, on the runner's desktop.
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
else()
    message(STATUS "  DesktopShellTest skipped (UltraCanvas target not present)")
endif()
