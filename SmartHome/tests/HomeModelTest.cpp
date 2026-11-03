// SmartHome/tests/HomeModelTest.cpp
// The home's rooms (SmartHomeRoom and the room calls of SmartHomeAPI), on the
// simulated home, with and without smarthome.db.
//
//  1. The room names the simulator reports become the home's rooms, in the
//     order devices bring them, and every device carries its room's id.
//  2. CreateRoom refuses an empty name and a name taken (not minding case);
//     AssignDeviceToRoom moves a device, refuses a room that does not
//     exist, and "" takes the device out of every room.
//  3. Renaming a room renames it on its devices, and the backend's old name
//     does not bring the old room back on the next update.
//  4. Deleting a room leaves its devices in no room, and the backend's name
//     does not make the room again.
//  5. SetDeviceRoom by name makes the room when there is none.
//  6. OnHomeChange is called when the rooms change.
//  7. The rooms, and where each device is, survive closing the module and
//     opening the store again.
//  8. A version-1 store, which kept a device's room by name, opens with that
//     room made and the device in it.
//
// Author: UltraCanvas Framework
#include "UltraCanvasSmartHome.h"
#include "SimulatedProtocol.h"
#include "UltraCanvasPathUtf8.h"

#ifdef ULTRACANVAS_SMARTHOME_HAS_STORE
#include "UltraDatabase/UltraDatabase.h"
#endif

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>

using namespace UltraCanvas::SmartHome;
namespace fs = std::filesystem;

namespace {

int failures = 0;

void Check(bool condition, const char* what) {
    std::printf("%s  %s\n", condition ? "ok  " : "FAIL", what);
    if (!condition) ++failures;
}

std::shared_ptr<SimulatedProtocol> StartHome() {
    SimulatedHomeOptions options;
    options.realTime = false;
    options.randomEvents = false;
    auto sim = std::make_shared<SimulatedProtocol>(options);
    SMARTHOME_API.Initialize();
    SMARTHOME_API.RegisterProtocol(SmartHomeProtocolType::Simulated, sim);
    SMARTHOME_API.EnableProtocol(SmartHomeProtocolType::Simulated);
    return sim;
}

std::string IdOf(const std::string& name) {
    for (const auto& device : SMARTHOME_API.GetDevices())
        if (device.Name == name) return device.DeviceId;
    return "";
}

bool HasRoomNamed(const std::string& name) {
    for (const auto& room : SMARTHOME_API.GetRooms())
        if (room.Name == name) return true;
    return false;
}

std::string RoomIdNamed(const std::string& name) {
    for (const auto& room : SMARTHOME_API.GetRooms())
        if (room.Name == name) return room.RoomId;
    return "";
}

// Lets the backend report its devices again (a state change refreshes the
// device the way a radio report would).
void Nudge(const std::shared_ptr<SimulatedProtocol>& sim, const std::string& lightId) {
    SMARTHOME_API.SetSwitchState(lightId, true);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    sim->Advance(1.0);
    SMARTHOME_API.SetSwitchState(lightId, false);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    sim->Advance(1.0);
}

} // namespace

