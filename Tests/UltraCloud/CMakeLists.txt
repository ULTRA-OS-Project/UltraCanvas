# Tests/UltraCloud/CMakeLists.txt
# Builds the UltraCloudTests binary. Enabled by ULTRACANVAS_BUILD_ULTRACLOUD_TESTS.
# Headless: the provider tests inject a fake HTTP function, nothing is contacted.

cmake_minimum_required(VERSION 3.16)

add_executable(UltraCloudTests
    test_main.cpp
    test_accounts.cpp
    test_secrets.cpp
    test_webdav.cpp
    test_nextcloud.cpp
    test_service.cpp
    test_oauth.cpp
    test_dropbox.cpp
    test_onedrive.cpp
    test_googledrive.cpp)

target_compile_features(UltraCloudTests PRIVATE cxx_std_20)
target_link_libraries(UltraCloudTests PRIVATE UltraCloud)
target_include_directories(UltraCloudTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Same link-order note as Tests/UltraMail: UltraNet's helpers call into the
# UltraCanvas core library, which must follow UltraNet on the link line.
if(TARGET UltraNet)
    target_link_libraries(UltraCloudTests PRIVATE UltraNet)
endif()
if(ULTRACANVAS_LIBRARY)
    target_link_libraries(UltraCloudTests PRIVATE ${ULTRACANVAS_LIBRARY})
endif()

include(CTest)
add_test(NAME UltraCloud COMMAND UltraCloudTests)
