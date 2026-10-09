# Tests/VaultTests.cmake
# UltraVaultTests: memory-backend CRUD and lifecycle, encrypted-file
# persistence, the no-oracle property (wrong passphrase and a tampered file
# are indistinguishable), and DeviceKeyVault - its unlock status on every
# path, and a vault in a Thai-and-emoji folder. Compiles only UltraVault +
# UltraCrypt.
#
# Included by the top-level CMakeLists.txt under BUILD_TESTS, and on its own
# under ULTRACANVAS_BUILD_VAULT_TESTS - so the Windows CI row, which builds no
# full test suite, runs the vault where a folder name goes through UTF-16 and
# a code page (the runner's is 1252, which has no Thai). Until then the UTF-8
# folder test ran only on Linux and macOS, where it cannot fail.
add_executable(UltraVaultTests ${CMAKE_CURRENT_LIST_DIR}/UltraVaultTests.cpp)
target_link_libraries(UltraVaultTests PRIVATE UltraVault)
target_compile_features(UltraVaultTests PRIVATE cxx_std_20)
set_target_properties(UltraVaultTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(NAME UltraVaultTests COMMAND UltraVaultTests
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
# UltraVault is a home in the shared core (UltraCanvas/CMakeLists.txt, "MODULE
# HOMES"), so this binary loads libUltraCanvas. On Windows the loader looks
# beside the executable, in the working directory and on PATH - and the DLL
# is in the build root while the test is in bin/, which exited it with
# STATUS_DLL_NOT_FOUND (0xc0000135) in CI. Linux and macOS find it through
# the build-tree rpath; the extra PATH entry is harmless there.
set_tests_properties(UltraVaultTests PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${CMAKE_BINARY_DIR}")
