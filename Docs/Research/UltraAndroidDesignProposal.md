# UltraAndroid — Android Application Compatibility Module (Design Proposal)

**Date:** 2026-09-04
**Status:** Proposal — for review; no implementation yet
**Companion:** [`Docs/UltraCanvas/AndroidOnLinuxInvestigation.md`](../UltraCanvas/AndroidOnLinuxInvestigation.md)
(the survey that selected the runtimes wrapped here)
**Sibling in shape:** [`UltraWinDesignProposal.md`](UltraWinDesignProposal.md) —
UltraAndroid is to Android what UltraWin is to Windows, and deliberately mirrors
its rules, its API conventions and its licensing posture.

---

## 1. Purpose

UltraAndroid lets ULTRA OS run Android applications **as single native windows —
never an Android home screen — with the user's own folders visible to the
apps**. It is a sibling module of UltraWin/UltraNet/UltraAI: an UltraCanvas-owned
API wrapping open source runtimes so backends can be swapped without touching
callers.

Two backends behind one API, selected by what the host can actually provide:

| Tier | Engine | When used |
|---|---|---|
| 1 (default) | **Waydroid-class container** — Android userspace in LXC on the *host* kernel, GPLv3, spawned | Any host whose kernel provides binder. Near-native speed, real GPU driver, no VM |
| 2 (fallback) | **VM** — Cuttlefish (crosvm) or the SDK emulator, both QEMU/KVM-class, spawned | Hosts without binder in the kernel, or where container-level isolation is not wanted |

**UltraAndroid rules** (mirroring the UltraWin registry entry style):

- Clear structure; function names understandable on sight (PascalCase).
- Blocking operations return `UltraAndroidResult`; runtime, app and share
  instances are opaque `UltraAndroidHandle`s.
- **No Android home screen, launcher or notification shade is ever displayed.**
  Every Android app window is a native ULTRA OS window (tier 1: the container's
  multi-window mode, one host surface per activity; tier 2: the same, never a
  full-device viewer path).
- The user's folders are visible to Android apps at the **same mount point in
  both tiers**, and the host's file manager sees the app's shared storage back.
- **No engine is ever linked.** The container manager, LXC, `adb` and QEMU run
  strictly as separate processes, keeping GPLv3/GPLv2 outside the framework
  binaries and letting UltraAndroid degrade gracefully when nothing is installed
  (`UltraAndroid_GetCapabilities`).
- **No Google Play, GMS or Play Services is ever bundled**, and no ARM
  translation layer is ever redistributed (§5). Both are user-supplied choices
  that the module detects and reports, never ships.
- Never expose Waydroid, LXC, `adb` or QEMU types in public headers.

### 1.1 What this is *not*

UltraAndroid runs **other people's Android apps on Linux**.
`UltraCanvas/OS/Android/` runs **our apps on Android**. Opposite directions,
no shared code, and the names are close enough that the registry entry says so
explicitly.

They do meet at exactly one point, and it is a useful one: the runtimes
UltraAndroid provisions in tiers 1 and 2 are precisely the test beds the
UltraCanvas Android backend needs in order to run for the first time
([investigation §4](../UltraCanvas/AndroidOnLinuxInvestigation.md)). Whichever is
built first pays part of the other's cost.

---

## 2. Proposed public function surface

Lifecycle & capability discovery:

- `UltraAndroid_Initialize`, `UltraAndroid_Shutdown`, `UltraAndroid_IsInitialized`
- `UltraAndroid_GetCapabilities` — which tiers this host can run, whether binder
  is present, whether a translation layer is installed, guest ABIs available
- `UltraAndroid_GetConfig`, `UltraAndroid_SetConfig`, `UltraAndroid_GetVersion`

Runtime session (the single shared container or VM — the analogue of UltraWin's
environments, except there is normally exactly one):

- `UltraAndroid_StartRuntime`, `UltraAndroid_StopRuntime`
- `UltraAndroid_GetRuntimeState`, `UltraAndroid_GetRuntimeInfo`

System images:

- `UltraAndroid_ListImages`, `UltraAndroid_InstallImage`,
  `UltraAndroid_RemoveImage`, `UltraAndroid_GetImageInfo`

Folder sharing (the `U:` drive of this module):

- `UltraAndroid_ShareFolder`, `UltraAndroid_UnshareFolder`,
  `UltraAndroid_ListShares`

Applications:

- `UltraAndroid_InstallApk`, `UltraAndroid_UninstallApp`
- `UltraAndroid_ListApps`, `UltraAndroid_GetAppInfo` (package, launcher label,
  icon, ABIs, target SDK — what an ULTRA OS launcher shows)
- `UltraAndroid_RunApp`, `UltraAndroid_CloseApp`, `UltraAndroid_KillApp`,
  `UltraAndroid_GetAppState`, `UltraAndroid_WaitApp`, `UltraAndroid_ReleaseApp`

Inspection and routing:

- `UltraAndroid_InspectApk` — package name, label, icon, `minSdk`/`targetSdk`
  and the native ABIs present, read straight out of the APK **without a runtime
  installed at all**. This is the one call that always works, and the one
  UltraFiler needs in order to describe an `.apk` the user is looking at.
- `UltraAndroid_QueryCompatibility` — given an APK and the current host, answers
  *runs natively* / *needs translation (present or absent)* / *needs a different
  tier* / *cannot run*. The ABI question of §5, asked once, in one place.

---

## 3. Architecture

```
     UltraCanvas app  ·  UltraFiler  ·  ULTRA OS launcher
                          |
                 UltraAndroid public API           (this module)
                          |
          +---------------+----------------+
          |                                |
   session manager                  control transport
   (tier 1: container CLI           (adb, spawned — install, launch,
    tier 2: cvd / emulator)          enumerate, state; both tiers)
          |                                |
   host kernel binder + LXC          the running Android userspace
   or KVM + virtual device
```

Two decisions carry the design:

1. **One control transport, two session managers.** `adb` exists in both tiers
   and speaks package install, activity launch, enumeration and state. Only the
   session lifecycle (start/stop/provision) differs, so only that part is
   tier-specific. This keeps the tier-2 fallback cheap rather than a second
   implementation.
2. **`UltraAndroid_InspectApk` never touches a runtime.** An APK is a zip, and
   VirtualFS already reads zips; only the binary `AndroidManifest.xml` needs a
   small AXML decoder. So file-manager integration — "what is this `.apk`, and
   would it run here?" — works on a machine with no Android runtime installed,
   which is the common case and the first thing a user meets.

Windows never appear from the runtime's launcher: apps are started by explicit
activity launch, and the container is configured for one host surface per
activity.

---

## 4. Platform matrix

| Host | Tier 1 (container) | Tier 2 (VM) | Notes |
|---|---|---|---|
| ULTRA OS / Linux x86_64 | Yes, with binder in the kernel | Yes, with KVM | The target |
| ULTRA OS / Linux arm64 | Yes | Yes | Native for arm64 Android apps — no translation needed |
| Linux without binder | No | Yes | Why tier 2 exists |
| Windows / macOS | No | Out of scope | Module skipped at build time, as UltraWin is |
| Android itself | Not applicable | Not applicable | Meaningless on the platform it emulates |

Wayland is effectively required for tier 1; an X11-only session is a
second-class citizen.

---

## 5. Decisions proposed (for review)

1. **Waydroid-class container as the default tier**, VM as the fallback — not
   the other way round. The container uses the real GPU driver and no
   virtualization, which is what makes app windows feel native rather than
   remote.
2. **ULTRA OS commits to binder in its kernel config.** On mainstream 6.x
   kernels it is usually built in; if ULTRA OS ships its own config this becomes
   a guaranteed feature rather than an inherited accident. Without it every
   install falls to tier 2 and needs KVM instead.
