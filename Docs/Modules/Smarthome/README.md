## Contents

- [UltraCanvas Smart Home Module - Feature Overview](#ultracanvas-smart-home-module---feature-overview)
  - [Dashboard Panel (`SmartHomePanel`)](#dashboard-panel-smarthomepanel)
  - [Device Cards (`SmartHomeDeviceCard`)](#device-cards-smarthomedevicecard)
  - [Device-Specific Controls](#device-specific-controls)
  - [Dialogs, editors and views](#dialogs-editors-and-views)
  - [Protocol Support](#protocol-support)
  - [Store and automations](#store-and-automations)
  - [Application](#application)

## UltraCanvas Smart Home Module - Feature Overview

> **Status: partly implemented.** The engine, the facade (`SmartHomeAPI`), the
> store, the automation engine, the home's rooms, a simulated demo home and the
> sixteen widgets (`SmartHome/ui`) exist and are tested; the radio backends
> build but have met no hardware yet. Where each part stands:
> [`SmartHome/STATUS.md`](../../../SmartHome/STATUS.md). The demo application's
> Smart Home entry shows the dashboard panel on the simulated home.

Every widget is a container of UltraCanvas elements - switches, sliders,
buttons, spinners, segmented controls, list views, a colour picker, gauges,
line charts, a node diagram - so focus, keyboard, hover and theming are the
framework's. Only two pictures are drawn by the module itself: a blind in its
window and the scheduler's week grid. The element catalogue
([`UltraCanvasUIElements.md`](../../UltraCanvas/UltraCanvasUIElements.md#module-widgets--smart-home))
lists which elements each widget is made of.

### Dashboard Panel (`SmartHomePanel`)

-   **Six views** - device grid, rooms (the home's rooms, a heading and a row of cards each), list, scenes, automations (double-click turns one on or off) and the network as a topology graph, picked from the toolbar
-   **Search and filters** - a search field in the toolbar; `FilterByCategory`, `FilterByRoom`, `FilterByProtocol` for a host
-   **Scene Cards** - activate a scene with one press
-   **Pairing banner** - progress and Cancel while a radio is searching
-   **Live state** - the module reports changes on its own threads; the panel applies them on the UI thread twice a second

### Device Cards (`SmartHomeDeviceCard`)

Every device has an icon: the line icons in `media/icons/smarthome/`, chosen
by `SmartHomeDeviceIconName` (`UltraCanvasSmartHomeIcons.h`) from its
category, its kind of reading, or - for a plug - what its name says is
plugged in.

-   Visual cards showing device name, room, status, and quick toggle switch
-   Color-coded icons by category (lights=amber, locks=green, sensors=blue, etc.)
-   Long-press for device settings, tap for quick control
-   Battery indicator for wireless devices

### Device-Specific Controls

| Widget | Features |
|---|---|
| **Light Control** | On/off switch, brightness and colour-temperature sliders, a colour picker (hue and intensity bars), preset colours as a swatch bar |
| **Thermostat Control** | The target in a ring gauge coloured by what it is doing, the room's temperature and humidity, a spinner for the target (the wheel steps half a degree), a segmented control for the modes |
| **Lock Control** | A Lock / Unlock button (a jammed lock refuses), who used it last, a battery bar, the activity newest first in a list |
| **Blind Control** | A picture of the blind, Open / Stop / Close, position and tilt sliders, a button per preset |
| **Sensor Display** | The reading with its unit, coloured by the alert thresholds; the recent history as a line chart with a floor under its axis (2 °C, 10 %, 20 W, 50 lx), so a steady room draws a flat line |

### Dialogs, editors and views

| Dialog | Purpose |
|---|---|
| **Device Dialog** | The device's name, category, protocol, state, maker, model and firmware, the control that fits it, Rename and Remove |
| **Pairing Wizard** | A button per radio, then a search with a progress bar and Cancel until a device joins ("Added ...") or the time runs out (what went wrong, Back) |
| **Scene Editor** | A name, the actions in a list, a device dropdown to add one, Remove, Save |
| **Automation Editor** | A name, what starts it (WHEN), what must hold (AND IF), what it does (THEN), a switch for enabled, Save |
| **Network Topology** | Each device a card with its icon, name and room, linked to a router of its radio (in its own room where there is one); force, radial or tree layout with no overlapping cards; filter by radio |
| **Energy Monitor** | Power now, the history as a line chart (power, energy or cost), today, average, peak and cost |
| **Scheduler** | Timed events on a day or week grid or in a list; turn one off, remove it; a press on empty time asks the host to make one |
| **Group Control** | All on / All off, brightness and colour temperature for the group, its devices in a list, add and remove |

### Protocol Support

Ready for integration with: **Matter**, **Thread**, **Zigbee**, **Z-Wave**, **WiFi**, **Bluetooth**

The **simulated demo home** (`SmartHome/protocols/Simulated`) needs no
radio: 30 devices in 8 rooms answering commands and changing over time, for
development, tests and demos. Enabling the `Simulated` protocol type
through `SmartHomeAPI` brings it up.

### Store and automations

The home's own database, **smarthome.db** (UltraDatabase / SQLite): the
home's rooms (`SmartHomeRoom`, `GetRooms` and friends), scenes, automations,
the names and rooms given to devices, the home's location, the event log and
sensor history. **Automations run** in the module: device,
time, sunrise and sunset triggers, time, weekday and device conditions, and
device commands, notifications, delays and scenes as actions. The trigger and
condition forms are documented on `SmartHomeAutomation` in
`SmartHome/include/UltraCanvasSmartHome.h`.

### Application

**SmartHome Center** (`Apps/SmartHomeCenter`) is the home control window
built on this module: Home with its device tiles, Rooms, Devices, a page per
device with its history, Scenes, Energy, Activity and Settings, and a
command line (`--list`, `--set`, `--scene`, `--history`, `--log`). It opens
the demo home when no radio is enabled.
