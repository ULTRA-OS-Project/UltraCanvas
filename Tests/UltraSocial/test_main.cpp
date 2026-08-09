// Tests/UltraSocial/test_main.cpp
// Entry point for the UltraSocial engine test binary.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include <UltraNet/UltraNetCore.h>

int main() {
    std::printf("Running UltraSocial engine test suite\n\n");
    UltraNet_Initialize();   // connector tests speak HTTP to loopback fakes
    int rc = ultrasocial_test::RunAll();
    UltraNet_Shutdown();
    return rc;
}
