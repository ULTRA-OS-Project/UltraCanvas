// Tests/UltraStoreAdmin/test_main.cpp
// Entry point for the UltraStoreAdmin core test binary (the framework of
// Tests/UltraStore).
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "UltraCrypt/UltraCryptCore.h"

int main() {
    std::printf("Running UltraStoreAdmin core test suite (UltraCrypt backend: %s)\n\n",
                UltraCrypt_GetBackendName().c_str());
    return ultrastore_test::RunAll();
}
