# Tests/UltraIM/CMakeLists.txt
# Builds UltraIMTests and the provider module it loads. In-tree only:
# enabled by ULTRACANVAS_BUILD_ULTRAIM_TESTS, links UltraIM (the module's
# home: the shared core, or the archive under a static core).

add_library(UltraIMTestProviderModule MODULE TestProviderModule.cpp)
set_target_properties(UltraIMTestProviderModule PROPERTIES
    OUTPUT_NAME "ultraim_testprovider"
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/modules)
target_compile_features(UltraIMTestProviderModule PRIVATE cxx_std_20)
target_link_libraries(UltraIMTestProviderModule PRIVATE UltraIM)

add_executable(UltraIMTests UltraIMTests.cpp)
target_compile_features(UltraIMTests PRIVATE cxx_std_20)
target_link_libraries(UltraIMTests PRIVATE UltraIM)
target_compile_definitions(UltraIMTests PRIVATE
    ULTRAIM_TEST_MODULE_DIR="$<TARGET_FILE_DIR:UltraIMTestProviderModule>")
add_dependencies(UltraIMTests UltraIMTestProviderModule)
add_test(NAME UltraIMTests COMMAND UltraIMTests)

# ChatCube Direct: envelope and store units always; the end-to-end part
# starts the relay from Server/ChatCubeRelay with the python3 it finds and
# skips when that python has no fastapi / uvicorn / cryptography.
if(TARGET UltraIMDirectCore)
    add_executable(DirectProviderTests DirectProviderTests.cpp)
    target_compile_features(DirectProviderTests PRIVATE cxx_std_20)
    target_link_libraries(DirectProviderTests PRIVATE UltraIMDirectCore)
    target_compile_definitions(DirectProviderTests PRIVATE
        ULTRAIM_RELAY_DIR="${CMAKE_SOURCE_DIR}/Server/ChatCubeRelay"
        ULTRAIM_NET_PLUGIN_DIR="${CMAKE_BINARY_DIR}/Plugins/UltraNet")
    if(TARGET UltraNetMdnsPlugin)
        add_dependencies(DirectProviderTests UltraNetMdnsPlugin)
    endif()
    add_test(NAME DirectProviderTests COMMAND DirectProviderTests)
    set_tests_properties(DirectProviderTests PROPERTIES TIMEOUT 300)
endif()

# Matrix: against a real homeserver, Synapse, which matrix_test_server.py
# starts with the python in ULTRAIM_MATRIX_PYTHON (else python3); skips when
# that python has no matrix-synapse.
if(TARGET UltraIMMatrixCore)
    # The encryption on its own, no homeserver: canonical JSON, vodozemac
    # through the shim, the key store, room-key export files.
    add_executable(MatrixCryptoTests MatrixCryptoTests.cpp)
    target_compile_features(MatrixCryptoTests PRIVATE cxx_std_20)
    target_link_libraries(MatrixCryptoTests PRIVATE UltraIMMatrixCore)
    add_test(NAME MatrixCryptoTests COMMAND MatrixCryptoTests)

    add_executable(MatrixProviderTests MatrixProviderTests.cpp)
    target_compile_features(MatrixProviderTests PRIVATE cxx_std_20)
    target_link_libraries(MatrixProviderTests PRIVATE UltraIMMatrixCore)
    target_compile_definitions(MatrixProviderTests PRIVATE ULTRAIM_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME MatrixProviderTests COMMAND MatrixProviderTests)
    set_tests_properties(MatrixProviderTests PROPERTIES TIMEOUT 300)
endif()
