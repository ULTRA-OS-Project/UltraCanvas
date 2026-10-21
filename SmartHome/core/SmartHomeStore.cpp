// core/SmartHomeStore.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "SmartHomeStore.h"

#ifdef ULTRACANVAS_SMARTHOME_HAS_STORE

#include "DataFormats/UltraCanvasJSON.h"
#include "UltraDatabase/UltraDatabase.h"

#include <atomic>
#include <cstdint>
#include <ctime>

namespace UltraCanvas {
namespace SmartHome {

namespace {

// Each store gets a connection name of its own, so two stores (a test's
// and an application's) never share one.
std::string NextConnectionName() {
    static std::atomic<int> counter{0};
    return "smarthome-store-" + std::to_string(++counter);
}

// SQLite integers are signed: "until the end of time" (UINT64_MAX) must not
// wrap to -1.
int64_t ClampToInt64(uint64_t value) {
    return value > static_cast<uint64_t>(INT64_MAX) ? INT64_MAX : static_cast<int64_t>(value);
}

// How long the log and the history are kept.
constexpr int64_t kEventLogRows = 20000;
constexpr int64_t kReadingDays = 90;

const std::vector<UltraDbMigration>& Migrations() {
    static const std::vector<UltraDbMigration> steps = {
        {1, "smarthome v1",
         "CREATE TABLE settings (key TEXT PRIMARY KEY, value TEXT NOT NULL);"
         "CREATE TABLE device_override (device_id TEXT PRIMARY KEY, name TEXT NOT NULL DEFAULT '',"
         "  room TEXT NOT NULL DEFAULT '');"
         "CREATE TABLE scene (scene_id TEXT PRIMARY KEY, name TEXT NOT NULL, icon TEXT NOT NULL DEFAULT '');"
         "CREATE TABLE scene_action (scene_id TEXT NOT NULL, position INTEGER NOT NULL,"
         "  device_id TEXT NOT NULL, command TEXT NOT NULL, params TEXT NOT NULL,"
         "  PRIMARY KEY (scene_id, position));"
         "CREATE TABLE automation (automation_id TEXT PRIMARY KEY, name TEXT NOT NULL,"
         "  enabled INTEGER NOT NULL, trigger_type TEXT NOT NULL, trigger_config TEXT NOT NULL,"
         "  conditions TEXT NOT NULL);"
         "CREATE TABLE automation_action (automation_id TEXT NOT NULL, position INTEGER NOT NULL,"
         "  device_id TEXT NOT NULL, command TEXT NOT NULL, params TEXT NOT NULL,"
         "  PRIMARY KEY (automation_id, position));"
         "CREATE TABLE event_log (id INTEGER PRIMARY KEY AUTOINCREMENT, time INTEGER NOT NULL,"
         "  kind TEXT NOT NULL, device_id TEXT NOT NULL, subject_id TEXT NOT NULL, text TEXT NOT NULL);"
         "CREATE INDEX event_log_time ON event_log (time);"
         "CREATE TABLE reading (device_id TEXT NOT NULL, sensor_type INTEGER NOT NULL,"
         "  value REAL NOT NULL, unit TEXT NOT NULL, triggered INTEGER NOT NULL, time_ms INTEGER NOT NULL);"
         "CREATE INDEX reading_device_time ON reading (device_id, time_ms);"},
    };
    return steps;
}

std::string ParamsToJson(const std::map<std::string, std::string>& params) {
    JSONValue o = JSONValue::MakeObject();
    for (const auto& [key, value] : params) o.Set(key, value);
    return JSON::Serialize(o);
}

std::map<std::string, std::string> ParamsFromJson(const std::string& text) {
    std::map<std::string, std::string> params;
    const JSONValue o = JSON::Parse(text);
    for (const auto& member : o.GetMembers()) params[member.first] = member.second.GetString();
    return params;
}

std::string StringsToJson(const std::vector<std::string>& strings) {
    JSONValue a = JSONValue::MakeArray();
    for (const auto& s : strings) a.Append(s);
    return JSON::Serialize(a);
}

std::vector<std::string> StringsFromJson(const std::string& text) {
    std::vector<std::string> strings;
    const JSONValue a = JSON::Parse(text);
    for (const auto& e : a.GetElements()) strings.push_back(e.GetString());
    return strings;
}

} // namespace

SmartHomeStore::SmartHomeStore() : connection(NextConnectionName()) {}

SmartHomeStore::~SmartHomeStore() {
    Close();
}

bool SmartHomeStore::Fail(const std::string& what, const std::string& message) {
    lastError = what + ": " + message;
    return false;
}

bool SmartHomeStore::Open(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex);
    if (open) {
        UltraDb_CloseConnection(connection);
        open = false;
    }
    UltraDbConnectionConfig config;
    config.name = connection;
    config.driver = "sqlite";
    config.database = path;    // UTF-8; SQLite opens UTF-8 names on every platform
    if (UltraDbResult r = UltraDb_RegisterConnection(config); !r) return Fail("Opening " + path, r.message);
    if (UltraDbResult r = UltraDb_Migrate(connection, Migrations()); !r) {
        UltraDb_CloseConnection(connection);
        return Fail("Preparing " + path, r.message);
    }
    open = true;
    lastError.clear();
    // Keep what is kept bounded: old history goes on every open.
    UltraDb_Exec(connection, "DELETE FROM reading WHERE time_ms < ?",
                 {static_cast<int64_t>(std::time(nullptr) - kReadingDays * 86400) * 1000});
    return true;
}

void SmartHomeStore::Close() {
    std::lock_guard<std::mutex> lock(mutex);
    if (!open) return;
    UltraDb_CloseConnection(connection);
    open = false;
}

bool SmartHomeStore::IsOpen() const {
    std::lock_guard<std::mutex> lock(mutex);
    return open;
}

std::string SmartHomeStore::LastError() const {
    std::lock_guard<std::mutex> lock(mutex);
    return lastError;
}

bool SmartHomeStore::ExportTo(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!open) return Fail("Export", "no store is open");
    // VACUUM INTO writes a consistent, compacted copy while the store stays
    // in use; it refuses a file that already exists.
    if (UltraDbResult r = UltraDb_Exec(connection, "VACUUM INTO ?", {path}); !r) return Fail("Export to " + path, r.message);
    return true;
}

