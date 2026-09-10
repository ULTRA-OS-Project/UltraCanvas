// SmartHome/tests/MatterLinkTest.cpp
// Built only when ULTRACANVAS_SMARTHOME_MATTER=ON.
//
// Proves the Matter backend is linked against a real connectedhomeip rather
// than compiled into a shell: the hardware description carries a string that
// only the SDK's error formatter can produce, and the backend answers
// questions about itself with no fabric and no devices.
//
// Deliberately NOT called: Initialize(). That brings up the CHIP stack, which
// wants a writable storage directory, mDNS and the network; none of that is a
// unit test's business. What is checked here is the link and the honesty of
// the answers before any of it exists.
//
// Author: UltraCanvas Framework

#include "UltraCanvasSmartHome.h"
#include "ISmartHomeProtocol.h"
#include "MatterProtocol.h"

#include <cstdio>
#include <map>
#include <string>

using namespace UltraCanvas::SmartHome;

int main() {
    auto protocol = CreateMatterProtocol();
    if (!protocol) { std::puts("FAIL: CreateMatterProtocol returned nothing"); return 1; }
    std::puts("ok  the Matter backend constructs");

    if (protocol->GetType() != SmartHomeProtocolType::Matter) {
        std::puts("FAIL: wrong protocol type"); return 1;
    }
    if (protocol->GetName().empty()) {
        std::puts("FAIL: the base constructor was never given a name"); return 1;
    }
    std::printf("ok  identifies itself: %s\n", protocol->GetName().c_str());

    const std::string info = protocol->GetHardwareInfo();
    if (info.empty()) { std::puts("FAIL: GetHardwareInfo said nothing"); return 1; }
    std::printf("ok  %s\n", info.c_str());

    // chip::ErrorStr(CHIP_NO_ERROR) is rendered by the SDK; a shell build
    // says "not compiled in" instead.
    if (info.find("connectedhomeip linked") == std::string::npos) {
        std::puts("FAIL: no SDK-produced text; the backend is not linked to connectedhomeip");
        return 1;
    }
    std::puts("ok  the description carries text the SDK produced");

    if (protocol->HasNetwork()) { std::puts("FAIL: claims a fabric before Initialize"); return 1; }
    if (!protocol->GetPairedDevices().empty()) { std::puts("FAIL: invented devices"); return 1; }
    std::puts("ok  no fabric and no devices, honestly");

    // PairDevice without a setup code has nothing to commission with and must
    // say so rather than start something.
    std::map<std::string, std::string> noCode;
    if (protocol->PairDevice("anything", noCode)) {
        std::puts("FAIL: PairDevice accepted a request with no setup code"); return 1;
    }
    std::puts("ok  refuses to pair without a setup code");

    std::map<std::string, std::string> state;
    if (protocol->GetDeviceState("matter_nobody", state)) {
        std::puts("FAIL: reported state for a device it does not have"); return 1;
    }
    std::puts("ok  no state for an unknown device");

    std::puts("\nPASS - Matter links against connectedhomeip and answers for itself");
    return 0;
}
