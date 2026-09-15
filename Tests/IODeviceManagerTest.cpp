// Tests/IODeviceManagerTest.cpp
// The IODeviceManager foundation: the device lifecycle state machine and the
// registry's merge semantics.
//
// The merge is the part worth guarding. Two things went wrong in the design
// this layer replaces, and both are asserted against here: a category served
// by two backends must show the devices of *both* (the earlier shape gave
// each platform one EnumerateCameras(), so a webcam backend and a DSLR
// backend on the same platform collided and only one ever ran), and
// re-enumerating must hand back the *same* device object for a device that is
// still present, so an open session survives a rescan.
//
// It uses a fake backend rather than hardware, so it runs the same everywhere
// — including on a CI runner with no scanner, camera or printer attached.
// Version: 1.0.0
// Last Modified: 2026-09-14
// Author: UltraCanvas Framework

#include "IODeviceManager/UltraCanvasIODeviceManager.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

// ===== FAKE DEVICE =====

// Stands in for a real backend device. Counts its own connect/disconnect
// calls so the test can assert the base class talks to the backend exactly
// once per transition.
class FakeDevice : public IODevice {
public:
    FakeDevice(const std::string& id, IODeviceCategory category,
               const std::string& backend, bool failConnect = false)
        : IODevice(MakeInfo(id, category, backend))
        , shouldFailConnect(failConnect) {}

    ~FakeDevice() override { destroyed = true; }

    int connectCalls = 0;
    int disconnectCalls = 0;
    bool shouldFailConnect = false;
    bool destroyed = false;

protected:
    IODeviceResult DoConnect() override {
        ++connectCalls;
        if (shouldFailConnect) {
            return IODeviceResult::Error(IODeviceResultCode::ConnectionFailed,
                                         "fake backend refused");
        }
        return IODeviceResult::Ok();
    }

    void DoDisconnect() override { ++disconnectCalls; }

private:
    static IODeviceInfo MakeInfo(const std::string& id,
                                 IODeviceCategory category,
                                 const std::string& backend) {
        IODeviceInfo info;
        info.deviceId = id;
        info.name = "Fake " + id;
        info.category = category;
        info.backend = backend;
        info.transport = IODeviceTransport::Virtual;
        return info;
    }
};

std::shared_ptr<FakeDevice> MakeFake(const std::string& id,
                                     IODeviceCategory category,
                                     const std::string& backend) {
    return std::make_shared<FakeDevice>(id, category, backend);
}

// ===== FAKE WATCHER =====

// Stands in for udev. A real watcher reports from its own thread, so this one
// does too: the deadlock this design has to avoid only appears when the
// callback arrives from somewhere other than the caller's thread.
class FakeWatcher : public IDeviceWatcher {
public:
    ~FakeWatcher() override { Stop(); }

    std::string GetName() const override { return "Fake"; }

    IODeviceResult Start(CategoryChangedCallback onCategoryChanged) override {
        if (failToStart) {
            return IODeviceResult::Error(IODeviceResultCode::BackendUnavailable,
                                         "the fake watcher refused to start");
        }
        callback = std::move(onCategoryChanged);
        running = true;
        ++startCalls;
        return IODeviceResult::Ok();
    }

    void Stop() override {
        running = false;
        if (reporter.joinable()) {
            reporter.join();
        }
        ++stopCalls;
    }

    // Reports a change from another thread, as a real watcher does.
    void ReportFromAnotherThread(IODeviceCategory category) {
        if (reporter.joinable()) {
            reporter.join();
        }
        reporter = std::thread([this, category] {
            if (running && callback) {
                callback(category);
            }
        });
    }

    bool failToStart = false;
    std::atomic<int> startCalls{0};
    std::atomic<int> stopCalls{0};

private:
    std::atomic<bool> running{false};
    CategoryChangedCallback callback;
    std::thread reporter;
};

// ===== TESTS =====

void TestDeviceLifecycle() {
    std::cout << "\nDevice lifecycle\n";

    auto device = MakeFake("cam-1", IODeviceCategory::Camera, "Fake");

    Check(device->GetState() == IODeviceState::Disconnected,
          "a fresh device starts Disconnected");
    Check(!device->IsConnected(), "a fresh device is not connected");

    IODeviceResult result = device->Connect();
    Check(static_cast<bool>(result), "Connect() succeeds");
    Check(device->IsConnected(), "the device reports connected");
    Check(device->GetState() == IODeviceState::Ready,
          "a connected device is Ready");
    Check(result.deviceId == "cam-1", "the result carries the device id");
    Check(device->connectCalls == 1, "the backend saw one DoConnect()");

    // Connecting twice must not reach the backend again.
    device->Connect();
    Check(device->connectCalls == 1, "Connect() on a connected device is a no-op");

    device->Disconnect();
    Check(!device->IsConnected(), "Disconnect() clears the connected flag");
    Check(device->GetState() == IODeviceState::Disconnected,
          "a disconnected device is Disconnected");
    Check(device->disconnectCalls == 1, "the backend saw one DoDisconnect()");
}

