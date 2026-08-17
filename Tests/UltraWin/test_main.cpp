// Tests/UltraWin/test_main.cpp
// Entry point for the UltraWin test binary. All TEST() bodies in the other
// translation units self-register; main() just runs them.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "test_framework.h"

int main(int, char**) {
    return ultrawin_test::RunAllTests();
}
