# Running Windows Applications on Linux / ULTRA OS — Open Source Options Research

**Date:** 2026-08-09
**Status:** Research — no implementation yet
**Scope:** Which open source emulators / virtualisers are the best foundation
for a "run Windows apps on ULTRA OS" capability, and how they would fit the
UltraCanvas architecture and dependency policy.

---

## 1. The two fundamentally different approaches

There is no single "Windows emulator" — the ecosystem has converged on two
distinct technologies, and every serious product (Steam Deck, CrossOver,
WinBoat) picks one or combines both:

| | **API translation (Wine family)** | **Full virtualisation (QEMU/KVM)** |
|---|---|---|
| What runs | The app's own x86 code, natively; Windows API calls are translated to Linux calls | A complete, genuine Windows OS in a VM |
| Windows license needed | **No** | **Yes** (user must supply/activate Windows) |
| Compatibility | Very good and improving; fails on kernel drivers, anti-cheat, some installers | Effectively 100% |
| Performance | Near-native (NTSYNC in Wine 11 removed the last big gap) | Near-native CPU via KVM; graphics slower unless GPU passthrough |
| Resources | Light — no second OS | 4–8 GB RAM + 30–60 GB disk for the guest |
| Startup | Instant per-app | VM boot (can be kept resident/suspended) |
| Desktop integration | Native — Wine windows *are* X11/Wayland windows | Needs a bridge (FreeRDP RemoteApp, SPICE, Looking Glass) |

**Conclusion up front:** the best strategy is the industry-standard hybrid —
**Wine as the primary, lightweight tier** and **QEMU/KVM with FreeRDP
RemoteApp integration as the 100%-compatibility fallback tier**. Building a
new emulator from scratch is not a realistic option; the surveyed projects
represent decades of accumulated work (Wine alone is ~30 years old).

---

## 2. Candidates surveyed

### 2.1 Wine 11.0 — API translation layer (recommended, tier 1)

- **License:** LGPL-2.1-or-later (compatible with linking/embedding; we would
  invoke it as a child process anyway).
- **State (Jan 2026):** Wine 11.0 shipped 2026-01-13 with 6,300+ changes:
  - **NTSYNC** kernel-side NT synchronisation primitives (Linux ≥ 6.14) —
    massive multithreaded performance gains (benchmarks show 2–7× in
    sync-heavy workloads).
  - **New WoW64 mode complete** — 32-bit (even 16-bit) Windows apps run
    without 32-bit Linux libraries; important for a modern 64-bit-only
    ULTRA OS userland.
  - **Wayland driver** enabled by default since 10.0, clipboard support in 11.0
    — no XWayland dependency.
  - **ARM64EC / ARM64X support** — foundation for ARM builds (see Hangover).
  - Vulkan H.264 decoding, HiDPI scaling, FFmpeg multimedia backend.
- **Why it wins tier 1:** no Windows license, no VM overhead, per-app launch,
  windows appear as first-class native windows with no bridge layer. Its
  weaknesses (kernel drivers, anti-cheat, exotic installers) are exactly what
  tier 2 covers.

### 2.2 Proton / DXVK / vkd3d-proton — gaming-tuned Wine

- **Licenses:** Proton patches BSD-3-Clause; DXVK zlib; vkd3d-proton LGPL.
- Valve's Wine distribution with Direct3D 9/10/11→Vulkan (DXVK) and
  D3D12→Vulkan (vkd3d-proton). Usable outside Steam via `umu-launcher`.
- **Relevance:** if ULTRA OS targets games, ship DXVK/vkd3d-proton inside the
  Wine tier rather than adopting Proton wholesale (Proton assumes the Steam
  runtime container).

### 2.3 QEMU + KVM (+ libvirt) — full virtualisation (recommended, tier 2)

- **Licenses:** QEMU GPL-2.0 (separate process — no license impact on
  UltraCanvas code); libvirt LGPL-2.1 (linkable management API); virtio-win
  guest drivers are open source (Red Hat).
- **State:** QEMU 10.x; KVM gives near-native CPU/memory performance;
  **virtio** paravirtual drivers for disk/net/GPU; **virtiofs** is now the
  recommended host-file-sharing mechanism for Windows guests.
- **Why it wins tier 2 over alternatives:** kernel-native (KVM ships in every
  Linux kernel — nothing to maintain), fully scriptable via QMP/libvirt,
  no out-of-tree kernel modules, no corporate-controlled governance.

### 2.4 The seamless-integration layer: FreeRDP + Windows RemoteApp (the WinBoat pattern)

The missing piece that makes a VM feel native. **WinBoat** (MIT, 2025) and
**WinApps** proved the pattern:

1. Windows guest runs under QEMU/KVM (WinBoat wraps it in Docker via the
   MIT-licensed `dockur/windows` image, which automates Windows download +
   unattended install — legally the *user* still needs a Windows license).
2. Windows' built-in **RemoteApp** feature exports individual application
   windows over RDP instead of a full desktop.
3. **FreeRDP** (Apache-2.0 — ideal license, embeddable C library) renders
   each Windows app as a separate window on the Linux desktop, with
   clipboard, audio, and drive redirection.

**Relevance for ULTRA OS:** FreeRDP's client library can be wrapped behind an
UltraCanvas-owned API and rendered *inside an UltraCanvas element*, so Windows
apps would appear as native ULTRA OS windows — a deeper integration than
WinBoat's Electron shell achieves.

### 2.5 Hangover 11.0 + FEX-Emu / Box64 — the ARM64 answer

