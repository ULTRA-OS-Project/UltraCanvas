// SmartHome/examples/MatterCommission.cpp
// Commission one Matter device and say what it joined as.
//
//   SmartHomeMatterCommission <storage directory> <setup code> [timeout seconds]
//
// Built as SmartHomeMatterCommission when the Matter backend is
// (ULTRACANVAS_SMARTHOME_MATTER=ON). It starts the backend with its fabric
// stored under <storage directory>, commissions the device a manual pairing
// code or "MT:..." QR payload names, and prints the device the backend
// reported - category, endpoints and the device types each declared - or
// why it failed. Exit status: 0 joined with a known category, 2 joined as
// Unknown, 1 failed.
//
// The device must be reachable on the IP network (on-network commissioning:
// a device already on Wi-Fi or Ethernet, or one shared from another Matter
// controller with its pairing code); BLE commissioning is not wired here.
// connectedhomeip's own example devices make a test bench with no hardware,
// e.g. its Linux robot vacuum:
//
//   chip-rvc-app --KVS /tmp/rvc.ini      # setup code 34970112332
//   SmartHomeMatterCommission /tmp/uc-matter 34970112332
//
// Author: UltraCanvas Framework

#include "UltraCanvasSmartHome.h"
#include "ISmartHomeProtocol.h"
#include "MatterProtocol.h"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>

using namespace UltraCanvas::SmartHome;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <storage directory> <setup code> [timeout seconds]\n", argv[0]);
        return 1;
    }
    const std::string storage = argv[1];
    const std::string setupCode = argv[2];
    const int timeoutSeconds = argc > 3 ? std::atoi(argv[3]) : 120;

    auto protocol = std::dynamic_pointer_cast<MatterProtocol>(CreateMatterProtocol());
    if (!protocol) { std::puts("FAIL: no Matter backend in this build"); return 1; }

    std::mutex m;
    std::condition_variable cv;
    bool finished = false;
    bool joined = false;
    SmartHomeDeviceInfo device;
    std::string failure;

    protocol->SetOnDeviceJoin([&](const SmartHomeDeviceInfo& info) {
        std::lock_guard<std::mutex> lock(m);
        device = info;
        joined = finished = true;
        cv.notify_all();
    });
    protocol->SetOnError([&](int code, const std::string& message) {
        std::lock_guard<std::mutex> lock(m);
        if (finished) return;
        failure = "error " + std::to_string(code) + ": " + message;
        // Commissioning failures end the attempt; other errors are reported
        // and the wait goes on.
        if (code == -202 || code == -201) { finished = true; cv.notify_all(); }
        else std::printf("note: %s\n", failure.c_str());
    });

    protocol->LoadConfig(storage);
    if (!protocol->Initialize()) { std::puts("FAIL: the Matter stack did not start"); return 1; }
    std::puts("ok  the Matter controller is up");

    if (!protocol->CommissionWithCode(setupCode)) {
        std::puts("FAIL: commissioning did not start");
        protocol->Shutdown();
        return 1;
    }
    std::printf("..  commissioning %s (up to %d s)\n", setupCode.c_str(), timeoutSeconds);

    {
        std::unique_lock<std::mutex> lock(m);
        cv.wait_for(lock, std::chrono::seconds(timeoutSeconds), [&] { return finished; });
    }

    int status = 1;
    if (joined) {
        std::printf("ok  joined: %s (%s)\n", device.Name.c_str(), device.DeviceId.c_str());
        std::printf("    category: %s\n", DeviceCategoryToString(device.Category).c_str());
        const MatterNode node = protocol->GetMatterNode(device.DeviceId);
        std::printf("    endpoints:");
        for (uint32_t endpoint : node.Endpoints) std::printf(" %u", static_cast<unsigned>(endpoint));
        std::printf("\n    device types:");
        for (uint32_t type : node.DeviceTypes) std::printf(" 0x%04X", static_cast<unsigned>(type));
        std::printf("\n");
        status = device.Category == SmartHomeDeviceCategory::Unknown ? 2 : 0;
    } else {
        std::printf("FAIL: %s\n", failure.empty() ? "nothing joined in time" : failure.c_str());
    }
    protocol->Shutdown();
    return status;
}
