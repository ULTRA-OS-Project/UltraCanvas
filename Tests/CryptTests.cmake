# Tests/CryptTests.cmake
# UltraCryptTests: published vectors (FIPS 180-4, RFC 4231), AEAD round-trip
# plus the mandated negative cases, KDF behaviour, secure buffer semantics,
# and X.509 / CMS - among them that the system's roots are the operating
# system's. Compiles only UltraCrypt, so it runs headless.
#
# Included by the top-level CMakeLists.txt under BUILD_TESTS, and on its own
# under ULTRACANVAS_BUILD_CRYPT_TESTS - so the Windows and macOS CI rows,
# which build no full test suite, check the trust store where it is the
# operating system's (Windows' ROOT store, macOS's anchors): on Linux it is
# OpenSSL's own, and a test there cannot tell the two apart.
add_executable(UltraCryptTests ${CMAKE_CURRENT_LIST_DIR}/UltraCryptTests.cpp)
# UltraCrypt brings UltraCanvasBase32; the test also checks Base64 vectors,
# which live with the rest of the core's text utilities.
target_link_libraries(UltraCryptTests PRIVATE UltraCrypt UltraCanvasTextUtils)
target_compile_features(UltraCryptTests PRIVATE cxx_std_20)
set_target_properties(UltraCryptTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(NAME UltraCryptTests COMMAND UltraCryptTests
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
# UltraCrypt is a home in the shared core, so on Windows the binary in bin/
# needs the build root (where the core's DLL is) on PATH, as UltraVaultTests
# does (Tests/VaultTests.cmake).
set_tests_properties(UltraCryptTests PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${CMAKE_BINARY_DIR}")
