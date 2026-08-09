# UltraWin — Windows Application Compatibility Module (Design Proposal)

**Date:** 2026-08-09
**Status:** Proposal — for review; no implementation yet
**Companion:** [WindowsCompatibilityResearch.md](WindowsCompatibilityResearch.md)
(the survey that selected the engines wrapped here)

---

## 1. Purpose

UltraWin lets ULTRA OS run Windows applications **as single native windows —
never a Windows desktop — with full access to the user's native folders**. It
is a sibling module of UltraNet/UltraAI: an UltraCanvas-owned API wrapping
open source engines so backends can be swapped without touching callers.

Two backends behind one API, selected automatically per application:

| Tier | Engine | When used |
|---|---|---|
| 1 (default) | **Wine 11+** (LGPL, child processes) | Most apps: instant launch, no Windows license, native windows |
| 2 (fallback) | **QEMU/KVM** guest + **FreeRDP RemoteApp** (Apache-2.0, linked) | Apps Wine cannot run (kernel drivers, anti-cheat, exotic installers) |

UltraWin rules (mirroring the UltraNet registry entry style):

- Clear structure; function names understandable on sight (PascalCase).
- Blocking operations return `UltraWinResult`; app/VM/environment handles are
  opaque `UltraWinHandle`s.
- No Windows desktop is ever displayed; every Windows app window is a native
  ULTRA OS window (Wine: by construction; VM: RemoteApp/RAIL only — no
  full-desktop viewer path).
- The user's folders are visible to Windows apps under the **same drive
  letter in both tiers** (default `U:` → user home), with Windows known
  folders (Documents, Desktop, Downloads) redirected onto it.
- GPL engines (QEMU) run strictly as separate processes; only
  license-compatible libraries (FreeRDP Apache-2.0, optionally libvirt LGPL)
  are linked.
- Never expose Wine/QEMU/FreeRDP types in public headers.

## 2. Proposed public function surface

Lifecycle & capability discovery:

- `UltraWin_Initialize`, `UltraWin_Shutdown`
- `UltraWin_GetCapabilities` — which tiers are available on this host
  (Wine present? `/dev/kvm`? ntsync module? ARM64 → Hangover?)

Running applications (tier chosen automatically unless forced):

- `UltraWin_RunApp(exePath, options) → UltraWinHandle`
- `UltraWin_RunAppAsync`, `UltraWin_CloseApp`, `UltraWin_KillApp`
- `UltraWin_GetAppState`, `UltraWin_ListRunningApps`
- `UltraWin_QueryCompatibility(exePath)` — compatibility-database lookup
  returning the recommended tier + confidence

Environments (Wine prefixes, Bottles-style isolation):

- `UltraWin_CreateEnvironment`, `UltraWin_DeleteEnvironment`,
  `UltraWin_ListEnvironments`
- `UltraWin_InstallComponent(env, name)` — runtime dependencies
  (fonts, VC++ runtimes, .NET, DXVK), winetricks-equivalent

Folder mapping (both tiers):

- `UltraWin_MapFolder(hostPath, driveLetter)`, `UltraWin_UnmapFolder`,
  `UltraWin_ListMappings`

VM lifecycle (tier 2 only; one shared, suspendable guest):

- `UltraWin_VmProvision(isoPathOrDownload, options)` — unattended
  Windows Pro install, virtio drivers, RemoteApp allow-list, virtiofs service
- `UltraWin_VmStart`, `UltraWin_VmSuspend`, `UltraWin_VmStop`,
  `UltraWin_VmGetState`

## 3. Architecture

```
UltraWin public API (C-style, opaque handles)
├── Tier router (compatibility DB, seeded from WineHQ AppDB)
├── WineBackend                     ├── VmBackend
│   spawns wine/wineserver          │   spawns QEMU (KVM, virtio, headless)
│   per-env prefixes                │   controls it via QMP (see §5)
│   DXVK/vkd3d for D3D              │   virtiofs → U: in guest
│   Hangover on ARM64               │   links libfreerdp → RAIL windows
│   windows are native already      │   one RDP connection, multiplexed
└── UltraCanvasRemoteAppView — element hosting one RAIL app surface
```

- **WineBackend:** manages prefixes under
  `~/.local/share/ultrawin/environments/<name>/`, launches apps via
  `wineserver`-supervised child processes, maps `U:` → home (and removes the
  default `Z:` → `/` mapping for tidier app-visible drives; configurable).
