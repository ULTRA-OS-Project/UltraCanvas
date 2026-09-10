// SmartHome/tests/ThreadLinkTest.cpp
// Built only when ULTRACANVAS_SMARTHOME_THREAD=ON.
//
// Proves the Thread backend is linked against a real OpenThread rather than
// compiled into a shell: the hardware description carries the version string
// that only libopenthread-ftd can supply, and the backend answers questions
// about itself without a radio attached.
//
// Deliberately NOT called: Initialize(). The POSIX platform's otSysInit opens
// the radio co-processor named by the URL and, on a missing device, exits the
// process rather than returning an error. That is OpenThread's behaviour, not
// this backend's, and a unit test has no radio to offer it.
//
// Author: UltraCanvas Framework

#include "UltraCanvasSmartHome.h"
#include "ISmartHomeProtocol.h"
#include "ThreadProtocol.h"

#include <cstdio>
#include <string>

using namespace UltraCanvas::SmartHome;

int main() {
    auto protocol = CreateThreadProtocol();
    if (!protocol) { std::puts("FAIL: CreateThreadProtocol returned nothing"); return 1; }
    std::puts("ok  the Thread backend constructs");

    if (protocol->GetType() != SmartHomeProtocolType::Thread) {
        std::puts("FAIL: wrong protocol type"); return 1;
    }
    if (protocol->GetName().empty()) {
        std::puts("FAIL: the base constructor was never given a name"); return 1;
    }
    std::printf("ok  identifies itself: %s\n", protocol->GetName().c_str());

    const std::string info = protocol->GetHardwareInfo();
    if (info.empty()) { std::puts("FAIL: GetHardwareInfo said nothing"); return 1; }
    std::printf("ok  %s\n", info.c_str());

    // otGetVersionString() answers "OPENTHREAD/<commit>; POSIX; <date>". A
    // shell build would say "OpenThread not compiled in" instead.
    if (info.find("OPENTHREAD/") == std::string::npos) {
        std::puts("FAIL: no OpenThread version string; the backend is not linked to the stack");
        return 1;
    }
    std::puts("ok  the version string comes from libopenthread itself");

    std::printf("ok  hardware available: %s\n",
                protocol->IsHardwareAvailable() ? "yes" : "no (as expected without a radio)");

    // Nothing is attached, so there is no mesh to ask about; these must answer
    // rather than crash.
    if (protocol->HasNetwork()) { std::puts("FAIL: claims a network with no radio"); return 1; }
    if (!protocol->GetPairedDevices().empty()) { std::puts("FAIL: invented devices"); return 1; }
    std::puts("ok  no network and no devices, honestly");

    std::puts("\nPASS - Thread links against OpenThread and answers for itself");
    return 0;
}
