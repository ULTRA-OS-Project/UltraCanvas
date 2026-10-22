// core/SmartHomeStore.h
// smarthome.db: the home's own database, over UltraDatabase (SQLite).
//
// What it keeps: settings (the home's location), the rooms of the home,
// the names and rooms a person gave devices, scenes and automations with their actions, the event
// log, and sensor history. Protocol state the SDKs keep themselves (network
// keys, Matter fabrics) stays where each backend puts it.
//
// Parameters are always bound, never pasted into SQL; the schema is a list
// of versioned migrations (UltraDb_Migrate), so a later version adds a step
// rather than editing this one. Every call is guarded by one mutex: the
// manager calls in from the UI thread, the command worker and the backends.
//
// Internal to the engine. Applications use SmartHomeAPI::OpenStore.
// Version: 0.2.0 - rooms (migration 2)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasSmartHome.h"

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace UltraCanvas {
namespace SmartHome {

struct SmartHomeDeviceOverride {
    std::string Name;     // empty: the backend's
    std::string Room;     // a room name, as version 1 kept it; read once and turned into RoomId
    std::string RoomId;   // empty: the backend's room; kNoRoomOverride: in no room
};

// A device the person took out of every room (so the backend's room no
// longer applies either).
inline constexpr const char* kNoRoomOverride = "-";

class SmartHomeStore {
public:
    SmartHomeStore();
    ~SmartHomeStore();

    bool Open(const std::string& path);
    void Close();
    bool IsOpen() const;
    std::string LastError() const;
    bool ExportTo(const std::string& path);

    // ===== SETTINGS =====
    bool SetSetting(const std::string& key, const std::string& value);
    std::optional<std::string> GetSetting(const std::string& key);

    // ===== DEVICES =====
    bool SetDeviceName(const std::string& deviceId, const std::string& name);
    bool SetDeviceRoom(const std::string& deviceId, const std::string& room);
    bool SetDeviceRoomId(const std::string& deviceId, const std::string& roomId);
    std::map<std::string, SmartHomeDeviceOverride> LoadDeviceOverrides();

    // ===== ROOMS =====
    bool SaveRoom(const SmartHomeRoom& room);
    bool DeleteRoom(const std::string& roomId);
    std::vector<SmartHomeRoom> LoadRooms();

    // ===== SCENES AND AUTOMATIONS =====
    bool SaveScene(const SmartHomeScene& scene);
    bool DeleteScene(const std::string& sceneId);
    std::vector<SmartHomeScene> LoadScenes();
    bool SaveAutomation(const SmartHomeAutomation& automation);
    bool DeleteAutomation(const std::string& automationId);
    std::vector<SmartHomeAutomation> LoadAutomations();

    // ===== EVENT LOG =====
    bool AppendEvent(const SmartHomeEvent& event);
    std::vector<SmartHomeEvent> LoadEvents(int64_t since, size_t limit);

    // ===== SENSOR HISTORY =====
    bool AppendReading(const std::string& deviceId, const SmartHomeSensorReading& reading);
    std::vector<SmartHomeSensorReading> LoadReadings(const std::string& deviceId,
                                                     uint64_t startMs, uint64_t endMs);

private:
    bool Fail(const std::string& what, const std::string& message);
    bool SaveActions(const std::string& table, const std::string& key, const std::string& id,
                     const std::vector<SmartHomeCommand>& actions);
    std::vector<SmartHomeCommand> LoadActions(const std::string& table, const std::string& key,
                                              const std::string& id);

    mutable std::mutex mutex;
    std::string connection;     // the UltraDatabase connection name, unique per store
    bool open = false;
    std::string lastError;
    int appendsSincePrune = 0;
};

} // namespace SmartHome
} // namespace UltraCanvas