void TestFailedConnect() {
    std::cout << "\nFailed connect\n";

    auto device = std::make_shared<FakeDevice>("cam-bad", IODeviceCategory::Camera,
                                               "Fake", /*failConnect=*/true);

    IODeviceResult result = device->Connect();
    Check(!static_cast<bool>(result), "Connect() reports the backend failure");
    Check(result.code == IODeviceResultCode::ConnectionFailed,
          "the failure keeps the backend's result code");
    Check(!device->IsConnected(), "a failed Connect() leaves the device closed");
    Check(device->GetState() == IODeviceState::Error, "the device is in Error");
    Check(device->GetLastDeviceError().code == IODeviceResultCode::ConnectionFailed,
          "the failure is recorded as the last error");

    // A half-open session still has handles to release, so teardown must run
    // even though the device never reached connected.
    device->Disconnect();
    Check(device->disconnectCalls == 1,
          "Disconnect() after a failed Connect() still reaches the backend");
}

void TestRegistration() {
    std::cout << "\nManual registration\n";

    IODeviceManager& manager = IODeviceManager::GetInstance();
    manager.Shutdown();

    auto device = MakeFake("scan-1", IODeviceCategory::Scanner, "Fake");
    Check(static_cast<bool>(manager.RegisterDevice(device)),
          "RegisterDevice() accepts a device");
    Check(manager.GetDeviceCount() == 1, "the registry holds one device");

    Check(!manager.RegisterDevice(MakeFake("scan-1", IODeviceCategory::Scanner, "Fake")),
          "a duplicate device id is rejected");
    Check(!manager.RegisterDevice(nullptr), "a null device is rejected");

    Check(manager.GetDeviceById("scan-1") != nullptr, "lookup by id finds it");
    Check(manager.GetDeviceById("nope") == nullptr, "lookup of an unknown id is null");
    Check(manager.GetDeviceCount(IODeviceCategory::Scanner) == 1,
          "it counts under its own category");
    Check(manager.GetDeviceCount(IODeviceCategory::Printer) == 0,
          "it does not count under another");

    manager.UnregisterDevice("scan-1");
    Check(manager.GetDeviceCount() == 0, "UnregisterDevice() drops it");
    manager.UnregisterDevice("scan-1");  // must not throw or corrupt state
    Check(manager.GetDeviceCount() == 0, "unregistering twice is harmless");

    manager.Shutdown();
}

void TestTwoBackendsOneCategory() {
    std::cout << "\nTwo backends serving one category\n";

    IODeviceManager& manager = IODeviceManager::GetInstance();
    manager.Shutdown();

    // A webcam backend and a DSLR backend, both Camera — the case the
    // previous design could not express.
    manager.RegisterEnumerator(IODeviceCategory::Camera, "FakeWebcam", [] {
        return std::vector<IODevicePtr>{
            MakeFake("webcam-0", IODeviceCategory::Camera, "FakeWebcam")};
    });
    manager.RegisterEnumerator(IODeviceCategory::Camera, "FakeDSLR", [] {
        return std::vector<IODevicePtr>{
            MakeFake("dslr-0", IODeviceCategory::Camera, "FakeDSLR")};
    });

    Check(manager.GetRegisteredBackends(IODeviceCategory::Camera).size() == 2,
          "both backends are registered for Camera");

    IODeviceResult result = manager.EnumerateDevices(IODeviceCategory::Camera);
    Check(static_cast<bool>(result), "enumeration succeeds");
    Check(manager.GetDeviceCount(IODeviceCategory::Camera) == 2,
          "devices from BOTH backends are registered");
    Check(manager.GetDeviceById("webcam-0") != nullptr, "the webcam is present");
    Check(manager.GetDeviceById("dslr-0") != nullptr, "the DSLR is present");

    // A category nobody serves must say so rather than silently finding zero.
    IODeviceResult none = manager.EnumerateDevices(IODeviceCategory::Printer);
    Check(!static_cast<bool>(none),
          "enumerating a category with no backend is an error");
    Check(none.code == IODeviceResultCode::BackendUnavailable,
          "and the code says the backend is unavailable");

    manager.Shutdown();
}

