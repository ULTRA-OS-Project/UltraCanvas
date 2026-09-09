// Tests/UltraCleaner/test_main.cpp
// Entry point for the UltraCleaner engine test binary.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

int main() {
    std::printf("Running UltraCleaner engine test suite\n\n");
    return ultracleaner_test::RunAll();
}