3. **Never redistribute an ARM translation layer.** libndk/libhoudini are
   extracted from other vendors' Android distributions; their licensing does not
   permit us to ship them, their quality varies by CPU vendor, and an app that
   fails through one is indistinguishable from an app that fails through our
   code. `UltraAndroid_GetCapabilities` detects one the user installed and
   `UltraAndroid_QueryCompatibility` reports honestly when an APK needs it.
4. **Never bundle GMS/Play.** Distributing Google's services requires
   certification we do not have. Ship a vanilla image; if a user wants GApps or
   microG, that is their action, outside the module.
5. **One runtime session, not one per app.** UltraWin isolates apps in separate
   Wine prefixes because prefixes are cheap and apps corrupt them. An Android
   container is not cheap and Android already isolates apps from each other, so
   the default is a single shared session — with `UltraAndroid_StartRuntime`
   accepting a named session for the cases that need separation.
6. **The user's home is shared, not copied.** Both tiers mount the same host
   folders at the same guest path, so "open the file I am looking at" works
   without a copy step — unlike SAF's copy-to-cache behaviour that the
   UltraCanvas Android backend has to live with in the other direction.

---

## 6. New third-party dependencies (when implementation starts)

Nothing is linked. Everything is detected and spawned:

| Component | Role | Licence | How used |
|---|---|---|---|
| Waydroid (+ LXC) | Tier 1 session | GPLv3 / LGPL | Spawned CLI; detected, never bundled |
| Android platform-tools (`adb`) | Control transport, both tiers | Apache-2.0 | Spawned |
| Cuttlefish / QEMU | Tier 2 session | Apache-2.0 / GPLv2 | Spawned |
| System image | The Android userspace | Apache-2.0 (AOSP-derived) | Downloaded by the image manager, or user-supplied |
| AXML decoder | `UltraAndroid_InspectApk` | — | Small in-tree decoder; the zip side reuses VirtualFS |

The pattern matches UltraWin exactly: GPL engines stay in separate processes,
capability probing replaces hard dependencies, and the module reports itself as
unavailable rather than failing to build.

---

## 7. Phasing

**Stage 1 — useful without a runtime**
- Module skeleton, config, `UltraAndroid_GetCapabilities` host probe (binder,
  KVM, container manager, `adb`, translation layer, guest ABIs).
- `UltraAndroid_InspectApk` + `UltraAndroid_QueryCompatibility`, on VirtualFS's
  zip reader plus an AXML decoder.
- UltraFiler: `.apk` gets a real description and a "would this run here" answer.

**Stage 2 — tier 1**
- Image install, `StartRuntime`/`StopRuntime`, folder shares.
- `InstallApk`, `ListApps`, `RunApp` as single native windows, app supervision.

**Stage 3 — tier 2 and integration**
- VM tier for hosts without binder, behind the same API.
- Launcher entries, per-app windows in the ULTRA OS task list, audio routing,
  clipboard between host and guest.

Stage 1 is worth landing on its own: it is pure host-side code, it needs no
Android runtime to test, and it is the part every ULTRA OS user without a
container installed still benefits from.

---

## 8. Open questions

- **Image provenance.** Does ULTRA OS build and sign its own system image
  (control, size, maintenance) or consume a LineageOS-derived one (free, but a
  moving base and someone else's release cadence)?
- **Multi-window fidelity.** Container multi-window mode is good, not perfect —
  dialogs, IME placement and popup windows are where it shows. How much does
  ULTRA OS paper over, and how much is upstream work?
- **Audio.** Routing guest audio to PipeWire/PulseAudio per app window, so
  volume control is per ULTRA OS window rather than per container.
- **Clipboard and drag-and-drop** between host and guest, in both directions.
- **Notifications.** Android apps expect a notification shade; ULTRA OS has its
  own. Bridge them, or drop them?
- **Hybrid graphics.** Which GPU the container renders on, on laptops with two.
- **Does tier 2 ever ship?** It doubles the surface for hosts that may be rare
  once binder is a kernel commitment (§5.2). Possibly Stage 3, possibly never —
  though it is also the tier that doubles as the UltraCanvas Android backend's
  CI test bed, which may pay for it on its own.
