# SmartHome Center — the home control application for ULTRA OS (Proposal)

**Date:** 2026-10-20
**Status:** Phases 0 and 1 done (2026-10-21). Phase 0: the simulated
backend (§4.7), smarthome.db (§4.4), sensor history (§4.5), the automation
engine (§4.6) and the home model's rooms (§4.3) are in `SmartHome/`.
Phase 1: `Apps/SmartHomeCenter` 0.2.0 has Home, Rooms, Devices, a page per
device, Scenes, Energy, Activity, Settings and the first-run guide;
`--list`, `--set`, `--scene`, `--history` and `--log`, checked in CI by
`SmartHomeCenterCliTest`; the demo application's Smart Home entry opens
the panel on the simulator. Phase 2 has begun: the §4.8 widget port is
done - every `SmartHomeUI` widget is a container of framework elements,
`check_ui_reuse` scans `SmartHome/ui`, the widgets have rows in the element
catalogue, and `SmartHomeWidgetTest` drives them through a real window in
CI. SmartHome Center 0.3.0 has the phase's Network page (the module's
topology widget) and draws its devices with the module's icons. Automations,
backup and restore, notifications and kiosk mode are still to come.
**Scope:** a new application, `Apps/SmartHomeCenter`, on top of the existing
`SmartHome` engine and `SmartHomeUI` widget library. This document also
covers what those two libraries still need before an application can stand
on them: persistence, a home model with rooms, a working automation engine,
sensor history, and a simulated backend that works without hardware.

Like the other documents in this directory
([`UltraCalendarDesignProposal.md`](UltraCalendarDesignProposal.md),
[`UltraMessageDesignProposal.md`](UltraMessageDesignProposal.md)), it asks
which parts belong in the module and which in the application. Every claim
about the repository was checked against the tree on the date above.
[`SmartHome/STATUS.md`](../../SmartHome/STATUS.md) is the companion record of
what the module already does.

---

## 1. Recommendation

**Build SmartHome Center as a thin application that hosts the sixteen
widgets `SmartHomeUI` already has. Before the application, close the four
gaps in the engine that would make it a demo rather than a product:**

1. nothing is saved;
2. rooms exist only inside the panel widget;
3. automations are stored but never evaluated;
4. sensor history returns nothing.

**Ship a simulated backend from day one, so the application can be used,
tested and demonstrated with no radio attached. Plan for the engine to run
as a background hub service, because automations that stop when a window
closes are not automations.**

| Decision | Reason |
|---|---|
| An application, not a demo screen (§4.1) | The widgets are built and tested (`tests/WidgetTest.cpp`, 36 assertions), but nothing outside the test links `SmartHomeUI`. The demo app still lists the module as `Planned` (`Apps/DemoApp/UltraCanvasDemo.cpp:2096`). The ULTRA OS front page already sells "ULTRA OS Smart Home" (`UltraCanvas/OS/WASM/demo/ultraos-frontpage.cpp:286`). |
| Engine work first (§4.3–§4.6) | `SmartHomeManager::SaveConfiguration` / `LoadConfiguration` are `// TODO` (`core/UltraCanvasSmartHomeManager.cpp:759-765`). `EvaluateAutomations` is `// TODO` (`:668`). `SmartHomeAPI::GetSensorHistory` is `return {}` (`:984`). Rooms live in `SmartHomePanel::deviceRooms` (`ui/UltraCanvasSmartHomePanel.h:378`), so they vanish with the widget. An application over this state forgets the home on every restart. |
| A simulated backend, `SimulatedProtocol` (§4.7) | Matter, Thread, Zigbee and Z-Wave all compile, but none has met a device (STATUS.md §2). The SDK backends are off by default. KNX needs a KNXnet/IP gateway. Without a simulator, the application shows an empty grid on every developer machine and in CI. `SmartHomeAPI::RegisterProtocol` already accepts a backend from outside `core/`, so the simulator needs no engine changes. |
| One process now, hub service later (§4.2) | A Zigbee or Z-Wave stick is a serial port, and only one process can own it. Automations must run when no window is open. Phase 1 runs the engine inside the GUI; phase 3 moves it to `SmartHomeCenter --service` and makes the GUI a client over UltraMessage, which is already the per-user channel of the stack. |
| Port the widgets' painted controls to real elements first (§4.8) | The widgets paint their own sliders, toggles and buttons (`DrawSlider` in `ui/UltraCanvasSmartHomeDeviceControl.cpp:46`, about 120 `DrawText` / `FillRoundedRect` calls across `ui/*.cpp`). `scripts/check_ui_reuse.py` does not scan `SmartHome/` (its `SEARCH_ROOTS` are `UltraCanvas/*` and `Apps`), so nothing flagged this. The application must not inherit controls with no keyboard focus, theming or DPI scaling. |
| Local-first, no vendor cloud (§4.9) | The front page's promise is "a save OS that doesn't share your data with the rest of the world". Every protocol in the module is local: Matter, Thread, Zigbee, Z-Wave and KNXnet/IP all work on the LAN or the radio. Remote access is a later, opt-in phase through the ULTRA account (§10). |
| Notifications through UltraMessage (§4.6) | An application posts `system.notification` (`UltraCanvas/include/UltraMessage/UltraMessage.h:210`) and the desktop draws it. Smoke, water and "door left unlocked" alerts need no notification code of their own. |

