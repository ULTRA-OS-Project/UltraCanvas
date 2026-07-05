// Tests/UltraDatabase/test_main.cpp
// Entry point for the UltraDatabase test binary.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

int main() {
    std::printf("Running UltraDatabase test suite\n\n");
    return ultradb_test::RunAll();
}
