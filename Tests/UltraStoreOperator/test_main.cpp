// Tests/UltraStoreOperator/test_main.cpp
// Entry point for the UltraStoreOperator core test binary (the framework
// of Tests/UltraStore).
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include <cstdio>

int main() {
    std::printf("Running UltraStoreOperator core test suite\n\n");
    return ultrastore_test::RunAll();
}
