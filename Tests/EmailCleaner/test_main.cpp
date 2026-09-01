// Tests/EmailCleaner/test_main.cpp
// Entry point for the EmailCleaner engine test binary.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

int main() {
    std::printf("Running EmailCleaner engine test suite\n\n");
    return emailcleaner_test::RunAll();
}
