# Smart Home module — integration status

Source dropped in 2025-12-08, written against the design in
*Smart Home Protocol Research / Ultra OS Integration Guide* (same date).
This file records what compiles today and what is still to do. It is a
working document: delete it once the module builds.

**The module is in the build.** `BUILD_SMARTHOME` is ON by default and builds
two targets: `SmartHome` (engine and facade) and `SmartHomeUI` (the widgets
that have implementations). Every protocol backend is behind its own option and
all of them are OFF, because none can compile until its vendor SDK is vendored.
`ULTRACANVAS_BUILD_SMARTHOME_TESTS=ON` adds the two test executables to ctest.

## Layout

Follows §4.1 of the research document, with the directory renamed from
`UltraCanvasSmartHome/` to `SmartHome/` to match the sibling modules
(`UltraAI/`, `UltraCloud/`, `UltraNet/`, `VirtualFS/`), none of which carry
the `UltraCanvas` prefix.

| Path | Contents |
|---|---|
| `include/` | Public API, the two core interfaces, protocol base class |
| `core/` | `SmartHomeManager` singleton (registry, command queue, threads) |
| `protocols/<Name>/` | Matter, Thread, Zigbee, ZWave, KNX backends |
| `ui/` | Panel, per-device controls, advanced widgets |

`devices/` from §4.1 is not present; device abstractions currently live in
`include/ISmartHomeDevice.h`.

## Compile status

Header syntax checked with the repository's own include paths, `-std=gnu++20`.
The `core/` sources and the facade test are fully compiled, linked and run.
The protocol backends remain a header-only check: their `.cpp` files include
vendor SDKs that are absent (see §2).

| Component | Errors | State |
|---|---|---|
| `include/UltraCanvasSmartHome.h` | 0 | clean |
| `include/ISmartHomeDevice.h` | 0 | clean |
| `include/ISmartHomeProtocol.h` | 0 | clean |
| `include/SmartHomeProtocolBase.h` | 0 | clean (was 6) |
| `core/UltraCanvasSmartHomeManager.h` | 0 | clean |
| `protocols/Matter` | 0 | clean (was 32) |
| `protocols/Thread` | 0 | clean (was 30) |
| `protocols/Zigbee` | 0 | clean (was 23) |
| `protocols/ZWave` | 0 | clean (was 5) |
| `protocols/KNX` | 0 | clean (was 6) |
| `core/*.cpp` | 0 | **compiles and links** |
| `tests/FacadeTest.cpp` | 0 | **compiles, links and passes** |
| `ui/*.h` | 0 | clean (was 116) — ported |
| `ui/UltraCanvasSmartHomeDeviceCard.cpp` | 0 | **builds and passes** |
| `ui/UltraCanvasSmartHomePanel.cpp` | 0 | **builds and passes** |
| `ui/UltraCanvasSmartHomeDeviceControl.cpp` | 0 | **builds and passes** |
| `ui/UltraCanvasSmartHomeDialogs.cpp` | 0 | **builds and passes** |
| `ui/UltraCanvasSmartHomeEditors.cpp` | 0 | **builds and passes** |
| `ui/UltraCanvasSmartHomeAdvanced.cpp` | 0 | **builds and passes** |

### How the backends were fixed

One pattern accounted for nearly all 96 backend errors: **the interfaces in
`ISmartHomeProtocol.h` were an early sketch, and the backends had moved on.**
Where all five backends agreed with each other — and with the two programs in
`examples/` — the interface was treated as the stale side and brought up to
the code that actually works.

- `FormNetwork()` took no arguments; every backend and both examples pass a
  network name. Now `FormNetwork(const std::string& = "")`.
- The info-level device API (`GetPairedDevices`, `PairDevice`, `UnpairDevice`,
  `GetDeviceState`) existed in all five backends and in no interface.
- Groups, binding and OTA had capability queries (`SupportsGroups()` and
  friends) but no methods behind them. Added with default implementations, so
  Z-Wave and KNX — which do not implement them — stay concrete.
