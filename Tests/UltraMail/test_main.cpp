// Tests/UltraMail/test_main.cpp
// Entry point for the UltraMail engine test binary.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

int main() {
    std::printf("Running UltraMail engine test suite\n\n");
    return ultramail_test::RunAll();
}