void TestEnumerationMerge() {
    std::cout << "\nRe-enumeration keeps live devices\n";

    IODeviceManager& manager = IODeviceManager::GetInstance();
    manager.Shutdown();

    // A backend whose device list the test can change between scans.
    static std::vector<std::string> present;
    present = {"cam-a", "cam-b"};

    manager.RegisterEnumerator(IODeviceCategory::Camera, "FakeHotplug", [] {
        std::vector<IODevicePtr> found;
        for (const auto& id : present) {
            found.push_back(MakeFake(id, IODeviceCategory::Camera, "FakeHotplug"));
        }
        return found;
    });

    manager.EnumerateDevices(IODeviceCategory::Camera);
    Check(manager.GetDeviceCount(IODeviceCategory::Camera) == 2,
          "the first scan finds both cameras");

    // Open a session, then rescan. The object must survive, or the caller's
    // handle would silently start pointing at a dead device.
    IODevicePtr camA = manager.GetDeviceById("cam-a");
    Check(camA != nullptr && static_cast<bool>(camA->Connect()),
          "a device can be connected");

    manager.EnumerateDevices(IODeviceCategory::Camera);
    Check(manager.GetDeviceById("cam-a") == camA,
          "re-enumeration keeps the SAME object for a device still present");
    Check(camA->IsConnected(), "and its open session survives the rescan");

    // Unplug one, plug another.
    present = {"cam-a", "cam-c"};
    std::vector<std::pair<IODeviceChange, std::string>> seen;
    manager.SetDeviceChangeCallback(
        [&](IODeviceChange change, const IODeviceInfo& info) {
            seen.emplace_back(change, info.deviceId);
        });

    manager.EnumerateDevices(IODeviceCategory::Camera);
    Check(manager.GetDeviceCount(IODeviceCategory::Camera) == 2,
          "the rescan still reports two cameras");
    Check(manager.GetDeviceById("cam-b") == nullptr, "the removed camera is gone");
    Check(manager.GetDeviceById("cam-c") != nullptr, "the new camera is registered");
    Check(camA->IsConnected(), "the untouched device is still connected");

    bool sawRemove = false, sawAdd = false;
    for (const auto& event : seen) {
        if (event.first == IODeviceChange::Removed && event.second == "cam-b") sawRemove = true;
        if (event.first == IODeviceChange::Added && event.second == "cam-c") sawAdd = true;
    }
    Check(sawRemove, "the change callback reported the removal");
    Check(sawAdd, "the change callback reported the addition");

    manager.Shutdown();
}

void TestBrokenBackendIsContained() {
    std::cout << "\nA throwing backend does not hide the others\n";

    IODeviceManager& manager = IODeviceManager::GetInstance();
    manager.Shutdown();

    manager.RegisterEnumerator(IODeviceCategory::Camera, "Throws",
                               []() -> std::vector<IODevicePtr> {
                                   throw std::runtime_error("backend exploded");
                               });
    manager.RegisterEnumerator(IODeviceCategory::Camera, "Works", [] {
        return std::vector<IODevicePtr>{
            MakeFake("good-0", IODeviceCategory::Camera, "Works")};
    });

    IODeviceResult result = manager.EnumerateDevices(IODeviceCategory::Camera);
    Check(static_cast<bool>(result), "enumeration still succeeds overall");
    Check(manager.GetDeviceCount(IODeviceCategory::Camera) == 1,
          "the working backend's device is registered");
    Check(result.message.find("Throws") != std::string::npos,
          "the result names the backend that failed");

    manager.Shutdown();
}