- `IZigbeeProtocol` and `IThreadProtocol` were stringly typed
  (`std::string GetPanId()`); the backends and the examples use the protocols'
  real widths (`uint16_t` PAN ID, `uint64_t` extended PAN ID), Thread datasets
  as TLV byte vectors, and a commissioner/joiner API the sketch lacked.
- `GetSecurityLevel()` returned a bare `int` — `return 5`, against a four-value
  `SmartHomeSecurityLevel` enum. Now returns the enum: `Encrypted` for
  Zigbee and Thread, `Certified` for Matter, which has attestation.
- Protocol-internal types stay out of the core header: the ZCL calls
  (`ReadAttribute` with `ZigbeeAttributeValue`) and Matter's fabric objects are
  backend-only and no longer claim to override anything.
- Matter used `OnAttributeChange` as a callback type that was never defined,
  and declared `OnCommissioningComplete` twice. `KNXProtocol.h` used
  `std::condition_variable` without including `<condition_variable>`.

### The two-layer split, settled

`SmartHomeAPI` (public, `include/`) and `SmartHomeManager` (engine, `core/`)
are two layers, not duplicates, and both stay:

| | `SmartHomeAPI` | `SmartHomeManager` |
|---|---|---|
| Audience | application developer, and the widgets | protocol-backend author |
| Vocabulary | `SmartHomeDeviceInfo`, `SmartHomeLightState` — values | `shared_ptr<ISmartHomeProtocol>` — live objects |
| Owns | nothing; it delegates | registries, command queue, three worker threads |

Neither is touched by the end user, who sees only the widgets.

The facade used to be incomplete: it had no way to register a protocol backend,
so an application could `EnableProtocol(Zigbee)` but never supply a Zigbee
backend — `examples/DevicePairing.cpp` had the call commented out with the note
"In real code, this would be done through SmartHomeManager". It is complete now:

- `SmartHomeAPI::RegisterProtocol` / `UnregisterProtocol` / `HasProtocolBackend`,
  taking the backend as a `shared_ptr` to a forward-declared type, so
  applications that only drive devices never see `ISmartHomeProtocol`.
- `RegisterProtocolFactory` / `UnregisterProtocolFactory` /
  `CreateSmartHomeProtocol` — declared in the original source but **never
  defined**, so any caller would have failed to link. Defined in
  `core/SmartHomeProtocolRegistry.cpp`.
- `RegisterBuiltinProtocols()` registers whichever backends CMake compiled in
  (`ULTRACANVAS_SMARTHOME_<NAME>`); `SmartHomeAPI::Initialize()` calls it, and
  `EnableProtocol()` builds a backend on demand from its factory.

**The rule worth keeping: if application code has to include `core/`, the
facade has a hole.**

`tests/FacadeTest.cpp` holds this line. It compiles and passes today.

### Bugs this uncovered

Two were found by actually building and running the module for the first time:

- `SmartHomeProtocolBase`'s destructor called `Shutdown()`, which is pure
  virtual on the interface — undefined behaviour during destruction, and an
  undefined reference at link time. Every backend already shuts itself down in
  its own destructor, so the base call was redundant as well as wrong. Removed.
- `SmartHomeManager::commandMutex` was the only one of five mutexes not
  declared `mutable`, while `GetPendingCommandCount() const` locks it. Would
  not compile. Fixed.
- `SmartHomeDeviceDialog` declared its own `IsVisible()` over a `bool visible`
  member. `UltraCanvasUIElement::IsVisible()` is **not** virtual, so that only
  hid it: the dialog would report itself closed while the framework's dispatch
  and focus handling, which call the base, carried on as though it were open.
  The member is gone; `Show()` and `Hide()` drive `SetVisible()`, so there is
  one answer. `tests/WidgetTest.cpp` asserts the two agree.

### Known latent bug

`MatterProtocol::SubscribeAttribute` stores its callback in
`attributeSubscriptions` but nothing ever invokes it — the map is only written
and cleared. Attribute subscriptions will never fire until
`OnAttributeChanged` dispatches to it. Left as-is: fixing it is behaviour, not
integration.

## What is left

### 1. UI layer — done

