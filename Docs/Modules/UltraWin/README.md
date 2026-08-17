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

- `UltraWin_RunApp(exePath, options, &handle)` forks and execs
  `wine <exe> [args...]` in its own process group, with `WINEPREFIX` set and
  optional extra environment variables (e.g. a DXVK switch) applied.
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

## Capability probing

`UltraWin_GetCapabilities()` is cheap and safe to call at UI-build time:

| Field | Meaning |
|---|---|
| `wineAvailable` / `winePath` / `wineVersion` | usable binary (config override or PATH: `wine`, `wine64`) |
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

## UltraFiler integration

Double-clicking a `.exe` in UltraFiler launches it through
`UltraWin_RunApp` in a per-app environment named after the executable
(created automatically on first launch, off the UI thread). When Wine is
missing, the status bar says how to install it. Available only in Linux
builds (`ULTRACANVAS_HAS_ULTRAWIN`); on other platforms activation of an
`.exe` behaves as before.

## Not yet implemented (later stages)

- `UltraWin_InstallComponent` (fonts, VC++ runtimes, DXVK — winetricks
  equivalent), `UltraWin_QueryCompatibility` + automatic tier routing.
- The whole VM tier: `UltraWin_VmProvision/Start/Suspend/Stop`, virtiofs
  shared folders, `UltraCanvasRemoteAppView` RAIL element.