**Do not** add a proprietary cloud relay, a device-vendor login, or a second
dashboard toolkit. The widgets exist; the work is under them and around them.

---

## 2. What the application is

A single window where a household:

- sees every device, grouped by room, with its live state;
- switches, dims, locks, opens and sets the temperature;
- adds a device: picks a protocol, puts the device in pairing mode or scans
  its Matter QR code, and names it and assigns it a room;
- builds **scenes** ("Evening": living room at 30 %, blinds down, heating to
  20 °C) and runs them with one click;
- builds **automations** ("at sunset, if somebody is home, run Evening";
  "if the water sensor triggers, notify me and close the valve");
- sees **energy** use per device and over time;
- sees the **network**: which radios are up, which devices route through
  which, and which are offline or have a weak signal;
- keeps the configuration: backs it up to a file and restores it on a new
  machine.

### What it is not

- **Not a protocol tool.** Channel changes, network-key rotation and
  Zigbee interviews belong to an *Advanced* page, hidden by default. They
  are not the main window.
- **Not a camera NVR.** `SmartHomeDeviceCategory::Camera` exists, but
  recording video streams is a different product. Phase 1 shows a camera
  device's state. A live view (`UltraCanvasMediaViewer`) is a later phase.
- **Not a cloud service.** See §4.9.

---

## 3. What the tree already has