// ===== SETTINGS =====

bool SmartHomeStore::SetSetting(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!open) return false;
    UltraDbResult r = UltraDb_Exec(connection,
        "INSERT INTO settings (key, value) VALUES (?, ?) ON CONFLICT(key) DO UPDATE SET value = excluded.value",
        {key, value});
    return r ? true : Fail("Saving setting " + key, r.message);
}

std::optional<std::string> SmartHomeStore::GetSetting(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!open) return std::nullopt;
    UltraDbResultSet rows;
    if (!UltraDb_Query(connection, "SELECT value FROM settings WHERE key = ?", {key}, rows) || rows.Empty()) {
        return std::nullopt;
    }
    return rows.Row(0)["value"].AsString();
}

// ===== DEVICES =====

bool SmartHomeStore::SetDeviceName(const std::string& deviceId, const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!open) return false;
    UltraDbResult r = UltraDb_Exec(connection,
        "INSERT INTO device_override (device_id, name) VALUES (?, ?)"
        " ON CONFLICT(device_id) DO UPDATE SET name = excluded.name",
        {deviceId, name});
    return r ? true : Fail("Saving the name of " + deviceId, r.message);
}

bool SmartHomeStore::SetDeviceRoom(const std::string& deviceId, const std::string& room) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!open) return false;
    UltraDbResult r = UltraDb_Exec(connection,
        "INSERT INTO device_override (device_id, room) VALUES (?, ?)"
        " ON CONFLICT(device_id) DO UPDATE SET room = excluded.room",
        {deviceId, room});
    return r ? true : Fail("Saving the room of " + deviceId, r.message);
}