int main() {
    const fs::path dir = fs::temp_directory_path() / ("uc_smarthome_home_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    const std::string dbPath = UltraCanvas::PathToUtf8(dir / "smarthome.db");

    std::atomic<int> homeChanges{0};

    std::puts("-- rooms from the backend");
    auto sim = StartHome();
    SMARTHOME_API.SetOnHomeChange([&] { ++homeChanges; });
    const auto rooms = SMARTHOME_API.GetRooms();
    Check(rooms.size() == 8, "the simulated home's 8 room names are 8 rooms");
    Check(!rooms.empty() && rooms.front().Name == "Living room", "in the order devices bring them");
    bool allPlaced = true;
    for (const auto& device : SMARTHOME_API.GetDevices()) {
        const auto room = SMARTHOME_API.GetRoom(device.RoomId);
        allPlaced = allPlaced && !device.RoomId.empty() && room.Name == device.Room;
    }
    Check(allPlaced, "every device carries the id of the room it names");
    Check(RoomIdNamed("Living room") == "living-room", "ids are made from names");

    std::puts("-- create and assign");
    const std::string spots = IdOf("Kitchen spots");
    const std::string kettle = IdOf("Kettle");
    const std::string ceiling = IdOf("Ceiling light");
    homeChanges = 0;
    std::string pantry;
    Check(SMARTHOME_API.CreateRoom({"", "Pantry", "", "", 0}, &pantry) && pantry == "pantry", "CreateRoom gives an id");
    Check(homeChanges > 0, "OnHomeChange is called for a new room");
    Check(!SMARTHOME_API.CreateRoom({"", "pantry", "", "", 0}), "a name taken (not minding case) is refused");
    Check(!SMARTHOME_API.CreateRoom({"", "", "", "", 0}), "an empty name is refused");
    Check(SMARTHOME_API.AssignDeviceToRoom(spots, pantry), "AssignDeviceToRoom");
    Check(SMARTHOME_API.GetDevice(spots).Room == "Pantry" && SMARTHOME_API.GetDeviceRoom(spots) == pantry,
          "the device is in it at once");
    Check(!SMARTHOME_API.AssignDeviceToRoom(spots, "no-such-room"), "a room that does not exist is refused");
    Check(SMARTHOME_API.AssignDeviceToRoom(kettle, "") && SMARTHOME_API.GetDevice(kettle).Room.empty() &&
          SMARTHOME_API.GetDevice(kettle).RoomId.empty(), "\"\" takes a device out of every room");
    Nudge(sim, IdOf("Kitchen spots"));
    Check(SMARTHOME_API.GetDevice(kettle).Room.empty(), "and the backend's room does not bring it back");

    std::puts("-- rename");
    const std::string kitchen = RoomIdNamed("Kitchen");
    SmartHomeRoom renamed = SMARTHOME_API.GetRoom(kitchen);
    renamed.Name = "Cuisine";
    homeChanges = 0;
    Check(SMARTHOME_API.UpdateRoom(renamed), "UpdateRoom renames");
    Check(homeChanges > 0, "OnHomeChange is called for a rename");
    const std::string fridge = IdOf("Fridge");
    Check(SMARTHOME_API.GetDevice(fridge).Room == "Cuisine", "its devices show the new name");
    Nudge(sim, IdOf("Worktop light").empty() ? spots : IdOf("Worktop light"));
    for (int i = 0; i < 5; ++i) sim->Advance(1.0);
    Check(!HasRoomNamed("Kitchen"), "the backend's old name does not make the old room again");
    Check(SMARTHOME_API.GetDevice(fridge).Room == "Cuisine", "and the devices stay in the renamed room");
    SmartHomeRoom clash = renamed;
    clash.Name = "living ROOM";
    Check(!SMARTHOME_API.UpdateRoom(clash), "renaming to another room's name is refused");

    std::puts("-- delete");
    const std::string bedroom = RoomIdNamed("Bedroom");
    const std::string lamp = IdOf("Bedside lamp");
    Check(SMARTHOME_API.DeleteRoom(bedroom), "DeleteRoom");
    Check(SMARTHOME_API.GetDevice(lamp).Room.empty(), "its devices are in no room");
    Nudge(sim, lamp);
    Check(!HasRoomNamed("Bedroom") && SMARTHOME_API.GetDevice(lamp).Room.empty(),
          "and the backend's name does not make it again");
    Check(!SMARTHOME_API.DeleteRoom(bedroom), "deleting it twice is refused");

    std::puts("-- by name");
    Check(SMARTHOME_API.SetDeviceRoom(ceiling, "Den") && HasRoomNamed("Den") &&
          SMARTHOME_API.GetDevice(ceiling).Room == "Den", "SetDeviceRoom by name makes the room");
    Check(SMARTHOME_API.SetDeviceRoom(ceiling, "") && SMARTHOME_API.GetDevice(ceiling).Room == "Living room",
          "and \"\" puts it back in the backend's room");

#ifdef ULTRACANVAS_SMARTHOME_HAS_STORE
    std::puts("-- the store keeps the home");
    Check(SMARTHOME_API.OpenStore(dbPath), "OpenStore");
    Check(HasRoomNamed("Pantry") && HasRoomNamed("Cuisine"), "the rooms made before it opened are kept");
    SMARTHOME_API.AssignDeviceToRoom(spots, pantry);   // written through now
    SMARTHOME_API.Shutdown();
    Check(SMARTHOME_API.GetRooms().empty(), "after Shutdown no room is in memory");
    sim = StartHome();
    Check(SMARTHOME_API.OpenStore(dbPath), "the store opens again");
    Check(HasRoomNamed("Pantry") && HasRoomNamed("Cuisine") && !HasRoomNamed("Kitchen") && !HasRoomNamed("Bedroom"),
          "the rooms came back as they were left");
    Check(SMARTHOME_API.GetDevice(spots).Room == "Pantry", "a device's room came back");
    Check(SMARTHOME_API.GetDevice(fridge).Room == "Cuisine", "the renamed room kept its devices");
    Check(SMARTHOME_API.GetDevice(lamp).Room.empty(), "a deleted room's devices stay in no room");
    SMARTHOME_API.Shutdown();

    std::puts("-- a version-1 store");
    const std::string oldPath = UltraCanvas::PathToUtf8(dir / "v1.db");
    {
        UltraDbConnectionConfig config;
        config.name = "home-model-test-v1";
        config.driver = "sqlite";
        config.database = oldPath;
        UltraDb_RegisterConnection(config);
        // Version 1's device_override, as it was.
        const std::vector<UltraDbMigration> v1 = {
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
             "CREATE INDEX reading_device_time ON reading (device_id, time_ms);"}};
        UltraDb_Migrate(config.name, v1);
        UltraDb_Exec(config.name, "INSERT INTO device_override (device_id, name, room) VALUES (?, '', ?)",
                     {spots, std::string("Larder")});
        UltraDb_CloseConnection(config.name);
    }
    sim = StartHome();
    Check(SMARTHOME_API.OpenStore(oldPath), "a version-1 store opens");
    Check(HasRoomNamed("Larder") && SMARTHOME_API.GetDevice(spots).Room == "Larder" &&
          SMARTHOME_API.GetDevice(spots).RoomId == RoomIdNamed("Larder"),
          "the room it kept by name is a room, with the device in it");
    SMARTHOME_API.Shutdown();
    sim = StartHome();
    SMARTHOME_API.OpenStore(oldPath);
    Check(SMARTHOME_API.GetDevice(spots).Room == "Larder", "and stays so after the upgrade");
#else
    std::puts("-- (built without UltraDatabase: the store checks are skipped)");
#endif

    SMARTHOME_API.SetOnHomeChange(nullptr);
    SMARTHOME_API.Shutdown();
    std::error_code ec;
    fs::remove_all(dir, ec);
    std::printf("%s - the home's rooms\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
