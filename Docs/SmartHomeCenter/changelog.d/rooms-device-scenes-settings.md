- **Rooms, a page per device, Scenes, Settings and a first-run guide** -
  proposal phase 1. The navigation gains Rooms, Scenes and Settings.
  - *Rooms*: the home's rooms with their device counts, the chosen room's
    devices; add, rename and delete rooms, move a device to another room.
    Rooms are the module's now (`SmartHomeAPI::GetRooms`), so they and
    every move are kept in the home's data file.
  - *Device*: opens from the inspector ("Open device page"), a double-click
    in Devices or Rooms: its controls, every detail the backend gives, the
    last 24 hours of its readings as a line chart, and its name, room and
    removal.
  - *Scenes*: a card per scene to run, edit or delete; an editor with the
    actions in order and a row to add one (a device, what to do, a value);
    *Capture current state* starts a scene from the devices as they are.
    Home's scene buttons come from the home's scenes.
  - *Settings*: where the home is, with today's sunrise and sunset; the data
    file and *Export a backup*; which radios this build carries.
  - *Getting started*: welcome, location, rooms to tick from a suggested
    list, radios - on the first start of a home (`--first-run` shows it any
    time).
- **The command line acts on the home.** `--set <device> <attribute>=<value>`
  (on, brightness, colorTemp, target, mode, locked, position), `--scene
  <name|id>`, `--history <device> [--since 24h] [--csv]`, `--log [--since
  1h]` and `--list --csv`; each starts the home, does one thing and exits.
  `SmartHomeCenterCliTest` checks them on the demo home with no display, in
  CI.
- `--page` takes rooms, scenes, settings and device (with `--select`).
