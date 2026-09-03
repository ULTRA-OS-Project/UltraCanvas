// Tests/UltraCloud/test_main.cpp
// Entry point for the UltraCloud test binary.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

int main() {
    std::printf("Running UltraCloud test suite\n\n");
    return ultracloud_test::RunAll();
}
