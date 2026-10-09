// Tests/UltraCanvasStart/test_main.cpp
// Entry point for the UltraCanvasStart engine test binary.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

int main() {
    std::printf("Running UltraCanvasStart engine test suite\n\n");
    return ultracanvasstart_test::RunAll();
}
