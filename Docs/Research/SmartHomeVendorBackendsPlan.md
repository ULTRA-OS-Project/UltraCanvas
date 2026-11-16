# SmartHome vendor backends — plan

Status: **in progress** (phase 1 started 2026-11-16). Owner of the decisions:
the project owner; open questions are at the end.

The SmartHome module speaks five standard protocols (Matter, Thread, Zigbee,
Z-Wave, KNX; `SmartHome/STATUS.md`). Most appliances people already own do not
speak any of them: a Xiaomi robot vacuum talks Xiaomi's own *miIO* protocol over
Wi-Fi, an LG washing machine talks only to LG's ThinQ cloud. This plan adds
backends for those, in the order that reaches the most devices for the least
dependency, and the device vocabulary they need first.

## What is missing today

| Gap | Effect |
|---|---|
| No vacuum or appliance vocabulary in `SmartHomeAPI` | A vacuum can be *named* an Appliance (Matter does that now) but not started, docked or watched; nothing reports a wash cycle's phase or time left |
| No vendor backend | Xiaomi / Mi, Roborock, Dreame, LG, Tuya, Shelly, Hue bridges, Home Assistant: unreachable |
| No secret store for device credentials | A miIO token or a ThinQ access token would sit in a plain file |

## Phases

### Phase 0 — the vocabulary (needed by every phase below)

- `SmartHomeVacuumState` (state: docked / cleaning / returning / paused /
  error, battery, fan level, error text, cleaned area and time) and
  `SetVacuumCommand(id, start|pause|dock|locate|spot)`, `SetVacuumFanLevel`.
- `SmartHomeApplianceState` (operational state, programme, phase, time
  remaining, finished-at, door/lid) and `SetApplianceCommand(id,
  start|pause|stop)` — the shape Matter's Operational State cluster and LG
  ThinQ both have, so one vocabulary serves both.
- SmartHome Center: a control set for each, a tile line ("Cleaning · 63 %",
  "Rinse · 0:24 left"), an *appliance finished* event for automations.
- The simulated home gains a robot vacuum and a washing machine that run
  cycles, so all of it is tested without hardware.

### Phase 1 — Xiaomi miIO, local (started)

The protocol of Xiaomi's Wi-Fi devices (Mi / Mijia, Roborock, Dreame, Viomi
vacuums; purifiers, humidifiers, lamps, plugs). Local UDP on port 54321, no
cloud at run time:

- 32-byte header: magic `0x2131`, packet length, 4 unknown bytes, device id,
  timestamp (seconds, the device's clock), 16-byte checksum.
- Body: JSON-RPC (`{"id":n,"method":...,"params":[...]}`), AES-128-CBC with
  PKCS#7, key `MD5(token)`, IV `MD5(key ‖ token)`.
- Checksum: MD5 over header (token in the checksum field) and body.
- A *hello* (header of `0xFF`s, length 32) is answered by every device on the
  LAN with its id and clock — that is both discovery and the handshake before
  each exchange.
- Two command families: the older per-model methods (Roborock: `get_status`,
  `app_start`, `app_pause`, `app_charge`, `find_me`, `set_custom_mode`) and
  **MIoT spec** (`get_properties` / `set_properties` / `action` on
  service/property/action ids, which `miot-spec.org` publishes per model).

The one hard part is the **token**: 32 hex digits per device, set when the
device was added to the Mi Home app. Users get it from their Xiaomi account
(the cloud API returns it with the device list) or from tools that ask the
cloud for it. Phase 1 takes it as configuration; a Xiaomi-account login that
fetches tokens is step 1.6.

| Step | What | Verified by |
|---|---|---|
| 1.1 | MD5 in UltraCrypt, legacy-gated like SHA-1 (UltraCrypt is the one place hashing lives) | RFC 1321 vectors, `md5sum` |
| 1.2 | `MiioPacket`: header, checksum, encryption, hello — SDK-free bytes in, bytes out | test vectors made with `openssl` / `md5sum`, independent of the code |
| 1.3 | `MiioRpc`: request ids, JSON-RPC requests and replies, MIoT property / action payloads | tests |
| 1.4 | `MiioTransport`: UDP socket, hello handshake, clock offset, retries, reply matching by id; discovery by broadcast hello | a fake miIO device on loopback in the test |
| 1.5 | `XiaomiProtocol` backend: devices configured by address + token (+ model), profiles mapping models to the phase-0 vocabulary — Roborock legacy, MIoT vacuum (`siid 2/3`), generic MIoT switch / plug | fake device; then real hardware |
| 1.6 | Xiaomi-account login to fetch tokens (cloud, optional) | real account |

New protocol type `SmartHomeProtocolType::Xiaomi`, as the comment on the enum
promised: a vendor's own protocol is a type of its own.

**Not verified against a second implementation.** The layout above is the
documented, widely re-implemented one (python-miio and others); installing
python-miio to cross-check was not possible in the build environment, so the
codec tests check the cryptography with standard tools and the layout against
this description. First contact with a real device confirms it.

### Phase 2 — LG ThinQ Connect (cloud)

LG appliances have no local API. LG's official *ThinQ Connect* API: a
personal access token from LG's developer portal, REST for the device list,
profiles and control, MQTT (AWS IoT, client certificate) for live state.
UltraNet already has HTTP and an MQTT plugin. Maps to `SmartHomeApplianceState`
(washer, dryer, dishwasher, fridge, oven, air conditioner). Needs internet and
an LG account by design — the backend says so in its hardware description.

### Phase 3 — Home Assistant bridge

The proposal's open question 6 (`SmartHomeCenterDesignProposal.md`). One
backend over Home Assistant's REST + WebSocket API exposes every integration a
user already runs (Xiaomi, Roborock, LG, Tuya, Hue, Shelly, Sonos …) as
devices here. Cheapest coverage; depends on the user running Home Assistant.

### Phase 4 — further local protocols

Tuya local (encrypted TCP, per-device local key — same token problem as
miIO), Shelly (plain HTTP/RPC on the LAN, no secret), Philips Hue bridge
(local REST with a button-press pairing). Each is small once phases 0–1 exist.

### Cross-cutting — secrets

A miIO token, a ThinQ access token and a Home Assistant token are
credentials. They belong in the ULTRA OS secret store (UltraVault), with the
backend keeping only a reference; until that is wired, phase 1 keeps them in
the module's configuration and says so.

## Open questions

1. Phase 0 before phase 1's backend (recommended: a backend with no
   vocabulary can only show a vacuum as an inert tile), or the backend first?
2. Cloud token fetch (1.6): acceptable to talk to Xiaomi's servers at all,
   once, to obtain tokens? Without it the user brings the token.
3. Phase 2 versus phase 3 first: LG directly (no Home Assistant needed) or
   the bridge (many vendors at once)?

## Progress

- 2026-11-16: plan; 1.1 (MD5 in UltraCrypt) and 1.2–1.3 (packet codec and
  RPC) built and tested.
