# UltraWin

**Windows-application compatibility module for ULTRA OS.**
Sibling of `UltraCanvas` (UI), `UltraNet` (networking) and `UltraAI` (AI).

UltraWin runs Windows applications on Linux / ULTRA OS **as single native
windows — never a Windows desktop — with the user's own folders visible to
the app**. It wraps open source engines behind an UltraCanvas-owned API so
backings can be swapped without touching callers.

> Status: Stage 1 (Wine tier) implemented — environments, drive mappings,
> launch/supervision. The Stage 2 VM tier (QEMU/KVM + FreeRDP RemoteApp) and
> Stage 3 polish (compatibility routing, gaming profile, ARM64 via Hangover)
> are specified in
> [`Docs/Research/UltraWinDesignProposal.md`](../../Research/UltraWinDesignProposal.md).

Linux / ULTRA OS only: the library is skipped on Windows/macOS builds
(`ULTRACANVAS_ENABLE_ULTRAWIN`, on by default, guarded by platform).

---

## Why it exists

Running Windows software is exactly the kind of concern an OS owns once:
engine discovery, per-app isolation, drive mapping, process supervision,
and (later) VM provisioning and seamless RemoteApp windows. Without a shared
module every app would shell out to `wine` ad hoc — and inevitably leak
prefixes, expose the whole host filesystem through Wine's default `Z:` drive,
or hang on Wine's interactive first-run dialogs.

Two tiers, one API (see the research doc for the full comparison):

| Tier | Engine | Windows license | Coverage |
|---|---|---|---|
| 1 — **Wine** (this stage) | Wine ≥ 10, spawned as child processes | not needed | most apps, instant launch |
| 2 — **VM** (Stage 2) | QEMU/KVM guest + FreeRDP RemoteApp | user-supplied | ~100 % (kernel drivers, anti-cheat) |

Nothing is linked: Wine runs as ordinary child processes, keeping LGPL
outside the framework binaries and letting UltraWin degrade gracefully when
Wine is not installed (`UltraWin_GetCapabilities`).

## Headers

| Header | Contents |
|---|---|
| `UltraWin/UltraWin.h` | umbrella |
| `UltraWin/UltraWinCore.h` | `UltraWinResult`, `UltraWinHandle`, capabilities, config, init/shutdown |
| `UltraWin/UltraWinEnvironment.h` | environments (Wine prefixes) + drive mappings |
| `UltraWin/UltraWinApp.h` | launching and supervising applications |

Conventions match UltraNet: blocking operations return `UltraWinResult`
(`operator bool()` for the success path); app instances are opaque
`UltraWinHandle`s; all names PascalCase with the `UltraWin_` prefix.

## Quick start

```cpp
#include "UltraWin/UltraWin.h"

UltraWin_Initialize();                      // default config is fine

auto caps = UltraWin_GetCapabilities();
if (!caps.wineTierAvailable) {
    // tell the user to install Wine; nothing else to do
    return;
}

// Launch — the "Photofiltre" environment (an isolated Wine prefix) is
// created automatically on first use, with U: -> $HOME mapped and the
// host root NOT exposed.
UltraWinRunOptions opt;
opt.environment = "Photofiltre";
UltraWinHandle app = UltraWinInvalidHandle;
auto r = UltraWin_RunApp("/home/user/Apps/PhotoFiltre.exe", opt, &app);
if (!r) { /* r.code / r.message */ }

// ... the app runs as a native window; supervise as needed:
UltraWin_GetAppState(app);                  // Running / Exited / ...
UltraWin_CloseApp(app);                     // graceful (SIGTERM)
int exitCode = -1;
UltraWin_WaitApp(app, 5000, &exitCode);
UltraWin_ReleaseApp(app);
```

## Environments (Wine prefixes)

- One isolated prefix per application by default (Bottles model), under
  `$XDG_DATA_HOME/ultrawin/environments/<name>` (override:
  `UltraWinConfig::environmentsRoot`).
- Created with `wineboot --init`; Wine's interactive mono/gecko download
  prompts are suppressed (`UltraWinConfig::suppressWinePrompts`).