void TestHotPlugMonitoring() {
    std::cout << "\nHot-plug monitoring\n";

    IODeviceManager& manager = IODeviceManager::GetInstance();
    manager.Shutdown();

    // A backend whose device list the test changes, as a hot-plug would.
    static std::vector<std::string> present;
    present = {"cam-1"};
    manager.RegisterEnumerator(IODeviceCategory::Camera, "FakeHotplug", [] {
        std::vector<IODevicePtr> found;
        for (const auto& id : present) {
            found.push_back(MakeFake(id, IODeviceCategory::Camera, "FakeHotplug"));
        }
        return found;
    });

    auto watcher = std::make_shared<FakeWatcher>();
    Check(static_cast<bool>(manager.SetDeviceWatcher(watcher)),
          "a watcher can be substituted");

    std::vector<std::pair<IODeviceChange, std::string>> seen;
    std::mutex seenMutex;
    manager.SetDeviceChangeCallback(
        [&](IODeviceChange change, const IODeviceInfo& info) {
            std::lock_guard<std::mutex> lock(seenMutex);
            seen.emplace_back(change, info.deviceId);
        });

    Check(static_cast<bool>(manager.StartMonitoring()), "monitoring starts");
    Check(manager.IsMonitoring(), "and the manager reports it");
    Check(watcher->startCalls == 1, "the watcher was started once");
    Check(static_cast<bool>(manager.StartMonitoring()),
          "starting twice succeeds");
    Check(watcher->startCalls == 1, "without starting the watcher again");

    // The point of the whole feature: a device appears and the change
    // callback fires without anyone calling EnumerateDevices().
    manager.EnumerateDevices(IODeviceCategory::Camera);
    {
        std::lock_guard<std::mutex> lock(seenMutex);
        seen.clear();
    }

    present = {"cam-1", "cam-2"};
    watcher->ReportFromAnotherThread(IODeviceCategory::Camera);

    bool sawAdd = false;
    for (int i = 0; i < 200 && !sawAdd; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        std::lock_guard<std::mutex> lock(seenMutex);
        for (const auto& event : seen) {
            if (event.first == IODeviceChange::Added && event.second == "cam-2") {
                sawAdd = true;
            }
        }
    }
    Check(sawAdd, "a device arriving fires Added WITHOUT an explicit rescan");
    Check(manager.GetDeviceCount(IODeviceCategory::Camera) == 2,
          "and the registry holds it");

    // Removal travels the same path.
    present = {"cam-1"};
    watcher->ReportFromAnotherThread(IODeviceCategory::Camera);
    bool sawRemove = false;
    for (int i = 0; i < 200 && !sawRemove; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        std::lock_guard<std::mutex> lock(seenMutex);
        for (const auto& event : seen) {
            if (event.first == IODeviceChange::Removed && event.second == "cam-2") {
                sawRemove = true;
            }
        }
    }
    Check(sawRemove, "a device leaving fires Removed the same way");

    // StopMonitoring() joins a thread that is calling back into the manager.
    // If it held either lock while joining, this would hang rather than fail.
    manager.StopMonitoring();
    Check(!manager.IsMonitoring(), "monitoring stops");
    Check(watcher->stopCalls >= 1, "and the watcher was stopped");
    manager.StopMonitoring();
    Check(true, "stopping twice is harmless");

    manager.Shutdown();
}

void TestMonitoringEdgeCases() {
    std::cout << "\nMonitoring edge cases\n";

    IODeviceManager& manager = IODeviceManager::GetInstance();
    manager.Shutdown();

    // A watcher that cannot start must leave monitoring off, not half on.
    auto broken = std::make_shared<FakeWatcher>();
    broken->failToStart = true;
    manager.SetDeviceWatcher(broken);
    Check(!static_cast<bool>(manager.StartMonitoring()),
          "a watcher that fails to start reports the failure");
    Check(!manager.IsMonitoring(), "and monitoring is left off");

    // The watcher cannot be swapped underneath a running one.
    auto working = std::make_shared<FakeWatcher>();
    manager.SetDeviceWatcher(working);
    manager.StartMonitoring();
    IODeviceResult refused = manager.SetDeviceWatcher(std::make_shared<FakeWatcher>());
    Check(!static_cast<bool>(refused), "the watcher cannot be replaced while running");
    Check(refused.code == IODeviceResultCode::InvalidState, "with InvalidState");

    // Shutdown() has to stop monitoring, or the watcher thread outlives the
    // registry it reports into.
    manager.Shutdown();
    Check(!manager.IsMonitoring(), "Shutdown() stops monitoring");
    Check(working->stopCalls >= 1, "and stops the watcher");
}

void TestShutdownDisconnects() {
    std::cout << "\nShutdown releases devices\n";

    IODeviceManager& manager = IODeviceManager::GetInstance();
    manager.Shutdown();

    auto device = MakeFake("cam-shut", IODeviceCategory::Camera, "Fake");
    manager.RegisterDevice(device);
    device->Connect();
    Check(device->IsConnected(), "the device is connected before shutdown");

    manager.Shutdown();
    Check(!device->IsConnected(), "Shutdown() disconnected it");
    Check(device->disconnectCalls == 1, "the backend saw the teardown");
    Check(manager.GetDeviceCount() == 0, "the registry is empty");
    Check(manager.GetRegisteredBackends(IODeviceCategory::Camera).empty(),
          "the enumerators are cleared");

    manager.Shutdown();  // must be safe twice
    Check(true, "Shutdown() twice is harmless");
}

}  // namespace

int main() {
    std::cout << "IODeviceManager foundation tests\n";
    std::cout << "================================\n";

    TestDeviceLifecycle();
    TestFailedConnect();
    TestRegistration();
    TestTwoBackendsOneCategory();
    TestEnumerationMerge();
    TestBrokenBackendIsContained();
    TestHotPlugMonitoring();
    TestMonitoringEdgeCases();
    TestShutdownDisconnects();

    std::cout << "\n";
    if (g_failures == 0) {
        std::cout << "All IODeviceManager tests passed.\n";
        return 0;
    }
    std::cout << g_failures << " IODeviceManager test(s) FAILED.\n";
    return 1;
}
