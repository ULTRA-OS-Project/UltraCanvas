# Tests/StartPageTests.cmake
# StartPageTest (UltraCanvasStartPage without a window) and AppSettingsTest
# (UltraCanvasAppSettings on a temp folder). Included by Tests/CMakeLists.txt
# under BUILD_TESTS, and by the top-level CMakeLists.txt on its own under
# ULTRACANVAS_BUILD_STARTPAGE_TESTS - so the macOS and Windows CI rows, which
# build no full test suite, still run the two on their own path rules and
# settings folders. Paths are relative to this file, so either includer works.

set(_SPT_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(_SPT_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/../UltraCanvas/include")
# Where the test executables go. On Windows beside the core DLL: an
# executable finds its DLLs in its own directory, and build/bin is on no
# search path (see VectorFormatsTests.cmake).
if(WIN32)
    set(_SPT_BIN_DIR "$<TARGET_FILE_DIR:UltraCanvas>")
else()
    set(_SPT_BIN_DIR "${CMAKE_BINARY_DIR}/bin")
endif()

if(TARGET UltraCanvas)
    # UltraCanvasStartPage without a window: the parts laid out top to bottom,
    # the preferred height growing by what each takes, the recent entries as
    # buttons, the action as wide as the column, a section at the margin and
    # the rows AddField, AddNote and AddButton put in it, the status line in
    # the error colour. Headless.
    message(STATUS "  Building StartPageTest...")
    add_executable(StartPageTest
        ${_SPT_DIR}/StartPageTest.cpp
    )
    target_include_directories(StartPageTest PRIVATE ${_SPT_INCLUDE_DIR})
    target_compile_features(StartPageTest PRIVATE cxx_std_20)
    target_link_libraries(StartPageTest PRIVATE UltraCanvas)
    set_target_properties(StartPageTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${_SPT_BIN_DIR}
    )
    add_test(NAME StartPageTest COMMAND StartPageTest
             WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    message(STATUS "    Test registered: StartPageTest")

    # UltraCanvasAppSettings: an application's preferences in one JSON file -
    # typed values, dotted keys, a recent list, the file written and read back,
    # a broken one as the defaults, the file under the settings folder.
    message(STATUS "  Building AppSettingsTest...")
    add_executable(AppSettingsTest
        ${_SPT_DIR}/AppSettingsTest.cpp
    )
    target_include_directories(AppSettingsTest PRIVATE ${_SPT_INCLUDE_DIR})
    target_compile_features(AppSettingsTest PRIVATE cxx_std_20)
    target_link_libraries(AppSettingsTest PRIVATE UltraCanvas)
    set_target_properties(AppSettingsTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${_SPT_BIN_DIR}
    )
    add_test(NAME AppSettingsTest COMMAND AppSettingsTest
             WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    message(STATUS "    Test registered: AppSettingsTest")
else()
    message(STATUS "  StartPageTest and AppSettingsTest skipped (UltraCanvas target not present)")
endif()
