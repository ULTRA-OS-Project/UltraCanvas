#### 2026-10-22 *0.1.1*
- **A Network page.** The navigation gains *Network*: the module's map of
  the radios (`SmartHomeNetworkTopology`), every device a card with its
  icon, name and room, linked to a router of its radio - in its own room
  where there is one. Force, radial and tree layouts, a filter by radio,
  no card on another; zoomed far out the devices show as their icons, named
  on hover. Double-click a device, or choose it and *Open device*, for its
  page. `--page network` opens on it.
- **Devices look as they do in the module's widgets.** The icon is the
  module's choice (`SmartHomeDeviceIconName`): a plug called "TV" shows a TV,
  the fridge a fridge, the kettle a kettle, a garden gate a gate; it sits
  white on a round disc of the device's category colour on the tiles, in the
  inspector and on the device's page.
- The app links the module's widget library (`SmartHomeUI`) for the two.
- The app hears the module through a listener (`SmartHomeAPI::AddListener`)
  rather than its single callbacks, so a `SmartHomePanel` in the same
  process no longer takes them over.
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

#### 2026-10-20 *0.1.0*
- **First version: SmartHome Center, the home control window, on the demo
  home.** A navigation column (Home, Devices, Energy, Activity) beside the
  page. *Home* shows the inside temperature, the lights that are on, the
  locks and the power drawn across the top, four scenes (Good morning,
  Evening, Away, Good night), then every room with a tile per device - a
  switch on the tile for whatever switches, the reading for whatever
  measures - and the selected device's controls and details on the right:
  brightness and colour temperature for a light, target and mode for a
  thermostat, position for a blind, lock and unlock. *Devices* is the whole
  home as a table, *Energy* the metered plugs live, *Activity* what
  happened and when. A leak or smoke alarm puts a red banner over Home.
  The Energy page is the framework's bar chart run sideways, one row per
  plug drawing power, biggest at the top, on an axis marked at whole watts;
  idle plugs are left out and counted under the title, and when every plug
  is idle the chart says "Nothing is drawing power" instead of bare axes.
- **The demo home.** With no radio enabled the window opens the simulated
  home of the SmartHome module (30 devices in 8 rooms), says so in a panel
  under the menu and in every device's details, and offers three events to
  stage: motion in the hallway, the patio door opening, water under the
  sink. *Add device* pairs a new simulated device after a few seconds.
- **The home is kept, and automations run.** The window opens the
  module's store (`smarthome.db` in the per-user data directory under
  `ULTRA/SmartHome`, `smarthome-demo.db` for the demo home, so simulated
  devices never mix with a real home): scenes and automations survive a
  restart. The demo home starts with five automations - a water leak switches
  the kettle and towel rail off and says so, smoke turns the hall light on,
  motion lights the hall for two minutes, a patio door open for ten minutes
  is reported, and the Evening scene runs fifteen minutes before sunset. What
  they do shows in Activity with why they ran.
- **Command line.** `--list` prints the home room by room and exits;
  `--page` and `--select` open the window on a page and a device.
