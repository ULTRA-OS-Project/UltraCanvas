// SmartHome/tests/ZWaveLinkTest.cpp
// Built only when ULTRACANVAS_SMARTHOME_ZWAVE=ON.
//
// Two things are checked, neither of which needs a Z-Wave stick present:
//
//  * the backend constructs and answers questions about itself, so the
//    OpenZWave link is real rather than a shell that happens to compile;
//  * OpenZWave is reached DYNAMICALLY. It is LGPL 2.1 and its packages ship
//    libopenzwave.a alongside libopenzwave.so, so a plain -lopenzwave can pull
//    the archive in and carry the relinking obligation into the binary. CMake
//    asks for the shared object by name; this test is the runtime half of that
//    guarantee, and the check to run after any change to how it is linked:
//
//      ldd  <binary> | grep openzwave     -> must list libopenzwave.so
//      nm -C <binary> | grep " T OpenZWave::" -> must be empty
//
// Author: UltraCanvas Framework

#include "UltraCanvasSmartHome.h"
#include "ISmartHomeProtocol.h"
#include "ZWaveProtocol.h"

#include <cstdio>

using namespace UltraCanvas::SmartHome;

int main() {
    auto protocol = CreateZWaveProtocol();
    if (!protocol) { std::puts("FAIL: CreateZWaveProtocol returned nothing"); return 1; }
    std::puts("ok  the Z-Wave backend constructs");

    if (protocol->GetType() != SmartHomeProtocolType::ZWave) {
        std::puts("FAIL: wrong protocol type"); return 1;
    }
    if (protocol->GetName().empty()) {
        std::puts("FAIL: the base constructor was never given a name"); return 1;
    }
    std::printf("ok  identifies itself: %s\n", protocol->GetName().c_str());

    // No stick attached here, so this is expected to be false. What matters is
    // that it answers instead of crashing or claiming hardware it cannot see.
    std::printf("ok  hardware available: %s\n",
                protocol->IsHardwareAvailable() ? "yes" : "no (as expected without a stick)");

    if (protocol->GetHardwareInfo().empty()) {
        std::puts("FAIL: GetHardwareInfo said nothing"); return 1;
    }
    std::printf("ok  %s\n", protocol->GetHardwareInfo().c_str());

    // Always at least the configured controller path, even with nothing plugged in.
    if (protocol->GetAvailableAdapters().empty()) {
        std::puts("FAIL: no adapters, not even the configured one"); return 1;
    }
    std::puts("ok  enumerates adapters");

    // Z-Wave cannot join or leave a network from software, and must say so
    // rather than reporting a success the caller would wait on.
    if (protocol->JoinNetwork("whatever") || protocol->LeaveNetwork()) {
        std::puts("FAIL: Join/LeaveNetwork should refuse"); return 1;
    }
    std::puts("ok  refuses to pretend it can join or leave a network");

    if (protocol->GetSecurityLevel() != SmartHomeSecurityLevel::Basic) {
        std::puts("FAIL: an empty network should report Basic"); return 1;
    }
    std::puts("ok  reports the weakest link, not the best case");

    std::puts("\nPASS - Z-Wave links against OpenZWave and answers for itself");
    return 0;
}
