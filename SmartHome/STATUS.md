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

Measured with the repository's own include paths, `-std=gnu++20`.

| Component | Errors | State |
|---|---|---|
| `include/UltraCanvasSmartHome.h` | 0 | clean |
| `include/ISmartHomeDevice.h` | 0 | clean |
| `include/ISmartHomeProtocol.h` | 0 | clean |
| `include/SmartHomeProtocolBase.h` | 0 | clean (was 6) |
| `core/UltraCanvasSmartHomeManager.h` | 0 | clean |
| `protocols/ZWave` | 5 | signature mismatches |
| `protocols/KNX` | 6 | missing `<condition_variable>`, signatures |
| `protocols/Zigbee` | 23 | signature mismatches |
| `protocols/Thread` | 30 | signature mismatches |
| `protocols/Matter` | 32 | undeclared callbacks, signatures |
| `ui/*.h` | 116 | wrong base class and event model |

### Already fixed

The protocol base class disagreed with its own interface; this blocked all
five backends at once. Fixed in `include/`:

- `GetCapabilities()` returned `uint32_t` against the interface's
  `ProtocolCapability`.
- `GetDeviceIds()` / `GetDeviceInfo()` / `HasDevice()` were marked `override`
  but are base-class conveniences the interface never declared.
- `GetDiagnostics()` returned three different types in three files. The
  interface now returns the `std::map<std::string, std::string>` the base
  already builds, and Thread's protocol-specific variant was renamed
  `GetThreadDiagnostics()` so it no longer hides the virtual.
- `SetState()` invoked `onStateChange` with two arguments; the callback takes
  one.

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

### 2. Protocol backends (96 errors)

Mostly backend methods whose signatures drifted from `ISmartHomeProtocol`
(e.g. `FormNetwork(const std::string&)` against the interface's
`FormNetwork()`), plus missing includes. Mechanical, once decided per case
whether the interface or the backend is right.

### 3. Third-party dependencies — not yet resolved

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

### 4. Two framework gaps the design assumes

- **No BLE transport.** Matter commissioning needs Bluetooth LE; the only
  Bluetooth here is adapter *detection* in `UltraCanvasHardwareInfo`.
- **No public-key crypto.** Matter device attestation needs X.509, which
  `Docs/Modules/UltraCrypt/README.md` §2 puts explicitly out of scope
  ("No consumer"). That ruling now has a consumer and needs revisiting.

mDNS/DNS-SD, by contrast, already exists
(`UltraCanvas/Plugins/UltraNet/mdns/MdnsPlugin.cpp`, Avahi / Bonjour / Win32),
but resolves IPv4 only — Matter and Thread need AAAA.

### 5. No implementation for the UI layer

The three `ui/` headers declare widgets; no `.cpp` files were supplied for
them. `SmartHomeAPI` in the public header is likewise declared with a pImpl
whose implementation is absent — only `SmartHomeManager` has a `.cpp`.
