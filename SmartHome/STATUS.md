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

### 2. Third-party dependencies — decided, not yet installed

Every backend is OFF by default and each needs its vendor SDK. CMake now checks
for them and fails at configure time naming what is missing, rather than letting
the compiler emit a wall of missing-header errors:

| Backend | Needs | Licence |
|---|---|---|
| Matter | connectedhomeip + mbedTLS | Apache 2 |
| Thread | OpenThread + mbedTLS | BSD 3-Clause / Apache 2 |
| Zigbee | Silicon Labs EZSP (libezsp) | vendor |
| Z-Wave | OpenZWave 1.6 (`libopenzwave1.6-dev`) | **LGPL 2.1**, dynamic |
| KNX | nothing — KNXnet/IP over sockets | — |

**KNX and Z-Wave build today.** `-DULTRACANVAS_SMARTHOME_KNX=ON` compiles and
links with no third party at all. `-DULTRACANVAS_SMARTHOME_ZWAVE=ON` compiles
and links against OpenZWave 1.6 from `libopenzwave1.6-dev`, dynamically:
`libsmarthome.a` carries 98 undefined `OpenZWave::` symbols, `ldd` on a linked
binary lists `libopenzwave.so.1.6`, and no `OpenZWave::` symbol is defined in
the binary itself. `tests/ZWaveLinkTest.cpp` runs under ctest whenever that
backend is enabled.

Two macros gate the real code inside those backends and are easy to miss:
`ULTRACANVAS_WITH_ZWAVE` and `ULTRACANVAS_WITH_EZSP`. Selecting a backend's
source file is not enough — without its macro the file compiles into a shell
that links successfully and does nothing. CMake defines both now; the symptom
if it ever stops is a build that succeeds while `nm` shows no undefined
`OpenZWave::` symbols at all.

**What each backend actually contains** (counted, not assumed — a backend can
compile and link while calling nothing at all):

| Backend | Lines | Real SDK calls | State |
|---|---|---|---|
| Z-Wave | 2209 | 162 × `OpenZWave::` | **builds and links**, dynamically |
| KNX | 1994 | none needed — implements KNXnet/IP itself | **builds and links** |
| Thread | 1816 | 80 × `ot*` | real, but needs a *built* OpenThread |
| Matter | 1324 | 5 × `chip::` | thin wrapper; deferred |
| Zigbee | 2036 + 550 | ASH/EZSP written in-tree | **builds and links** |

**Zigbee: the transport is now written, in-tree.** The backend's `EZSP_*` and
`ZStack_*` functions were all `return false; // Not implemented`, including
`InitializeEZSP()`, and the includes named Silicon Labs' own host headers
(`ezsp/ash-host.h`) which this project does not ship — Legrand's libezsp has no
such header, so no library install would have satisfied them either.

Rather than adopt a vendor library, the two layers underneath are implemented
here, in `protocols/Zigbee/ezsp/`:

- `AshCodec` — ASH framing per UG101: byte stuffing, CRC-16/CCITT, data
  randomisation, the frame types, and a stream reader that reassembles frames
  split across serial reads. Every rule is a function from bytes to bytes,
  which is the point: `tests/AshCodecTest.cpp` exercises all of it with no
  radio attached, and checks the CRC against the published CRC-16/CCITT-FALSE
  value for "123456789" (0x29B1) rather than against itself, so a wrong
  polynomial or seed cannot pass by agreeing with its own mistake.
- `AshTransport` — the part that genuinely needs a port and a clock: termios
  setup, the RST/RSTACK handshake, sequence numbers, acknowledgement,
  retransmission on NAK or timeout.
- `EzspFrame` — EZSP frame encode/decode for both header formats, and the
  version command, which has to go out in the legacy format because its answer
  is what decides the format of everything after it.

`InitializeEZSP()` now opens the port, resets the NCP, negotiates the protocol
version and starts the receive pump. Form/permit-join/leave are wired to real
EZSP commands.

**APS, ZDO and ZCL are now unpacked (2026-09-09).** `EzspFrame` grew the layers
above the EZSP header: the 11-byte `EmberApsFrame`, `sendUnicast` /
`sendMulticast` parameter blocks, `incomingMessageHandler` decoding, the seven
ZDO requests the interview and binding need (Active_EP, Simple_Desc,
Node_Desc, IEEE_addr, Bind, Unbind, Mgmt_Leave) with their responses and
Device_annce, and the ZCL header plus attribute-record walker (Report
Attributes / Read Attributes Response, all fixed-width types, both string
lengths). `tests/EzspFrameTest.cpp` checks 41 byte layouts written from
UG100, the ZDP tables and ZCL 2.6 — not from the code.

On top of that, in `ZigbeeStack`:

- Unicasts and groupcasts carry a real APS frame (HA profile, host endpoint
  1, retry + route discovery for unicasts). The previous `EZSP_SendUnicast`
  sent `nwk, ep, cluster, len, data` with no APS frame at all — the NCP would
  have rejected every frame.
- ZDO requests register under their transaction sequence number in a map of
  their own (the ZCL map is keyed by ZCL TSN; the two spaces would collide),
  and `incomingMessageHandler` routes profile-0 responses back by that number.
  Bind, unbind and leave block until the device's status response, so `true`
  means the device confirmed, not that bytes left the port.