The three widget headers were written against a different element API. That
port is finished, and **all sixteen widgets now have implementations**:

| Written against | This framework |
|---|---|
| `class UIElement` | `UltraCanvasUIElement` — no `UIElement` type exists |
| `Render(IRenderContext*)` | `Render(IRenderContext*, const Rect2Df&)` |
| `OnMouseDown/Up/Move/Wheel/KeyDown/TouchStart/Move/End` | one `OnEvent(const UCEvent&)` |
| raw `uint32_t` colours | `Color` (the literals were RGBA, so `Color::FromRGBA` takes them as they stand) |

Mouse and touch share a path, because `UCEvent` carries both in the same
`pointer` / `pointerId` fields.

| File | Widgets |
|---|---|
| `ui/UltraCanvasSmartHomeDeviceCard.cpp` | device card, scene card |
| `ui/UltraCanvasSmartHomePanel.cpp` | the dashboard |
| `ui/UltraCanvasSmartHomeDeviceControl.cpp` | light, thermostat, lock, blind, sensor display |
| `ui/UltraCanvasSmartHomeDialogs.cpp` | device dialog, pairing wizard |
| `ui/UltraCanvasSmartHomeEditors.cpp` | scene editor, automation editor |
| `ui/UltraCanvasSmartHomeAdvanced.cpp` | topology, energy monitor, scheduler, group control |

Every widget drives its device through `SmartHomeAPI` and also reports through
its own callback, so a host can let the widget talk to the module or intercept
the change. Layout is computed from each element's local bounds rather than
fixed pixel positions.

`SmartHomeAutomation` stores its trigger as a type string plus a
`TriggerConfig` the public header documents as JSON, while the editor works in
the structured `AutomationTrigger`. The two are bridged with UltraCanvasJSON
rather than by pasting strings together, so a config holding a quote or a
backslash survives the round trip; the test asserts it by saving, reopening,
saving again and comparing the two configs.

`tests/WidgetTest.cpp` builds them all against `libUltraCanvas` and drives them
with synthetic `UCEvent`s — 36 assertions.

### 2. Third-party dependencies — not yet resolved

- **Matter** includes the connectedhomeip SDK (`chip::`), which is not
  vendored and not fetched by CMake. It also uses
  `chip::TestPersistentStorageDelegate` — a test class — for real storage.
- **Zigbee** includes `ezsp/ezsp.h`, `ezsp/ash-host.h` (Silicon Labs EZSP)
  and `znp/znp.h` (TI Z-Stack); none are present.
- Both `Docs/Dependencies.md` and the demo's dependency table still record
  the module as "No additional third party — (core only)", which stops being
  true the moment any backend is switched on. Update both together, and add
  the licence rows (connectedhomeip Apache 2.0, OpenThread BSD, OpenZWave
  LGPL) to `THIRD_PARTY_LICENSES.md`.

### 3. Two framework gaps the design assumes

- **No BLE transport.** Matter commissioning needs Bluetooth LE; the only
  Bluetooth here is adapter *detection* in `UltraCanvasHardwareInfo`.
- **No public-key crypto.** Matter device attestation needs X.509, which
  `Docs/Modules/UltraCrypt/README.md` §2 puts explicitly out of scope
  ("No consumer"). That ruling now has a consumer and needs revisiting.

mDNS/DNS-SD, by contrast, already exists
(`UltraCanvas/Plugins/UltraNet/mdns/MdnsPlugin.cpp`, Avahi / Bonjour / Win32),
but resolves IPv4 only — Matter and Thread need AAAA.

### 4. Examples

`examples/DevicePairing.cpp` (Thread) and `examples/ZigbeePairing.cpp` are
interactive command-line drivers. They were decisive in resolving the
interface-versus-backend disagreements above: where the sketch and the backend
differed, these showed which side real calling code uses. Two fixes were needed
in them: `DevicePairing.cpp` used `std::istringstream` without including
`<sstream>`, and its call to `GetDiagnostics()` became `GetThreadDiagnostics()`
after that method was renamed to stop it hiding the interface's virtual.

Neither is wired into CMake yet; they need the backends to link first.
