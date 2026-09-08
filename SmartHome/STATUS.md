# Smart Home module — integration status

Source dropped in 2025-12-08, written against the design in
*Smart Home Protocol Research / Ultra OS Integration Guide* (same date).
This file records what compiles today and what is still to do. It is a
working document: delete it once the module builds.

**The module is NOT in the build.** No `add_subdirectory(SmartHome)` exists
yet, deliberately — the protocol backends and the UI layer do not compile
against this framework yet (see below). Nothing here can break other targets.

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
This is a header check: the `.cpp` files cannot be compiled yet because the
vendor SDKs they include are absent (see §2).

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
| `ui/*.h` | 116 | wrong base class and event model |

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

### Known latent bug

`MatterProtocol::SubscribeAttribute` stores its callback in
`attributeSubscriptions` but nothing ever invokes it — the map is only written
and cleared. Attribute subscriptions will never fire until
`OnAttributeChanged` dispatches to it. Left as-is: fixing it is behaviour, not
integration.

## What is left

### 1. UI layer — the real work (116 errors)

The widgets were written against a different framework shape:

| Written against | This framework |
|---|---|
| `class UIElement` | `UltraCanvasUIElement` — no `UIElement` type exists |
| `Render(IRenderContext*)` | `Render(IRenderContext*, const Rect2Df&)` |
| `OnMouseDown/Up/Move/Wheel/KeyDown/TouchStart/Move/End` | one `OnEvent(const UCEvent&)` |
| `OnResize(int,int)` | no such virtual |

Every `override` in the three UI headers therefore fails. This is a port, not
a rename: the per-event virtuals must fold into a single `OnEvent` switch on
`UCEvent`. Colours are raw `uint32_t` literals and should become the
framework's `Color`.

`SmartHomeAutomationEditor::AddAction` takes `SmartHomeSceneAction`, a type
that is never defined anywhere; scene actions are `SmartHomeCommand`.

No `.cpp` files were supplied for the UI layer at all, and `SmartHomeAPI` in
the public header declares a pImpl whose implementation is likewise absent —
only `SmartHomeManager` has one.

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