std::map<std::string, SmartHomeDeviceOverride> SmartHomeStore::LoadDeviceOverrides() {
    std::lock_guard<std::mutex> lock(mutex);
    std::map<std::string, SmartHomeDeviceOverride> overrides;
    if (!open) return overrides;
    UltraDbResultSet rows;
    if (!UltraDb_Query(connection, "SELECT device_id, name, room FROM device_override", rows)) return overrides;
    for (const auto& row : rows) {
        overrides[row["device_id"].AsString()] = {row["name"].AsString(), row["room"].AsString()};
    }
    return overrides;
}

// ===== SCENES AND AUTOMATIONS =====

bool SmartHomeStore::SaveActions(const std::string& table, const std::string& key, const std::string& id,
                                 const std::vector<SmartHomeCommand>& actions) {
    // Called with the mutex held, inside the caller's transaction.
    if (!UltraDb_Exec(connection, "DELETE FROM " + table + " WHERE " + key + " = ?", {id})) return false;
    int position = 0;
    for (const SmartHomeCommand& action : actions) {
        if (!UltraDb_Exec(connection,
                          "INSERT INTO " + table + " (" + key + ", position, device_id, command, params)"
                          " VALUES (?, ?, ?, ?, ?)",
                          {id, position++, action.DeviceId, action.Command, ParamsToJson(action.Parameters)})) {
            return false;
        }
    }
    return true;
}

std::vector<SmartHomeCommand> SmartHomeStore::LoadActions(const std::string& table, const std::string& key,
                                                          const std::string& id) {
    std::vector<SmartHomeCommand> actions;
    UltraDbResultSet rows;
    if (!UltraDb_Query(connection,
                       "SELECT device_id, command, params FROM " + table + " WHERE " + key + " = ? ORDER BY position",
                       {id}, rows)) {
        return actions;
    }
    for (const auto& row : rows) {
        actions.push_back({row["device_id"].AsString(), row["command"].AsString(),
                           ParamsFromJson(row["params"].AsString())});
    }
    return actions;
}

bool SmartHomeStore::SaveScene(const SmartHomeScene& scene) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!open) return false;
    // The scene and its actions change together or not at all.
    UltraDb_Exec(connection, "BEGIN");
    bool ok = static_cast<bool>(UltraDb_Exec(connection,
        "INSERT INTO scene (scene_id, name, icon) VALUES (?, ?, ?)"
        " ON CONFLICT(scene_id) DO UPDATE SET name = excluded.name, icon = excluded.icon",
        {scene.SceneId, scene.Name, scene.Icon}));
    ok = ok && SaveActions("scene_action", "scene_id", scene.SceneId, scene.Actions);
    UltraDb_Exec(connection, ok ? "COMMIT" : "ROLLBACK");
    return ok ? true : Fail("Saving scene " + scene.SceneId, "the database refused it");
}

bool SmartHomeStore::DeleteScene(const std::string& sceneId) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!open) return false;
    UltraDb_Exec(connection, "DELETE FROM scene_action WHERE scene_id = ?", {sceneId});
    return static_cast<bool>(UltraDb_Exec(connection, "DELETE FROM scene WHERE scene_id = ?", {sceneId}));
}

std::vector<SmartHomeScene> SmartHomeStore::LoadScenes() {
    std::lock_guard<std::mutex> lock(mutex);
    std::vector<SmartHomeScene> scenes;
    if (!open) return scenes;
    UltraDbResultSet rows;
    if (!UltraDb_Query(connection, "SELECT scene_id, name, icon FROM scene ORDER BY rowid", rows)) return scenes;
    for (const auto& row : rows) {
        SmartHomeScene scene;
        scene.SceneId = row["scene_id"].AsString();
        scene.Name = row["name"].AsString();
        scene.Icon = row["icon"].AsString();
        scene.Actions = LoadActions("scene_action", "scene_id", scene.SceneId);
        scenes.push_back(std::move(scene));
    }
    return scenes;
}

