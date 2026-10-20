# SmartHome Center

The home control window on the SmartHome module: every room and device,
the scenes, the power drawn and what happened, in one window. It talks to
the home through `SmartHomeAPI` alone, so whatever radio the module drives -
Matter, Thread, Zigbee, Z-Wave, KNX - looks the same here. With no radio
enabled it opens the module's **simulated demo home** (30 devices in 8
rooms) and says so.

The design behind it:
[`Docs/Research/SmartHomeCenterDesignProposal.md`](../../Docs/Research/SmartHomeCenterDesignProposal.md).
The module, its backends and what each has been verified against:
[`SmartHome/STATUS.md`](../../SmartHome/STATUS.md).
Changelog and version: [`Docs/SmartHomeCenter/CHANGELOG.md`](../../Docs/SmartHomeCenter/CHANGELOG.md).

## The window

![Home: figures, scenes, rooms of device tiles, and the selected device](../../Docs/SmartHomeCenter/images/home.png)

![Energy: the metered plugs, live](../../Docs/SmartHomeCenter/images/energy.png)

| Page | What it shows |
|---|---|
| **Home** | Inside temperature, lights on, locks, power now; the four scenes; every room with a tile per device (a switch on the tile for whatever switches, the reading for whatever measures); the selected device's controls and details on the right. A leak or smoke alarm puts a red banner on top. |
| **Devices** | The whole home as a table: room, type, state, battery, power, protocol, last seen. Double-click a row to open the device on Home. |
| **Energy** | The metered plugs, live: what each draws against the biggest draw, and what each has metered so far. |
| **Activity** | What happened since the window opened, newest first: switches, locks, sensors, scenes, pairing. |

Tiles take the colour of what the device is doing: amber for light and
power, coral for heating, green for an open lock or blind, blue for a sensor
reporting, red for an alarm or a device that stopped answering.

## Layout

| Path | What is in it |
|---|---|
| `ui/SmartHomeCenterModel.*` | What a device and the home look like to a person, from `SmartHomeAPI` alone: the state line, the icon, active / alert, the summary figures, the `--list` text. No UI types. |
| `ui/SmartHomeCenterPaths.*` | Where the store lives: the per-user data directory, `ULTRA/SmartHome/smarthome.db` (`smarthome-demo.db` for the demo home) |
| `ui/SmartHomeCenterWindow.*` | The window: navigation, header, the four pages, the tiles, the inspector, the table models, the demo panel, the scenes the demo home starts with |
| `main.cpp` | Bootstrap, starting the radios (or the demo home), the command line |
| `SmartHomeCenter.desktop` | The freedesktop shortcut; `make install` places it with the app icon (`media/appicon/SmartHomeCenter.png` / `.svg`) |

Icons are `media/icons/smarthome/*.svg`: white line drawings the window
tints per state, so one file serves every colour.

Nothing is painted by hand: tiles and figures are containers of labels,
images, switches and badges; the inspector uses the framework's slider,
segmented control and buttons; the tables are `UltraCanvasListView`s; the
Energy page is an `UltraCanvasBarChartElement` in its horizontal orientation.

SmartHomeAPI reports changes on the thread that caused them. The window's
callbacks only note which device changed; a UI timer reads those devices
back four times a second and updates their tiles in place.

## Command line

```
SmartHomeCenter                       # the window
SmartHomeCenter --demo                # the window, on the demo home even if a radio works
SmartHomeCenter --list                # the home, room by room, as text
SmartHomeCenter --page energy         # open on a page: home, devices, energy, activity
SmartHomeCenter --select "Front door" # open with a device selected
SmartHomeCenter --trigger "Under-sink leak sensor"   # demo home: stage a sensor event
```

## Not yet

The store and automations run in the module (`SmartHome/STATUS.md`). Still
to come in the window: editing rooms, scenes and automations, history
charts, the pairing wizard's per-protocol steps, a dark theme, and the hub
service that keeps automations running with the window closed.
