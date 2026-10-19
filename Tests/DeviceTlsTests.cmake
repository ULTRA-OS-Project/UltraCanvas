# Tests/DeviceTlsTests.cmake
# IODeviceScannerESCLLiveTest: the eSCL backend scanning over HTTPS from
# IODeviceScannerESCLLiveScanner.py, a scanner that answers only over TLS with
# a self-signed certificate, which the test starts itself - trust on first
# use, a key that changes, scanning over the pinned connection.
#
# Included by Tests/CMakeLists.txt under BUILD_TESTS, and by the top-level
# CMakeLists.txt on its own under ULTRACANVAS_BUILD_DEVICE_TLS_TESTS - so the
# macOS and Windows CI rows, which build no full test suite, run the trust
# on the TLS libcurl uses there (Apple's on macOS, Schannel on Windows), not
# only on Linux's OpenSSL. Paths are relative to this file, so either
# includer works.
#
# Python 3, the scanner script and openssl are passed as arguments. The test
# is registered without them when CMake finds no Python 3 or no openssl, and
# then skips - exit code 77 - or, where ULTRACANVAS_TEST_ESCL_REQUIRED is set,
# fails, so a row that requires it cannot quietly lose it.
if(TARGET UltraCanvas)
    set(_DTT_DIR "${CMAKE_CURRENT_LIST_DIR}")
    # On Windows beside the core DLL, which an executable finds only in its
    # own directory (see FilerTests.cmake).
    if(WIN32)
        set(_DTT_BIN_DIR "$<TARGET_FILE_DIR:UltraCanvas>")
    else()
        set(_DTT_BIN_DIR "${CMAKE_BINARY_DIR}/bin")
    endif()

    find_package(Python3 QUIET COMPONENTS Interpreter)
    find_program(ULTRACANVAS_OPENSSL_PROGRAM openssl)

    message(STATUS "  Building IODeviceScannerESCLLiveTest...")
    add_executable(IODeviceScannerESCLLiveTest ${_DTT_DIR}/IODeviceScannerESCLLiveTest.cpp)
    target_include_directories(IODeviceScannerESCLLiveTest PRIVATE ${_DTT_DIR}/../UltraCanvas/include)
    target_compile_features(IODeviceScannerESCLLiveTest PRIVATE cxx_std_20)
    target_link_libraries(IODeviceScannerESCLLiveTest PRIVATE UltraCanvas)
    set_target_properties(IODeviceScannerESCLLiveTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${_DTT_BIN_DIR}
    )
    if(Python3_Interpreter_FOUND AND ULTRACANVAS_OPENSSL_PROGRAM)
        add_test(NAME IODeviceScannerESCLLiveTest
                 COMMAND IODeviceScannerESCLLiveTest ${Python3_EXECUTABLE}
                         ${_DTT_DIR}/IODeviceScannerESCLLiveScanner.py
                         ${ULTRACANVAS_OPENSSL_PROGRAM}
                 WORKING_DIRECTORY ${_DTT_BIN_DIR})
    else()
        message(STATUS "    IODeviceScannerESCLLiveTest will skip: no Python 3 or no openssl")
        add_test(NAME IODeviceScannerESCLLiveTest COMMAND IODeviceScannerESCLLiveTest
                 WORKING_DIRECTORY ${_DTT_BIN_DIR})
    endif()
    set_tests_properties(IODeviceScannerESCLLiveTest PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 180)
    message(STATUS "    Test registered: IODeviceScannerESCLLiveTest")
endif()