- **Licenses:** Hangover follows Wine (LGPL); FEX-Emu MIT; Box64 MIT.
- Hangover 11.0 (Jan 2026, tracks Wine 11) runs **x86/x64 Windows apps on
  ARM64 Linux** by emulating only the application code (via FEX or Box64)
  while Wine itself runs natively — ~60–80 % native CPU performance, 2–3×
  faster than QEMU usermode. Now only ~10 patches on top of upstream Wine,
  i.e. converging into Wine proper. Valve's Steam Frame uses the same
  FEX-based approach.
- **Relevance:** if ULTRA OS ever targets ARM64 hardware, this is the only
  viable open source path; it slots into the same Wine tier.

### 2.6 Rejected options

| Option | License | Why rejected |
|---|---|---|
| **VirtualBox** | GPL-3.0 core + proprietary extension pack | Out-of-tree kernel modules to maintain, Oracle governance, weaker automation story than libvirt/QMP, extension pack licensing traps. KVM is strictly better on Linux. |
| **Xen** | GPL-2.0 | Server hypervisor; requires booting the Xen microkernel below the host OS — wrong architecture for a desktop feature. |
| **Bochs** | LGPL | Pure interpreter, orders of magnitude too slow for modern Windows. |
| **86Box / PCem / DOSBox-X** | GPL-2.0 | Cycle-accurate *retro* PC emulators (DOS/Win9x era). Only relevant if ULTRA OS wants a "legacy software" feature; not for modern apps. |
| **Writing our own emulator** | — | Wine ≈ 30 years / millions of LOC of API reimplementation; QEMU similar scale. Not achievable; wrap, don't rewrite. |

### 2.7 Optional add-ons (later phases)

- **Looking Glass** (GPL-2.0): near-zero-latency framebuffer sharing for VMs
  with GPU passthrough — the gaming/pro-graphics endgame for tier 2, but
  requires a second GPU/SR-IOV; keep as an advanced option.
- **Bottles** (GPL-3.0, Python/GTK): not embeddable in C++, but the best UX
  reference for Wine prefix management (per-app isolated prefixes,
  dependency installers, versioned runners).

---

## 3. Recommended architecture for ULTRA OS

Following the house rule that public engines are always wrapped behind an
UltraCanvas-owned API (AGENTS.md — "Wrapped engines"), the capability would be
a new module, e.g. **UltraWin**:

```
┌─────────────────────────────────────────────────────────┐
│  UltraWin public API (UltraCanvas-owned, engine-neutral)│
│  RunWindowsApp(path) → picks best backend automatically │
├───────────────────────────┬─────────────────────────────┤
│ Tier 1: Wine backend      │ Tier 2: VM backend          │
│ - wine/wineserver spawned │ - QEMU/KVM guest via libvirt│
│   as child processes      │   (or QMP directly)         │
│ - per-app prefixes        │ - virtio disk/net, virtiofs │
│   (Bottles-style)         │   file sharing              │
│ - DXVK/vkd3d for games    │ - FreeRDP (Apache-2.0) lib  │
│ - Hangover on ARM64       │   wrapped as an UltraCanvas │
│ - windows appear natively │   element per RemoteApp     │
└───────────────────────────┴─────────────────────────────┘
```

Key design points:

1. **Process isolation = license isolation.** Wine (LGPL) and QEMU (GPL-2.0)
   run as separate processes; only FreeRDP (Apache-2.0) and optionally
   libvirt (LGPL) are linked, both compatible with the framework's licensing.
2. **Tier selection is automatic**: try Wine first (instant, free); offer the
   VM tier when an app is known-incompatible (a small compatibility database,
   seeded from WineHQ AppDB ratings, decides).
3. **The VM is one shared, suspendable guest**, not one VM per app — RemoteApp
   multiplexes many app windows over one session.
4. **UltraCanvas integration**: an `UltraCanvasRdpView`-style element hosts
   each RemoteApp surface, giving Windows apps ULTRA OS window chrome,
   clipboard, and file-dialog bridging.
5. **Windows licensing caveat**: tier 2 requires the *user* to provide a
   licensed Windows image. Tier 1 (Wine) has no such requirement — another
   reason it must be the default.

## 4. Suggested phasing

1. **Phase 1 — Wine tier:** bundle/detect Wine 11, prefix manager, launch +
   lifecycle API, `.exe` association in UltraFiler. Lowest effort, biggest
   coverage.
2. **Phase 2 — VM tier:** QEMU/KVM guest provisioning (dockur/windows-style
   unattended install), FreeRDP RemoteApp element, shared folders via
   virtiofs.
3. **Phase 3 — polish:** compatibility database + automatic tier routing,
   DXVK/gaming profile, ARM64 via Hangover, optional Looking Glass path.

---

## 5. Sources

- Wine 11.0 release (NTSYNC, WoW64, Wayland): phoronix.com/news/Wine-11.0-Tomorrow, 9to5linux.com/wine-11-officially-released-with-ntsync-support-vulkan-h-264-decoding-and-more
- Wine 10.0 (Wayland default, ARM64EC): phoronix.com/news/Wine-10.0-Released
- Hangover 11.0 (Wine + FEX/Box64 on ARM64): phoronix.com/news/Hangover-11.0-Released, github.com/AndreRH/hangover
- FEX-Emu: fex-emu.com
- WinBoat architecture (KVM-in-Docker + FreeRDP RemoteApp, MIT): winboat.app, windowsforum.com/threads/winboat-run-real-windows-apps-on-linux-with-kvm-in-docker-and-remoteapp.391795/
- KVM vs QEMU roles: northflank.com/blog/kvm-vs-qemu
- QEMU/KVM Windows 11 guest + virtio/virtiofs state: technologytales.com/windows-11-virtualisation-on-linux-using-kvm-and-qemu
- 2026 state-of-the-ecosystem overviews: linuxnest.com/the-2026-state-of-running-windows-applications-on-linux, botmonster.com/self-hosting/run-windows-apps-linux-bottles-proton-2026
