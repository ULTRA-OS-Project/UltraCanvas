// Tests/UltraMessage/test_main.cpp
// Entry point for the UltraMessage test binary.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include <UltraMessage/UltraMessage.h>

int main() {
    std::printf("Running UltraMessage test suite\n\n");
    const int rc = ultramsg_test::RunAll();
    UltraMsg_Shutdown();
    return rc;
}
