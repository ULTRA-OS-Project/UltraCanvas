// protocols/Simulated/SimulatedProtocol.h
// The simulated backend: a furnished demo home with no radio behind it.
//
// Every real backend needs hardware (a USB stick, a gateway, a Matter device)
// and none has met any yet (STATUS.md §2), so without this an application
// shows an empty home on every developer machine and in CI. This backend
// stands in for all of them: eight rooms and 30 devices of every category the
// widgets know, each answering commands and changing over time the way the
// real thing does - a thermostat warms its room towards the target, blinds
// take seconds to travel, a fridge's compressor cycles, a motion sensor fires
// now and then and clears again. "Pairing" finds a new device after a few
// seconds, so the add-device flow can be exercised too.
//
// It is always built (it needs nothing) and registered as a built-in factory,
// so an application needs no header but the facade's:
//
//     SMARTHOME_API.Initialize();
//     SMARTHOME_API.EnableProtocol(SmartHomeProtocolType::Simulated);
//     auto devices = SMARTHOME_API.GetDevices();      // the demo home
//
// Tests construct it directly with realTime = false and drive the clock with
// Advance(), so nothing they assert depends on the wall clock.
//
// Beyond the ordinary commands, every simulated device answers four that
// stand in for the outside world, so a demo can stage an event through
// SmartHomeAPI::SendCommand alone:
//   simulateTrigger / simulateClear   - a sensor detects / stops detecting
//                                       (motion, a door opening, water, smoke)
//   simulateOffline / simulateOnline  - the device drops off / comes back
//
// Its protocol type is SmartHomeProtocolType::Simulated, never a real one, so
// a fake device can never be mistaken for a paired Zigbee bulb.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "SmartHomeProtocolBase.h"

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace UltraCanvas {
namespace SmartHome {

class SimulatedDevice;   // SimulatedProtocol.cpp

struct SimulatedHomeOptions {
    bool sampleHome = true;        // furnish the demo home; false starts empty
    bool realTime = true;          // run the clock on a thread of its own;
                                   // false: the caller drives it with Advance()
    double pairingSeconds = 4.0;   // how long pairing takes to find a device
    bool randomEvents = true;      // motion now and then, appliances cycling
    uint32_t seed = 20261020;      // the same home behaves the same way
};

class SimulatedProtocol : public SmartHomeProtocolBase {
public:
    explicit SimulatedProtocol(SimulatedHomeOptions options = {});
    ~SimulatedProtocol() override;

    // ===== LIFECYCLE =====
    bool Initialize() override;
    void Shutdown() override;

    // ===== CAPABILITIES / HARDWARE =====
    std::vector<SmartHomeDeviceCategory> GetSupportedDeviceCategories() const override;
    bool IsHardwareAvailable() const override { return true; }
    std::string GetHardwareInfo() const override;
    std::vector<std::string> GetAvailableAdapters() const override { return {"sim0"}; }
    bool SelectAdapter(const std::string& adapterId) override { return adapterId == "sim0"; }

    // ===== NETWORK =====
    bool FormNetwork(const std::string& networkName = "") override;
    bool JoinNetwork(const std::string& networkId) override;
    bool LeaveNetwork() override;
    NetworkTopology GetTopology() const override;

    // ===== DISCOVERY / PAIRING =====
    bool StartDiscovery(int durationSeconds = 60) override;
    void StopDiscovery() override;
    bool StartPairing(int timeoutSeconds = 60) override;
    void StopPairing() override;

    // ===== DEVICES =====
    std::vector<std::shared_ptr<ISmartHomeDevice>> GetDevices() const override;
    std::shared_ptr<ISmartHomeDevice> GetDevice(const std::string& deviceId) const override;
    bool RemoveDevice(const std::string& deviceId) override;
    bool InterviewDevice(const std::string& deviceId) override;
    std::vector<SmartHomeDeviceInfo> GetPairedDevices() override;
    bool PairDevice(const std::string& deviceId,
                    const std::map<std::string, std::string>& params) override;
    bool UnpairDevice(const std::string& deviceId) override;
    bool GetDeviceState(const std::string& deviceId,
                        std::map<std::string, std::string>& state) override;
    bool SendCommand(const std::string& deviceId, const std::string& command,
                     const std::map<std::string, std::string>& params = {}) override;

    SmartHomeSecurityLevel GetSecurityLevel() const override { return SmartHomeSecurityLevel::Encrypted; }

    // ===== CALLBACKS =====
    // Guarded: the clock thread reports changes while the manager is still
    // wiring these up, and std::function is not safe to read while written.
    void SetOnStateChange(OnProtocolStateChange callback) override;
    void SetOnDeviceDiscover(OnProtocolDeviceDiscover callback) override;
    void SetOnDeviceJoin(OnProtocolDeviceJoin callback) override;
    void SetOnDeviceLeave(OnProtocolDeviceLeave callback) override;
    void SetOnDeviceUpdate(OnProtocolDeviceUpdate callback) override;
    void SetOnNetworkForm(OnProtocolNetworkForm callback) override;
    void SetOnError(OnProtocolError callback) override;

    // ===== CONFIGURATION =====
    // The demo home is rebuilt from code on every start; nothing to keep.
    bool LoadConfig(const std::string&) override { return true; }
    bool SaveConfig(const std::string&) override { return true; }
    std::map<std::string, std::string> GetDiagnostics() const override;

    // ===== SIMULATION =====
    // Moves the simulated world on by `seconds`: temperatures drift, blinds
    // travel, meters count, pending pairing completes. The clock thread calls
    // it twice a second when realTime is on; tests call it themselves.
    void Advance(double seconds);
    double GetSimulatedTime() const;               // seconds since construction
    std::vector<std::string> GetRooms() const;     // in the order they were furnished

private:
    void FurnishSampleHome();
    void AddDevice(const std::shared_ptr<SimulatedDevice>& device);
    std::shared_ptr<SimulatedDevice> FindDevice(const std::string& deviceId) const;
    std::shared_ptr<SimulatedDevice> MakeCatalogueDevice(int index);
    void ClockThread();
    void UpdateNetworkInfo();

    // Fire a callback with no lock of ours held: the manager answers an
    // update by reading the device straight back.
    void NotifyUpdate(const std::string& deviceId);
    void NotifyJoin(const SmartHomeDeviceInfo& info);
    void NotifyLeave(const std::string& deviceId);
    void NotifyDiscover(const SmartHomeDeviceInfo& info);

    SimulatedHomeOptions options;

    mutable std::mutex simMutex;                          // everything below to the clock
    std::vector<std::shared_ptr<SimulatedDevice>> devices;
    std::vector<std::shared_ptr<SimulatedDevice>> discoverable;  // found, not yet paired
    std::vector<std::string> rooms;
    std::mt19937 random;
    double simulatedTime = 0.0;
    double pairingUntil = 0.0;          // pairing window closes at this time
    double pairingFindsAt = 0.0;        // the next joining device arrives at this time
    double discoveryUntil = 0.0;
    double nextMotionEvent = 0.0;
    int catalogueNext = 0;              // which catalogue device pairing finds next
    int nextSerial = 1;

    mutable std::mutex callbackMutex;

    std::thread clock;
    std::mutex clockMutex;
    std::condition_variable clockWake;
    bool clockRunning = false;          // guarded by clockMutex
};

// The factory RegisterBuiltinProtocols() registers for
// SmartHomeProtocolType::Simulated: the demo home, on the real-time clock.
std::shared_ptr<ISmartHomeProtocol> CreateSimulatedProtocol();

} // namespace SmartHome
} // namespace UltraCanvas
