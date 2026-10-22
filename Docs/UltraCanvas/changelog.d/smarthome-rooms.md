- **SmartHome: the home has rooms of its own.** `SmartHomeRoom` (id, name,
  icon, floor, sort order) and, on `SmartHomeAPI`, `GetRooms`, `GetRoom`,
  `CreateRoom`, `UpdateRoom`, `DeleteRoom`, `AssignDeviceToRoom`,
  `GetDeviceRoom` and `SetOnHomeChange`. A room name a backend reports
  becomes a room the first time a device brings it; `SmartHomeDeviceInfo`
  gains `RoomId` beside `Room`, which is now the name of the home's room the
  device is in - the person's choice over the backend's. Renaming a room
  renames it on its devices, and the backend's old name does not bring the
  old room back; deleting one leaves its devices in no room. `SetDeviceRoom`
  by name still works and makes the room when there is none.
  - smarthome.db migration 2 adds the `room` table and keeps a device's room
    by id; a version-1 store's room names become rooms the first time it
    opens. Rooms and choices made before the store opened are saved to it.
  - `SmartHomeHomeModelTest` covers it, with and without the store.
- **SmartHome: `SmartHomeAPI::WaitForCommands(timeoutMs)`** waits until every
  command queued so far has been sent - for a command-line tool that exits
  right after a command, and for tests.