- **VmBackend:** a single Windows Pro guest, kept suspended when idle.
  Windows apps are launched through RemoteApp (`RAIL_ORDER_EXEC`) over one
  FreeRDP connection; each app window surfaces in an
  `UltraCanvasRemoteAppView` element added to the element catalogue, giving
  it ULTRA OS window chrome, clipboard, and focus semantics.
- **Filesystem:** `U:` is the same physical home directory in both tiers —
  Wine drive mapping on tier 1, virtiofs (WinFsp + `VirtioFsSvc`) on tier 2;
  RDP drive redirection (`\\tsclient`) is the tier-2 fallback when virtiofs
  provisioning is unavailable.

## 4. Platform matrix

| Host | Tier 1 | Tier 2 |
|---|---|---|
| x86-64 Linux / ULTRA OS | Wine 11 | QEMU/KVM x86 guest |
| ARM64 | Hangover (Wine + FEX/Box64), ~60–80 % native | Windows-on-ARM guest via KVM — possible but immature; defer |
| RISC-V | Box64 has RISC-V support (experimental); defer | No — defer |

## 5. Decisions proposed (for review)

1. **QMP directly, not libvirt.** libvirt adds a daemon dependency and
   packaging weight; UltraWin manages exactly one VM with a known
   configuration, which QMP over a UNIX socket handles in a few hundred
   lines. Revisit if VM management needs grow.
2. **No Docker/containers.** WinBoat wraps QEMU in Docker only for
   distribution convenience (`dockur/windows`); UltraWin runs QEMU directly
   and reimplements the unattended-install answer files (`autounattend.xml`)
   natively. Fewer moving parts, no container runtime dependency.
3. **System Wine first, bundled runner later.** Phase 1 detects distro Wine
   ≥ 10; a pinned, bundled runner (Proton-style) comes with the gaming
   profile in Phase 3.
4. **Embed libfreerdp, don't shell out to `xfreerdp`** — required for
   multiplexing many app windows over one connection and for rendering into
   UltraCanvas elements (FreeRDP issue #12625).
5. **Windows licensing UX:** tier 2 is strictly opt-in; provisioning
   requires the user to supply/download Windows install media and states the
   license requirement. Tier 1 never needs this.

## 6. New third-party dependencies (when implementation starts)

To be recorded in `Docs/Dependencies.md`, `master_dependencies.yaml`,
`THIRD_PARTY_LICENSES.md` per house rule 5:

| Dependency | Kind | License | Linked? |
|---|---|---|---|
| Wine ≥ 10 (11 recommended) | runtime, child process | LGPL-2.1+ | No |
| QEMU + KVM | runtime, child process | GPL-2.0 | No |
| FreeRDP 3 (libfreerdp) | library | Apache-2.0 | **Yes** |
| virtio-win guest drivers + WinFsp | guest-side | GPL/BSD mix; WinFsp GPLv3-with-FLOSS-exception | No (inside guest) |
| DXVK / vkd3d-proton | runtime, per-env | zlib / LGPL | No |
| Hangover + FEX/Box64 (ARM64 builds) | runtime, child process | LGPL / MIT | No |

## 7. Phasing

- **Stage 1 — Wine tier:** capability detection, environments, `RunApp`,
  `U:` mapping, `.exe` association in UltraFiler. Registry entry added to
  `Masterfile_modules.md`; module doc under `Docs/Modules/UltraWin/`.
- **Stage 2 — VM tier:** provisioning, QMP lifecycle, virtiofs,
  `UltraCanvasRemoteAppView` + RAIL multiplexing.
- **Stage 3 — polish:** compatibility DB + automatic routing, bundled
  gaming runner (DXVK profile), ARM64 via Hangover, Looking Glass option.

## 8. Open questions

1. Compatibility database format & update channel (ship static seed, update
   via UltraNet from a project-hosted feed?).
2. Should environments be per-app by default (Bottles model) or shared with
   per-app opt-out? Proposal: per-app by default; disk cost is small with
   `wineboot --init` + shared read-only runner.
3. Audio path for tier 2: RDP audio channel (simple) vs virtio-snd
   (lower latency) — measure in Stage 2.
4. Where does the VM disk image live, and what is the default size/quota?