bool SmartHomeStore::SaveAutomation(const SmartHomeAutomation& a) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!open) return false;
    UltraDb_Exec(connection, "BEGIN");
    bool ok = static_cast<bool>(UltraDb_Exec(connection,
        "INSERT INTO automation (automation_id, name, enabled, trigger_type, trigger_config, conditions)"
        " VALUES (?, ?, ?, ?, ?, ?)"
        " ON CONFLICT(automation_id) DO UPDATE SET name = excluded.name, enabled = excluded.enabled,"
        " trigger_type = excluded.trigger_type, trigger_config = excluded.trigger_config,"
        " conditions = excluded.conditions",
        {a.AutomationId, a.Name, a.Enabled, a.TriggerType, a.TriggerConfig, StringsToJson(a.Conditions)}));
    ok = ok && SaveActions("automation_action", "automation_id", a.AutomationId, a.Actions);
    UltraDb_Exec(connection, ok ? "COMMIT" : "ROLLBACK");
    return ok ? true : Fail("Saving automation " + a.AutomationId, "the database refused it");
}

bool SmartHomeStore::DeleteAutomation(const std::string& automationId) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!open) return false;
    UltraDb_Exec(connection, "DELETE FROM automation_action WHERE automation_id = ?", {automationId});
    return static_cast<bool>(UltraDb_Exec(connection, "DELETE FROM automation WHERE automation_id = ?", {automationId}));
}

std::vector<SmartHomeAutomation> SmartHomeStore::LoadAutomations() {
    std::lock_guard<std::mutex> lock(mutex);
    std::vector<SmartHomeAutomation> automations;
    if (!open) return automations;
    UltraDbResultSet rows;
    if (!UltraDb_Query(connection,
                       "SELECT automation_id, name, enabled, trigger_type, trigger_config, conditions"
                       " FROM automation ORDER BY rowid", rows)) {
        return automations;
    }
    for (const auto& row : rows) {
        SmartHomeAutomation a;
        a.AutomationId = row["automation_id"].AsString();
        a.Name = row["name"].AsString();
        a.Enabled = row["enabled"].AsBool();
        a.TriggerType = row["trigger_type"].AsString();
        a.TriggerConfig = row["trigger_config"].AsString();
        a.Conditions = StringsFromJson(row["conditions"].AsString());
        a.Actions = LoadActions("automation_action", "automation_id", a.AutomationId);
        automations.push_back(std::move(a));
    }
    return automations;
}

// ===== EVENT LOG =====

bool SmartHomeStore::AppendEvent(const SmartHomeEvent& event) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!open) return false;
    UltraDbResult r = UltraDb_Exec(connection,
        "INSERT INTO event_log (time, kind, device_id, subject_id, text) VALUES (?, ?, ?, ?, ?)",
        {event.Time, event.Kind, event.DeviceId, event.SubjectId, event.Text});
    if (!r) return Fail("Logging an event", r.message);
    // Every few hundred events, drop the oldest beyond the limit.
    if (++appendsSincePrune >= 500) {
        appendsSincePrune = 0;
        UltraDb_Exec(connection, "DELETE FROM event_log WHERE id <= (SELECT MAX(id) FROM event_log) - ?",
                     {kEventLogRows});
    }
    return true;
}

std::vector<SmartHomeEvent> SmartHomeStore::LoadEvents(int64_t since, size_t limit) {
    std::lock_guard<std::mutex> lock(mutex);
    std::vector<SmartHomeEvent> events;
    if (!open) return events;
    UltraDbResultSet rows;
    if (!UltraDb_Query(connection,
                       "SELECT time, kind, device_id, subject_id, text FROM event_log WHERE time >= ?"
                       " ORDER BY id DESC LIMIT ?",
                       {since, static_cast<int64_t>(limit)}, rows)) {
        return events;
    }
    for (const auto& row : rows) {
        events.push_back({row["time"].AsInt64(), row["kind"].AsString(), row["device_id"].AsString(),
                          row["subject_id"].AsString(), row["text"].AsString()});
    }
    return events;
}

// ===== SENSOR HISTORY =====

bool SmartHomeStore::AppendReading(const std::string& deviceId, const SmartHomeSensorReading& reading) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!open) return false;
    UltraDbResult r = UltraDb_Exec(connection,
        "INSERT INTO reading (device_id, sensor_type, value, unit, triggered, time_ms) VALUES (?, ?, ?, ?, ?, ?)",
        {deviceId, static_cast<int>(reading.Type), static_cast<double>(reading.Value), reading.Unit,
         reading.Triggered, static_cast<int64_t>(reading.Timestamp)});
    return r ? true : Fail("Recording a reading", r.message);
}