- ZCL: a reply with our TSN completes the pending request (this is how the
  synchronous `ReadAttribute` gets its answer); Report Attributes and
  unsolicited Read responses become `OnAttributeReport` calls per attribute;
  everything else goes to `OnZCLResponse`. Every message updates the sender's
  LQI/RSSI/last-seen.
- `Device_annce` from an unknown IEEE is treated as a join and starts the
  interview; from a known one it refreshes the network address.
- The interview no longer holds references into the node table across
  asynchronous callbacks (the previous `[this, &node]` captures dangled the
  moment a node was erased); each step is keyed by device id and waits for its
  answer before the next.

**What is still not done:** `EZSP_FormNetwork` sends `panId, channel, key`,
which is not `EmberNetworkParameters` (extendedPanId, panId, txPower, channel,
joinMethod, nwkManagerId, nwkUpdateId, channels — 20 bytes); the host endpoint
is never registered with `addEndpoint` and `networkInit` / security setup are
not sent; `messageSentHandler` and `trustCenterJoinHandler` are ignored. That
is the network-formation sequence, and it is the next piece of work.

**None of it has met a real NCP.** It is verified by compilation and by the
framing and layout tests. First contact with hardware should be at 115200 8N1
on the adapter's serial node, watching for RSTACK.

**Thread needs a built OpenThread, not a checkout.** Its headers are fine — one
rename, `openthread/tasklets.h` became `tasklet.h`, now fixed — but the backend
calls `otSysInit` / `otSysProcessDrivers` from the POSIX platform layer, and
OpenThread generates its core config at build time. Against a plain source tree
113 errors remain, essentially all of them that. Build OpenThread with
`OT_PLATFORM=posix` first; CMake now looks for `libopenthread-posix` as well as
the headers, and defines `ULTRACANVAS_WITH_OPENTHREAD`.

**Watch for the gating macros.** `ULTRACANVAS_WITH_ZWAVE`,
`ULTRACANVAS_WITH_OPENTHREAD` and `ULTRACANVAS_WITH_EZSP` each gate their
backend's real code. Selecting the source file is not enough: without the macro
the file compiles into a shell that links successfully and does nothing. That is
how the Z-Wave backend first built here — cleanly, with zero undefined
`OpenZWave::` symbols, which is what gave it away.

**Backend plan (decided 2026-09-09).** KNX first, because it needs nothing.
Zigbee on **EZSP only** — the backend also carries TI Z-Stack branches, but
every one is `return false; // Not implemented`, so offering the choice would
only invite someone to pick the half that does nothing; nothing defines
`ULTRACANVAS_WITH_ZSTACK` and those branches stay inert. **Matter is deferred**:
its option stays, and turning it on without the SDK is a clean configure error.
No stub backend was written for it — `EnableProtocol(Matter)` returning false
is more honest than an object that accepts commands and drops them.

**Crypto backend: mbedTLS** (decided 2026-09-09). Matter's device attestation
and OpenThread's commissioner both need X.509 and ECDSA on P-256. Neither asks
UltraCrypt for it, so the only question was which backend those SDKs are built
against. mbedTLS is what both default to, so they share one stack; it is ~1 MB
static against OpenSSL's ~4–5 MB, which matters on the ARM and RISC-V boards
ULTRA OS targets; and OpenSSL is linked only on Linux and Android today
(Windows uses Schannel, macOS SecureTransport), so it would be new on two of the
three desktop platforms — the point
`Docs/Modules/UltraCrypt/README.md` §3 makes as *"there is no free ride"*.

**UltraCrypt is unaffected.** libsodium remains the framework's crypto backend.
It has no P-256 and no X.509, so it was never a candidate for this job, and the
2026-08-10 ruling did not cover PKI. Nothing about that ruling changes.

`Docs/Dependencies.md`, the demo's dependency table and
`THIRD_PARTY_LICENSES.md` all record this now. **OpenZWave is LGPL 2.1**, the
only copyleft component in the tree — link it dynamically or leave that backend
off.

### 3. Two framework gaps the design assumes

- **No BLE transport.** Matter commissioning needs Bluetooth LE; the only
  Bluetooth here is adapter *detection* in `UltraCanvasHardwareInfo`.
- **Public-key crypto — probably NOT UltraCrypt's problem.** Matter device
  attestation needs X.509 and ECDSA on P-256, which
  `Docs/Modules/UltraCrypt/README.md` §2 puts out of scope. But
  `protocols/Matter/MatterProtocol.cpp` does not ask UltraCrypt for any of it:
  it includes connectedhomeip's own
  `credentials/DeviceAttestationCredsProvider.h`, so the PKI lives inside the
  SDK and comes with whichever crypto backend that SDK is built against
  (mbedTLS by default; OpenSSL and PSA are the alternatives). Reopening the
  UltraCrypt ruling is therefore a choice, not a prerequisite — an earlier
  version of this file called it a prerequisite, which overstated it.

  What *is* a real problem in that file: it uses
  `credentials/examples/DeviceAttestationCredsExample.h`, the SDK's **test**
  credentials, alongside `chip::TestPersistentStorageDelegate`. Both must be
  replaced with real device credentials and real storage before anything
  ships. That is credential provisioning, not a missing crypto library.

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