- Names are directory names: `[A-Za-z0-9._-]+`, max 64 chars, no leading dot.
- `UltraWin_DeleteEnvironment` refuses while apps from it are running
  (`EnvironmentBusy`).

## Drive mappings — the same folders as the native OS

- The user's home directory is mapped under one unified drive letter
  (default `U:`, `UltraWinConfig::homeDriveLetter`) in **every** environment
  — the same letter the Stage 2 VM tier will use, so apps see identical
  paths whichever tier runs them.
- Wine's default `Z:` → `/` (the entire host filesystem) is **removed** by
  default; opt back in with `UltraWinConfig::exposeRootDrive`.
- Additional folders: `UltraWin_MapFolder(env, 'D', "/data")` /
  `UltraWin_UnmapFolder` / `UltraWin_ListMappings`. Mappings persist in the
  environment's manifest (`ultrawin-mappings.conf`) and are re-asserted
  before every launch, because Wine prefix updates recreate the default
  dosdevices entries.
- `C:` (the prefix's virtual Windows drive) and the home letter are
  reserved.

## Running applications

- `UltraWin_RunApp(path, options, &handle)` forks and execs Wine in its own
  process group, with `WINEPREFIX` set and optional extra environment
  variables (e.g. a DXVK switch) applied. The path is routed by extension:
  `.exe` (and anything else) runs directly, `.msi` installs through
  `msiexec /i`, and `.lnk` Start-Menu shortcuts resolve through
  `start /wait /unix` — so installers and installed programs launch through
  the same one call.
- Windows appear as native windows — Wine's default mode; UltraWin never
  enables Wine's "virtual desktop".
- Supervision is polling-based (non-blocking `waitpid`): UltraWin installs
  **no SIGCHLD handler**, so the host application's signal setup is
  untouched.
- `UltraWin_CloseApp` signals the app's process group with SIGTERM
  (graceful), `UltraWin_KillApp` with SIGKILL; `UltraWin_WaitApp` blocks
  with a timeout; ended instances stay queryable until
  `UltraWin_ReleaseApp`.
- `UltraWinRunOptions::forceTier` accepts `Auto` (== Wine in Stage 1) and
  `Wine`; `Vm` returns `NotSupported` until Stage 2.

## Installed programs — feeding the ULTRA OS launcher

Installers create Start-Menu shortcuts inside the environment.
`UltraWin_ListPrograms(env)` enumerates them (all-users and per-user menus,
recursively; `UltraWinProgramInfo` carries name, Start-Menu subfolder as
`category`, and the shortcut's host path), so a launcher or app grid can
show installed Windows programs like native ones — starting one is
`UltraWin_RunApp(info.shortcutPath, …)` via the `.lnk` routing above.
Uninstaller shortcuts are included; filter by name where unwanted.

## Components — VC++ runtimes, fonts, .NET, DXVK

Many Windows applications fail on a bare prefix because they expect runtime
dependencies to be present. `UltraWin_InstallComponent(env, name)` installs
them per environment; names are **winetricks verbs** (`vcrun2019`,
`corefonts`, `dotnet48`, `dxvk`, …), and the installer is a spawned
**winetricks** — probed at runtime exactly like Wine
(`UltraWinCapabilities::winetricksAvailable`, `UltraWinConfig::winetricksPath`
override), never linked.

```cpp
UltraWin_InstallComponent("Photofiltre", "vcrun2019");   // blocking; run off
UltraWin_InstallComponent("Photofiltre", "corefonts");   // the UI thread
for (auto& c : UltraWin_ListComponents("Photofiltre")) { /* installed */ }
```

- The environment is created on first use (like `UltraWin_RunApp`);
  re-installing an installed component succeeds without re-downloading.
- Installs are **blocking and can be slow** (upstream downloads;
  `UltraWinConfig::componentInstallTimeoutSeconds`, default 30 min).
- Failures return `ComponentInstallFailed` with a pointer to the full
  winetricks output, captured in `<prefix>/ultrawin-install.log`.
- UltraWin passes the *resolved ELF wine loader* to winetricks (`WINE=`):
  distro `wine` commands are often script wrappers (Ubuntu alternatives),
  which winetricks' arch detection rejects.

## Capability probing

`UltraWin_GetCapabilities()` is cheap and safe to call at UI-build time:

| Field | Meaning |
|---|---|
| `wineAvailable` / `winePath` / `wineVersion` | usable binary (config override or PATH: `wine`, `wine64`) |
| `winetricksAvailable` / `winetricksPath` | component installer present (config override or PATH) |
| `ntsyncAvailable` | `/dev/ntsync` present (Linux ≥ 6.14 — Wine 11 fast sync) |
| `kvmAvailable` | `/dev/kvm` accessible (Stage 2 readiness) |
| `hostArchitecture` | `x86_64`, `aarch64`, … (on aarch64, PATH finding a Hangover wine build is the supported setup) |
| `wineTierAvailable` / `vmTierAvailable` | tier gates (`vmTierAvailable` is always false in Stage 1) |

## Building and testing

Built by default on Linux (`ULTRACANVAS_ENABLE_ULTRAWIN=ON`) as the static
`UltraWin` library; linked into `libultracanvas` the same way as UltraNet
(whole-archive in shared builds, `ULTRACANVAS_HAS_ULTRAWIN=1`).

```bash
cmake -B build -DULTRACANVAS_BUILD_ULTRAWIN_TESTS=ON
cmake --build build --target UltraWinTests
./build/Tests/UltraWin/UltraWinTests
```

The suite needs **no Wine install**: pure helpers are tested directly, the
launch/supervision path runs against a stub wine script (via the
`UltraWinConfig::winePath` override), and the one test that exercises a real
`wineboot` skips itself when no binary is found.

## UltraWin Manager — the graphical front-end

`UltraWinManager` (built with the framework, `Apps/UltraWinManager/`) is
the desktop UI over this module — assembled entirely from catalogue
elements, with every slow UltraWin call on a worker thread:

- **Environments** — the isolated Wine prefixes: their installed
  components, one-click component installs (winetricks verbs), launching
  a `.exe`/`.msi` into a chosen environment, deletion (busy-guarded).
- **Programs** — installed Start-Menu programs across all environments,
  launchable by double-click or button.
- **Windows VM** — machine state (provisioned/installed, pid, KVM/TCG,
  RDP port, home share), provisioning from install media, and
  start/suspend/resume/stop — the `ultrawin-setup` flow with buttons.

The header always shows the host capability line (Wine, winetricks, QEMU,
KVM, RemoteApp), and missing engines surface as guidance in the status
bar, never as silent failures.

## UltraFiler integration

Double-clicking a `.exe` or `.msi` in UltraFiler launches it through
`UltraWin_RunApp` in a per-app environment named after the file (created
automatically on first launch, off the UI thread; installers run through
msiexec). When Wine is missing, the status bar says how to install it.
Available only in Linux builds (`ULTRACANVAS_HAS_ULTRAWIN`); on other
platforms activation behaves as before.

## The VM tier (Stage 2a — machine backbone)

The fallback tier for the apps Wine cannot run boots a **real Windows
guest** under QEMU/KVM, headless — its desktop is never displayed;
application windows will reach ULTRA OS through FreeRDP RemoteApp over the
forwarded RDP port (Stage 2b). What ships now is the machine's backbone:

- `UltraWin_VmProvision(options)` prepares UltraWin's single shared
  machine under `UltraWinConfig::vmDirectory`: the qcow2 system disk
  (created via qemu-img, sparse, 64 GB cap by default), the unattended
  Windows-setup answer file (`autounattend.xml`: RDP host + RemoteApp
  allow-list enabled, TPM/RAM checks bypassed — **experimental until
  validated against real media**), and the machine manifest. The Windows
  ISO is **user-supplied** (Pro/Enterprise; a Windows license is the
  user's) and can be added by re-provisioning later; re-provisioning never
  recreates an existing disk.
- `UltraWin_VmStart` boots headless with KVM (TCG only via
  `vmAllowWithoutKvm`, for tests), virtio disk/net, the guest's RDP port
  forwarded to loopback (`vmRdpHostPort`, default 13389), and a QMP
  control socket in the machine directory; it returns once QMP answers.
  While install media is configured and setup has not completed, the
  machine boots from the ISO with the answer file attached as a virtual
  FAT volume.
- `UltraWin_VmSuspend` / `UltraWin_VmResume` pause the vCPUs (QMP
  stop/cont) — the cheap way to keep the guest resident between launches;
  `UltraWin_VmStop` is a graceful ACPI powerdown with a hard-quit
  fallback, `UltraWin_VmKill` the virtual power cord.
  `UltraWin_VmGetState` / `UltraWin_VmGetInfo` report
  NotProvisioned/Stopped/Running/Suspended plus pid, disk and RDP port.
- Engines stay wrapped: QEMU is **spawned, never linked** (same policy as
  Wine/winetricks); QMP is spoken directly over its UNIX socket (design
  decision: no libvirt daemon dependency), with JSON handled by the
  vendored yyjson engine the framework already ships.
- Capabilities: `qemuAvailable`/`qemuPath`, `virtiofsdAvailable`, and
  `vmTierAvailable` (= QEMU present **and** KVM usable — provisioning
  state is `UltraWin_VmGetInfo`'s business).

## RemoteApp sessions (Stage 2b-i)

The bridge between the running guest and the desktop: **FreeRDP** — the
one UltraWin engine that IS linked (Apache 2; version-adaptive over
FreeRDP 3, falling back to FreeRDP 2 where 3 is not packaged; optional —
without it `UltraWinCapabilities::remoteAppSupported` is false and VM-tier
launches report `NotSupported`).

- `UltraWin_RunApp(..., forceTier = Vm)` now launches through a RemoteApp
  (RAIL) session against the guest's forwarded RDP port: the guest runs
  the program and exports its windows — never a desktop. Sign-in uses
  `UltraWinConfig::vmGuestUsername/vmGuestPassword` (defaults match the
  account the provisioning answer file creates).
- Stage 2b accepts **guest paths** (`C:\...`) and RemoteApp aliases
  (`||name`); host paths follow with the virtiofs shared-folder
  integration.
- Supervision maps onto the session: `Running` while connected,
  `CloseApp`/`KillApp` end the session, `WaitApp`/`GetAppState` behave as
  in the Wine tier.
- The machine must be `Running` (`VmNotRunning` otherwise); drive
  redirection over RDP is deliberately off — folders come via virtiofs.

## Shared home over virtiofs (Stage 2b-ii)

The VM tier meets the same-folders requirement the same way the Wine tier
does — one unified home drive:

- With `UltraWinConfig::vmShareHome` (default on) and a virtiofsd binary
  on the host, `UltraWin_VmStart` spawns **virtiofsd** exporting `$HOME`
  and attaches it as a `vhost-user-fs` device with tag `ultrawin_home`
  (shared-memfd memory backend; `UltraWinVmInfo::homeShared` reports the
  live state, and the daemon's lifetime is tied to the machine's).
- `UltraWin_RunApp(forceTier = Vm)` accepts **host paths under the home
  directory** and translates them to the guest spelling
  (`/home/u/Apps/X.exe` → `U:\Apps\X.exe`, `homeDriveLetter`).
- Guest side: the virtiofs service (WinFsp + `VirtioFsSvc` from the
  virtio-win drivers) mounts the tag as the home drive — installed during
  provisioning once Stage 2c validates against real install media.

## Not yet implemented (later stages)

- Stage 2b-ii: the `UltraCanvasRemoteAppView` element rendering RAIL
  window surfaces as native ULTRA OS windows (needs a real Windows guest
  to validate against).
- Stage 2c: the provisioning pipeline is complete (WinPE virtio driver
  injection, guest-tools install, virtiofs mount, RDP-probe install
  detection, `ultrawin-setup` CLI) but awaits its validation run against
  real install media on a KVM machine — see
  [`VmValidation.md`](VmValidation.md).
- `UltraWin_QueryCompatibility` + automatic tier routing.