| Piece | Where | State |
|---|---|---|
| Public facade `SmartHomeAPI` (devices, pairing, control, scenes, automations, networks, callbacks) | `SmartHome/include/UltraCanvasSmartHome.h` | Built; `tests/FacadeTest.cpp` passes |
| Engine `SmartHomeManager` (registries, command queue, 3 worker threads) | `SmartHome/core/` | Built; persistence, automation evaluation and history are TODO |
| Backends | `SmartHome/protocols/{KNX,ZWave,Zigbee,Thread,Matter}` | Each compiles and links; all off by default; none tested on hardware |
| Dashboard `SmartHomePanel` with grid/list/room/scene/automation/network modes, filter, quick actions | `SmartHome/ui/UltraCanvasSmartHomePanel.*` | Built and tested; `SmartHomeRoomView` is forward-declared but has no definition |
| Device cards and scene cards | `ui/UltraCanvasSmartHomeDeviceCard.cpp` | Built and tested |
| Controls for light, thermostat, lock, blind; sensor display | `ui/UltraCanvasSmartHomeDeviceControl.*` | Built and tested; paints its own sliders and buttons |
| Device dialog and pairing wizard | `ui/UltraCanvasSmartHomeDialogs.cpp` | Built and tested |
| Scene editor and automation editor (trigger round-trips through UltraCanvasJSON) | `ui/UltraCanvasSmartHomeEditors.cpp` | Built and tested |
| Network topology, energy monitor, scheduler, group control | `ui/UltraCanvasSmartHomeAdvanced.cpp` | Built and tested |
| QR scanning from a file or a camera frame | `UltraCanvas/include/Plugins/QRCode/UltraCanvasQRCode.h` (`ScanQRCodeFile`, camera overload) | Used by `Apps/UltraAuthenticator/ScanAccountDialog.cpp`, the model for a Matter `MT:` code scan |
| Charts (line, bar, area) for history and energy | `Plugins/Charts/UltraCanvasSpecificChartElements.h` | In the framework |
| Database (SQLite driver, parameter binding, `UltraDbResult`) | UltraDatabase | Used by UltraFIBU |
| Per-user message channel, notifications | `UltraCanvas/include/UltraMessage/` | Phase 1 implemented |
| mDNS / DNS-SD | `UltraCanvas/Plugins/UltraNet/mdns/MdnsPlugin.cpp` | IPv4 only (STATUS.md §3); Matter and Thread need AAAA |

---

## 4. Design decisions

### 4.1 One application, standing on the module

The split follows UltraNetMonitor: the module does the work and the
application is a window plus a command line.

- `SmartHome` (engine) and `SmartHomeUI` (widgets) stay where they are.
  Anything a second program could want goes there, not in the app: the
  home model, persistence, the automation engine, history, the simulator.
  A desktop widget, UltraCalendar's device schedules
  (`UltraCalendarDesignProposal.md` §4.1) and a voice assistant are all
  plausible second programs.
- `Apps/SmartHomeCenter` holds the window, navigation, settings, first-run
  flow and the command line.

The rule `STATUS.md` already states still applies: **if the application has
to include `core/`, the facade has a hole.** The application includes
`UltraCanvasSmartHome.h` and the `ui/` headers only.

### 4.2 Where the engine runs: in-process now, a hub service later

**Phase 1–2: in-process.** `SmartHomeAPI::Initialize()` runs in the
application. Automations run while the window is open, and the window can
minimise to the tray instead of quitting. This is enough to build and test
everything else.

**Phase 3: `SmartHomeCenter --service`.** The same binary, with no window,
owns the radios, the database and the automation thread. The GUI becomes
one client among several. The channel between them is UltraMessage, which
already provides request/reply, topic subscriptions and a per-user broker:

| Topic / request | Direction | Carries |
|---|---|---|
| `smarthome.device.changed` | service → clients | device id, new state (JSON) |
| `smarthome.command` | client → service, request/reply | `SmartHomeCommand` as JSON; reply is the result |
| `smarthome.scene.activate`, `smarthome.automation.set` | client → service | ids |
| `smarthome.pairing.progress` | service → clients | progress, status text |
| `system.notification` | service → desktop | alerts (§4.6) |

To make the switch invisible to the widgets, the facade gets a second
implementation. `SmartHomeAPI` keeps its signature. Its `Impl` either
calls `SmartHomeManager` directly (in-process) or forwards over UltraMessage
(client). The widgets never know which. This is the one structural change
to the module the proposal asks for, and it can wait until phase 3.

On ULTRA OS the service is started by the session. On Linux it is a
systemd user unit that the installer drops next to the `.desktop` file. On
Windows and macOS it is a login item. Owning the serial port needs the
`dialout` group (or a udev rule) on Linux; the first-run flow checks this
and says what to do, as the Z-Wave and Zigbee backends cannot open the port
otherwise.

### 4.3 The home model belongs to the module

Today `SmartHomePanel` holds rooms in its own `std::map` and
`SmartHomeDeviceInfo` has no room. The module gains:

```cpp
struct SmartHomeRoom {
    std::string RoomId;
    std::string Name;
    std::string Icon;
    std::string Floor;          // "Ground floor", "" when the home has one
    int SortOrder = 0;
};

// SmartHomeAPI
std::vector<SmartHomeRoom> GetRooms() const;
bool CreateRoom(const SmartHomeRoom& room);
bool UpdateRoom(const SmartHomeRoom& room);
bool DeleteRoom(const std::string& roomId);          // devices fall back to "no room"
bool AssignDeviceToRoom(const std::string& deviceId, const std::string& roomId);
std::string GetDeviceRoom(const std::string& deviceId) const;
void SetOnHomeChange(OnHomeChange callback);
```

`SmartHomePanel::AddRoom` / `AssignDeviceToRoom` / `GetRooms` keep their
names and become pass-throughs, so `WidgetTest` keeps passing.
`SmartHomeRoomView`, which is forward-declared and undefined, is either
written as a real element or deleted. The panel's `RoomView` mode can
group cards itself.

The user's names and choices also belong in the module, kept separately
from what the backend reports: a device's display name, its room, whether
it is hidden, and its icon. A rename is not a radio command.
`RenameDevice` writes the local name. Pushing it to the device (where the
protocol supports it, such as Matter's `NodeLabel`) is a separate,
optional step.

### 4.4 Persistence: `smarthome.db` through UltraDatabase

The `SaveConfiguration` / `LoadConfiguration` TODOs are filled with an
UltraDatabase store, not a JSON file. History (§4.5) needs indexed queries
by time range anyway, and one store is simpler than two.

| Table | Holds |
|---|---|
| `schema_version` | migrations, as UltraFIBU does |
| `device` | id, protocol, backend address, local name, room, icon, hidden, first/last seen |
| `room` | §4.3 |
| `scene`, `scene_action` | `SmartHomeScene` and its `SmartHomeCommand`s |
| `automation` | id, name, enabled, trigger type, trigger config (JSON), conditions (JSON) |
| `automation_action` | commands, in order |
| `reading` | device id, sensor type, value, unit, timestamp; indexed on (device, timestamp) |
| `event_log` | what happened and why: "Evening ran (automation: Sunset)", "Front door unlocked from SmartHomeCenter" |

The database lives in the per-user data directory: `$XDG_DATA_HOME/ULTRA/SmartHome`,
`%LOCALAPPDATA%\ULTRA\SmartHome`, or `~/Library/Application Support/ULTRA/SmartHome`.
UltraNetMonitor's `ui/UltraNetMonitorPaths.*` already shows how to find
these directories. Paths go through `PathFromUtf8` / `PathToUtf8`.

It is the program's own internal file, so it is read and written directly.
**Backup and restore are not.** The user picks the file, so *File → Back
up…* and *Restore…* go through `UltraCanvasFileLoader`'s Save and Open
dialogs, with a `.ultrahome` archive (the database plus a manifest). Network
keys and Matter fabric credentials are excluded unless the user ticks
"include radio keys" and gives a passphrase.

Protocol state that the SDKs keep themselves (OpenZWave's `zwcfg_*.xml`,
Matter's ini storage, the Zigbee network key on the NCP) stays where each
backend puts it. The database records where that is, so backup can find
it.

### 4.5 Sensor history and energy

`SmartHomeAPI::GetSensorHistory` is implemented over the `reading` table.
The manager writes a row when a reading changes by more than a per-type
deadband (0.1 °C, 1 % humidity, every binary change), and at least once
every 15 minutes otherwise. Rows older than 30 days are thinned to hourly
averages, and the period is a setting. A typical home of 30 sensors stays
in the low megabytes per year.

Energy uses the same table with `SmartHomeSensorType` gaining `Power` (W)
and `Energy` (kWh, cumulative). `SmartHomeEnergyMonitor` reads it instead
of whatever its host feeds it. The *Energy* page puts a
`UltraCanvasBarChartElement` (per day / per device) under it.

### 4.6 The automation engine

`EvaluateAutomations` is today a loop over enabled automations with a TODO
where the evaluation should be. It needs:

| Trigger (`TriggerType`) | Fires when |
|---|---|
| `time` | a clock time on chosen weekdays |
| `sunrise` / `sunset` | sun event ± offset, from the home's latitude/longitude (a setting; the calculation is about 60 lines and needs no library) |
| `device` | a device's attribute crosses or equals a value (motion = true, temperature > 26) |
| `state` | a device has been in a state for N minutes ("door open for 10 min") |
| `scene` | a scene was activated |
| `location` | **later**: presence needs a phone, so it waits for the remote-access phase |

Conditions are the same predicates without the edge: "between 18:00 and
23:00", "lock is locked", "somebody is home". Actions are `SmartHomeCommand`s,
plus two that are not device commands: `notify` (posts `system.notification`
with the automation's text) and `delay` (seconds).

Rules the engine keeps:

- **Edge-triggered.** A `device` trigger fires when the condition becomes
  true, not every second it stays true.
- **A loop guard.** An automation that triggers itself (through the
  device it changes) is stopped after three rounds in ten seconds and logged.
- **Every action is logged with its cause** in `event_log`, which the
  *Activity* page shows. "Why did the light turn on?" is the question every
  smart-home user asks, and the log is how the application answers it.
- **Safety-relevant actions need an explicit opt-in.** An automation that
  *unlocks* a lock or *disables* an alarm is marked as such in the editor,
  and stays disabled until the user confirms it.

`SmartHomeAutomationEditor` already round-trips its structured
`AutomationTrigger` through the JSON `TriggerConfig`. The engine reads the
same JSON. Its schema is written down in the module doc, so the editor and
the engine cannot drift apart.

### 4.7 A simulated backend

`protocols/Simulated/SimulatedProtocol.{h,cpp}` implements
`ISmartHomeProtocol` with no I/O. It is always built (it has no
dependencies) and is registered by the application when the user picks
*Try with a demo home* on first run, or when `--demo` is passed. It provides:

- a fixed sample home (eight rooms, about 25 devices covering every
  category: dimmable and colour lights, plugs with power metering, a
  thermostat whose temperature drifts toward its target, a lock that runs
  low on battery, blinds that take time to travel, motion, contact, water
  and smoke sensors that can be triggered from a *Simulate* menu);
- "pairing" that discovers a new device after a few seconds;
- an occasional device going offline and coming back.

It serves three purposes:

- **Development.** Every page can be built and exercised without hardware.
- **Tests.** An application-level test drives the real window against the
  simulator: create a scene, run it, assert device states; trigger the
  water sensor, assert the automation ran and a notification was posted.
- **Showcase.** The demo app's Smart Home entry can open the panel on the
  simulated home, so it shows something.

The simulator is labelled as such everywhere it appears: a "Demo home"
banner, and a separate database file, so a real home is never mixed with
fake devices.

### 4.8 Widgets: port their painted controls to framework elements

The widgets are a strong starting point, but they predate the house rule's
enforcement on `SmartHome/`. Before the application ships:

1. Add `SmartHome/ui` to `SEARCH_ROOTS` in `scripts/check_ui_reuse.py`, and
   let it report.
2. Replace the painted controls with child elements:

   | Painted today | Becomes |
   |---|---|
   | brightness, colour temperature, blind position and tilt sliders | `UltraCanvasSlider` |
   | on/off toggle | `UltraCanvasSwitch` |
   | buttons (lock, scene run, wizard Next/Back) | `UltraCanvasButton` |
   | thermostat dial | `UltraCanvasGaugeDiagramElement` plus `UltraCanvasSpinner` for the target |
   | battery and signal | `UltraCanvasGaugeDiagramElement` (`LinearBar`), `UltraCanvasBadge` |
   | colour | `UltraCanvasColorPicker` |
   | status pill | `UltraCanvasBadge` / `UltraCanvasChip` |
   | dialogs | `UltraCanvasModalDialog` |
   | device list | `UltraCanvasListView` with an `IListModel` |

   The cards, the topology view and the energy view stay self-rendered
   views (the documented exception) but use real elements for anything
   that takes input.
3. Keep every widget's public API and callbacks, so `WidgetTest` keeps
   passing.
4. Write `Docs/UltraCanvas/`-style pages for the widgets and list them in
   `Docs/UltraCanvas/UltraCanvasUIElements.md`. `check_element_catalogue.py`
   will want them there once they are under a scanned root.

### 4.9 Privacy and security

- **No data leaves the home** in phases 1–3. There is no account, no
  telemetry and no vendor cloud. The privacy declaration in `Docs/Legal/`
  says so in one paragraph.
- **Radio keys stay in UltraVault** rather than in the database (the
  Zigbee network key, the Z-Wave S2 keys, the KNX IP Secure password).
  The database stores a vault reference.
- **Locks and alarms ask before acting** from the dashboard: a lock's
  *Unlock* needs a press-and-hold or a confirm, configurable per device.
- **Security level is visible.** `SmartHomeSecurityLevel` is shown on every
  device (badge on the card, line in the detail view). A KNX device on plain
  KNXnet/IP or a Z-Wave device paired without S2 is marked *Unencrypted*.
- **Matter's test credentials must not ship.** STATUS.md §3 records that the
  Matter backend uses the SDK's example attestation and issuer. The
  application refuses to enable Matter in a release build until those are
  replaced, and says so on the protocol page.

---

## 5. The application

### 5.1 Window

A navigation list on the left and the page on the right
(`UltraCanvasSplitPane`; the left side is an `UltraCanvasListView`). Each
page is mostly an existing widget:

| Page | Content | Built from |
|---|---|---|
| **Home** | Favourites, scenes row, rooms with a summary each ("2 lights on, 21.5 °C"), active alerts | `SmartHomePanel` (`RoomView`), `SmartHomeSceneCard` |
| **Rooms** | One room's devices; drag a device between rooms | `SmartHomePanel` filtered by room |
| **Devices** | Every device as a sortable table: name, room, protocol, state, battery, signal, last seen; search | `UltraCanvasListView` + `UltraCanvasListSortFilterProxy` |
| **Device detail** (opens from any page) | The device's control, its history chart, its info (model, firmware, protocol, security), rename/room/remove | `SmartHomeLightControl` etc., `UltraCanvasLineChartElement`, `SmartHomeDeviceDialog` |
| **Scenes** | Scene cards; *New scene* opens the editor; *Capture current state* fills a scene from the devices as they are | `SmartHomeSceneCard`, `SmartHomeSceneEditor` |
| **Automations** | List with enable switches, last run, next run; editor | `SmartHomeAutomationEditor`, `SmartHomeScheduler` |
| **Energy** | Today / week / month, per room and per device | `SmartHomeEnergyMonitor`, bar chart |
| **Activity** | The `event_log`: what happened, when, and why | `UltraCanvasListView` |
| **Network** | Radios and their state; topology; *Advanced* (channel, keys, interview) behind a disclosure | `SmartHomeNetworkTopology` |
| **Settings** | Protocols and adapters, location (for sunrise/sunset), units, history retention, backup, service | standard elements |

The window adapts down to a narrow width (the navigation collapses to
icons), since the same application should run on a wall-mounted tablet.
A **kiosk mode** (`--kiosk`: full screen, Home page only, no settings,
optional PIN to leave) serves that tablet directly.

### 5.2 First run

1. **Welcome**, with two paths: *Set up my home* or *Try with a demo home* (§4.7).
2. **Location** (city search or coordinates), used only for sunrise and sunset.
3. **Adapters**: the application lists serial devices with a known
   vendor/product (Silicon Labs, Aeotec, Zooz, Sonoff, Nabu Casa; the list
   lives in the module). It offers the matching backend, checks port
   permissions, and offers to look for KNX gateways and Matter devices on
   the LAN. Backends this build does not include are shown greyed out,
   with the reason (`HasProtocolBackend` / `IsProtocolAvailable`).
4. **Rooms**: a suggested list to tick and rename.
5. **Add first device**: the pairing wizard.

### 5.3 Adding a device

`SmartHomePairingWizard`, per protocol:

| Protocol | The user does | The application does |
|---|---|---|
| Matter | Scans the QR code, from the camera or a photo, or types the 11-digit code | `ScanQRCodeFile` / the camera overload, the way `ScanAccountDialog` scans `otpauth://`; accepts `MT:` payloads; then `CommissionDevice(code)` |
| Zigbee | Resets the device | `StartPairing(Zigbee, 120)`; shows devices as they join and finish their interview |
| Z-Wave | Presses the device's inclusion button; enters the S2 PIN when asked | `StartPairing(ZWave)`; prompts for the DSK PIN |
| Thread | (via Matter-over-Thread, as above) | joiner flow |
| KNX | Picks the gateway, then imports group addresses from an ETS export (`.knxproj` / CSV) | the import goes through FileLoader's Open dialog |

The wizard ends on *Name and room*, with the name suggested from the
device's model.

### 5.4 Command line

The same binary, as UltraNetMonitor does:

```
SmartHomeCenter                       # the window
SmartHomeCenter --demo                # the window, on the simulated home
SmartHomeCenter --kiosk               # full-screen Home page
SmartHomeCenter --service             # headless hub (phase 3)
SmartHomeCenter --list [--csv]        # devices and states
SmartHomeCenter --set <device> <attr>=<value>
SmartHomeCenter --scene <name|id>
SmartHomeCenter --history <device> --since 24h [--csv]
SmartHomeCenter --log --since 1h
```

The command line makes the module scriptable from day one, and gives the
tests a way in that does not need a display.

---

## 6. File layout

```
Apps/SmartHomeCenter/
  CMakeLists.txt               # BUILD_SMARTHOME_CENTER, needs SmartHome + SmartHomeUI
  SmartHomeCenter.desktop
  README.md
  main.cpp                     # bootstrap, command line, --service
  ui/
    SmartHomeCenterWindow.*    # split pane, navigation, page switching
    SmartHomeCenterPages.*     # Home, Devices, Activity, Settings
    SmartHomeCenterFirstRun.*  # the five-step setup
    SmartHomeCenterDeviceModel.*  # IListModel for the Devices table
    SmartHomeCenterPaths.*     # per-user data directory
    SmartHomeCenterKiosk.*

SmartHome/
  core/SmartHomeStore.*        # UltraDatabase persistence (§4.4)
  core/SmartHomeAutomationEngine.*   # §4.6, out of SmartHomeManager
  core/SmartHomeSun.*          # sunrise/sunset
  core/SmartHomeHistory.*      # §4.5
  protocols/Simulated/SimulatedProtocol.*   # §4.7, always built
  tests/StoreTest.cpp, AutomationTest.cpp, SimulatedTest.cpp

Docs/SmartHomeCenter/CHANGELOG.md
media/appicon/SmartHomeCenter.{svg,png}
```

The top-level `CMakeLists.txt` gets a `BUILD_SMARTHOME_CENTER` option next
to the other applications (`CMakeLists.txt:550-729`), on by default when
`BUILD_SMARTHOME` is.

---

## 7. Roadmap

| Phase | Delivers | Done when |
|---|---|---|
| **0 — module gaps** | Home model (§4.3), `smarthome.db` (§4.4), automation engine (§4.6), history (§4.5), simulator (§4.7); `STATUS.md` updated | Facade, store, automation and simulator tests pass; a home survives a restart |
| **1 — the window** | Home, Rooms, Devices, Device detail, Scenes, Settings, first run; `--demo`, `--list`, `--set`, `--scene`; demo-app entry opens the panel on the simulator and moves to `PartiallyImplemented` | Every page usable on the simulated home; app-level test passes in CI |
| **2 — widget port and depth** | §4.8 port and `check_ui_reuse` on `SmartHome/ui`; Automations, Energy, Activity, Network pages; backup/restore; notifications; kiosk mode | `check_ui_reuse` clean over `SmartHome/ui`; widget docs in the catalogue |
| **3 — hub service** | `--service`; the UltraMessage facade `Impl` (§4.2); systemd user unit / login item; GUI as client | Automations run with no window open; two GUI instances stay in sync |
| **4 — first hardware** | KNX against a real gateway; Z-Wave and Zigbee against a USB stick; Matter against one device, with real attestation in place of the test credentials (§4.9) | One device per protocol paired, controlled and surviving a restart, recorded in `STATUS.md` |
| **5 — beyond the house** | Remote access through the ULTRA account; phone presence for `location` triggers; camera live view; voice | Separate proposal |

Phases 0 and 1 need no hardware and no new third-party code.

---

## 8. Gap analysis

| Needed | Have | Gap |
|---|---|---|
| Save the home | `SaveConfiguration` stub | §4.4 |
| Rooms | panel-local map | §4.3 |
| Automations that run | storage + empty evaluator | §4.6 |
| History, energy | `GetSensorHistory` returns `{}` | §4.5 |
| Something to show without hardware | none | §4.7 |
| Accessible, themed controls inside the widgets | framework elements (done 2026-10-21) | §4.8 |
| Background operation | in-process only | §4.2, phase 3 |
| Matter over IPv6 discovery | mDNS is IPv4 only | Framework: AAAA in `MdnsPlugin` (STATUS.md §3) |
| Matter commissioning over BLE | no BLE transport | Framework gap (STATUS.md §3); until then, only devices already on the network (on-network commissioning) |
| Production Matter credentials | SDK examples | §4.9, phase 4 |

---

## 9. Open questions

1. **Name.** *SmartHome Center* (`Apps/SmartHomeCenter`) is used
   throughout. The ULTRA OS front page says "ULTRA OS Smart Home";
   `UltraHome` would match `UltraMail` / `UltraFiler`. The name is the
   owner's call.
2. **Hub service in phase 3, or earlier?** Phase 1 keeps it in-process to
   get a usable window soon. If the first real deployment is a
   wall-mounted ULTRA OS box, the service could come before the full window.
3. **Which hardware first?** KNX needs no SDK but needs a gateway. A Z-Wave
   or Zigbee USB stick is the cheapest real test (about €30). Matter needs a
   device plus the credential work.
4. **Multiple homes.** One home per user account is assumed. A holiday
   house or an office would need a home selector and one database each.
   That is cheap to allow in the schema now (a `home_id` column) and
   expensive later.
5. **Multiple users in one home.** Who may unlock the door? Phase 1 has no
   users: whoever runs the application controls the home. Per-person
   permissions arrive with remote access.
6. **Interop with Home Assistant.** Many prospective users already run it.
   A read-only bridge (its REST/WebSocket API as one more backend) would
   let SmartHome Center be a frontend on day one. Whether that undermines
   the "own stack" message is a product decision.

---

## See also

- [`SmartHome/STATUS.md`](../../SmartHome/STATUS.md): engine, backends,
  widgets, and what each has been verified against
- [`Docs/Modules/Smarthome/README.md`](../Modules/Smarthome/README.md): the
  module's feature overview
- [`UltraCalendarDesignProposal.md`](UltraCalendarDesignProposal.md): device
  schedules as a calendar consumer
- [`UltraMessageDesignProposal.md`](UltraMessageDesignProposal.md): the
  channel the hub service speaks
- [`Apps/UltraNetMonitor/README.md`](../../Apps/UltraNetMonitor/README.md):
  the closest sibling in shape (module plus window plus command line)