std::vector<SmartHomeSensorReading> SmartHomeStore::LoadReadings(const std::string& deviceId,
                                                                 uint64_t startMs, uint64_t endMs) {
    std::lock_guard<std::mutex> lock(mutex);
    std::vector<SmartHomeSensorReading> readings;
    if (!open) return readings;
    UltraDbResultSet rows;
    if (!UltraDb_Query(connection,
                       "SELECT sensor_type, value, unit, triggered, time_ms FROM reading"
                       " WHERE device_id = ? AND time_ms >= ? AND time_ms <= ? ORDER BY time_ms",
                       {deviceId, ClampToInt64(startMs), ClampToInt64(endMs)}, rows)) {
        return readings;
    }
    for (const auto& row : rows) {
        SmartHomeSensorReading reading;
        reading.Type = static_cast<SmartHomeSensorType>(row["sensor_type"].AsInt());
        reading.Value = static_cast<float>(row["value"].AsDouble());
        reading.Unit = row["unit"].AsString();
        reading.Triggered = row["triggered"].AsBool();
        reading.Timestamp = static_cast<uint64_t>(row["time_ms"].AsInt64());
        readings.push_back(std::move(reading));
    }
    return readings;
}

} // namespace SmartHome
} // namespace UltraCanvas

#else   // built without UltraDatabase (no SQLite): every call says so

namespace UltraCanvas {
namespace SmartHome {

SmartHomeStore::SmartHomeStore() = default;
SmartHomeStore::~SmartHomeStore() = default;
bool SmartHomeStore::Fail(const std::string& what, const std::string& message) {
    lastError = what + ": " + message;
    return false;
}
bool SmartHomeStore::Open(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex);
    return Fail("Opening " + path, "this build has no UltraDatabase (SQLite was not found)");
}
void SmartHomeStore::Close() {}
bool SmartHomeStore::IsOpen() const { return false; }
std::string SmartHomeStore::LastError() const { std::lock_guard<std::mutex> lock(mutex); return lastError; }
bool SmartHomeStore::ExportTo(const std::string&) { return false; }
bool SmartHomeStore::SetSetting(const std::string&, const std::string&) { return false; }
std::optional<std::string> SmartHomeStore::GetSetting(const std::string&) { return std::nullopt; }
bool SmartHomeStore::SetDeviceName(const std::string&, const std::string&) { return false; }
bool SmartHomeStore::SetDeviceRoom(const std::string&, const std::string&) { return false; }
std::map<std::string, SmartHomeDeviceOverride> SmartHomeStore::LoadDeviceOverrides() { return {}; }
bool SmartHomeStore::SaveScene(const SmartHomeScene&) { return false; }
bool SmartHomeStore::DeleteScene(const std::string&) { return false; }
std::vector<SmartHomeScene> SmartHomeStore::LoadScenes() { return {}; }
bool SmartHomeStore::SaveAutomation(const SmartHomeAutomation&) { return false; }
bool SmartHomeStore::DeleteAutomation(const std::string&) { return false; }
std::vector<SmartHomeAutomation> SmartHomeStore::LoadAutomations() { return {}; }
bool SmartHomeStore::AppendEvent(const SmartHomeEvent&) { return false; }
std::vector<SmartHomeEvent> SmartHomeStore::LoadEvents(int64_t, size_t) { return {}; }
bool SmartHomeStore::AppendReading(const std::string&, const SmartHomeSensorReading&) { return false; }
std::vector<SmartHomeSensorReading> SmartHomeStore::LoadReadings(const std::string&, uint64_t, uint64_t) { return {}; }
bool SmartHomeStore::SaveActions(const std::string&, const std::string&, const std::string&,
                                 const std::vector<SmartHomeCommand>&) { return false; }
std::vector<SmartHomeCommand> SmartHomeStore::LoadActions(const std::string&, const std::string&,
                                                          const std::string&) { return {}; }

} // namespace SmartHome
} // namespace UltraCanvas

#endif
