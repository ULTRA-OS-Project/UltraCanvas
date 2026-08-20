# Display Stack for ULTRA OS: X11 vs Wayland vs Custom — Research

**Date:** 2026-08-20
**Status:** Research — recommendation, no implementation yet
**Scope:** Which display architecture should ULTRA OS build on for the best
combination of speed, memory footprint, capability, and application
compatibility: X11, Wayland, or a fully custom low-level windowing system?
Companion to [LinuxMouseBehaviorResearch.md](LinuxMouseBehaviorResearch.md),
which already concluded that ULTRA OS must own the window-management layer
to deliver RISC OS-style interaction.

---

## 1. Reframing the question: it is a choice of *protocol*, not of *stack*

On modern Linux every serious display system — Xorg, every Wayland
compositor, and any custom system we could write — sits on the **same
kernel-level foundation**:

- **DRM/KMS** for display controller programming (modes, planes, page
  flips, vsync)
- **GBM/EGL or Vulkan** for GPU buffer allocation and rendering
- **libinput** (over evdev) for keyboards, mice, touchpads, tablets

There is no lower level to drop to without writing GPU drivers. So "our
own low-level window support" would *also* be a process that opens DRM/KMS
and libinput — exactly what a Wayland compositor is. The only real
decision is **which protocol that process speaks to applications**:

| Choice | Process architecture | Protocol to apps |
|---|---|---|
| X11 | Xorg server **+** separate WM **+** (usually) separate compositor | X11 |
| Wayland | **One** process: compositor = display server + WM + compositor | Wayland (+ XWayland for X11 apps) |
| Custom | One process (same as Wayland architecturally) | A protocol only ULTRA OS apps speak |

This reframing decides most of the question: **a Wayland compositor *is*
"our own low-level windowing system"** — ULTRA OS owns mode-setting,
input, scheduling, stacking, focus, effects, and all policy — that happens
to speak a standard wire protocol to clients so that the entire Linux
application ecosystem (GTK, Qt, Electron, SDL, Java, Flutter, Wine) works
unmodified.

---

## 2. Option A — X11 / Xorg

### Why it fails the "new OS in 2026" test

- **Xorg is in maintenance mode.** No significant feature development for
  years; the people who maintained it now build Wayland. Red Hat dropped
  the Xorg session in RHEL 10; Fedora GNOME/KDE are Wayland-only; Ubuntu
  defaults to Wayland. Building a *new* OS on it means adopting an
  end-of-life codebase as a foundation.
- **Structurally slower and heavier.** Three processes (Xorg + WM +
  compositing manager) where Wayland has one; every input event and every
  frame crosses extra process boundaries; windows are drawn, copied to the
  server, then composited — an extra copy per frame that Wayland's direct
  buffer passing (dmabuf) avoids. X11 cannot guarantee tear-free,
  every-frame-perfect output; Wayland does by design.
- **No isolation.** Any X11 client can read all keyboard input, capture
  any window's content, and inject events into any other client. For a
  consumer OS in 2026 this is a real liability.
- The one genuine X11 advantage — its permissiveness made the RISC OS
  mouse behaviors easy to bolt on (see companion doc) — is matched by a
  compositor we own, where those behaviors are first-class policy code.

**Verdict:** wrong foundation for ULTRA OS. X11 should survive only as a
*compatibility client protocol* via XWayland (§3), and as UltraCanvas's
current interim backend for running on today's desktops.

## 3. Option B — Wayland (own compositor)

### Efficiency

A Wayland compositor is the leanest architecture Linux offers short of a
single fullscreen app on KMS:

- **One process** does input, policy, and output. Minimal compositors
  built on wlroots (Sway, Cage) idle in the low tens of MB of RAM and are
  routinely run on Raspberry Pi-class hardware; the protocol overhead per
  frame is a few messages over a unix socket.
- **Zero-copy path:** clients render with the GPU and pass dmabuf handles;
  the compositor can put a fullscreen or unoccluded client buffer on a
  hardware plane (**direct scanout**), meaning the compositor touches the
  pixels *not at all* — better than X11 can ever do, and identical to what
  a custom stack would do.
- **Input latency** is lower than X11's (no server→WM→client round trips
  for focus/grab decisions; libinput events go straight from the
  compositor's event loop to the focused client).

### Control

Everything ULTRA OS needs to feel like its own OS is compositor-side and
therefore fully ours: stacking and focus policy (Adjust-click semantics),
window furniture, iconbar/pinboard-style shell, animations, per-surface
input rewriting for foreign apps, screen capture policy, security policy.
Where the standard protocol is not enough, **Wayland is explicitly
extensible**: ULTRA OS can ship private `ultra_*` protocol extensions that
UltraCanvas-native apps use for RISC OS-faithful features (menu-on-release
semantics, Adjust-scroll, pinboard integration), while foreign apps use
the standard core protocol. This is the sanctioned mechanism — every
desktop (GNOME, KDE, wlroots ecosystem) ships its own extensions.

### Compatibility

- Native Wayland: all current GTK, Qt, SDL, Electron, Flutter, and (since
  Wine 9/10) Wine apps.
- **XWayland** covers everything X11-only. It is one client process,
  spawned on demand, and — critically — it receives *its* input from our
  compositor, so ULTRA OS policies apply to X11 apps automatically.

### Implementation base (build vs. use a library)

Writing a compositor on raw `libwayland-server` + DRM/KMS is ~1–2 years of
plumbing before policy work starts. Three mature libraries remove that:

| Library | Language | Maturity / users | Fit for ULTRA OS |
|---|---|---|---|
| **wlroots** | C | The ecosystem standard — Sway, Hyprland (originally), Cage, dozens more; XWayland, all major protocols, DRM leasing, direct scanout all solved | **Safe default.** Largest community, most hardware exposure; C API links cleanly into C++20 |
| **Louvre** | C++ | Younger, smaller community; benchmarks very well; designed to make compositors easy | Attractive language fit for this codebase; risk = bus factor and less hardware battle-testing |
| **Smithay** | Rust | Cosmic (System76) is built on it | Rules itself out unless ULTRA OS wants Rust in the core |

Recommendation: **prototype on wlroots**, keep the ULTRA OS policy layer
cleanly separated behind our own C++ interface so the base library remains
swappable (Louvre later, or raw libwayland if we ever outgrow wlroots).

**Verdict: the correct choice.** Equal footprint and speed to a custom
stack (same architecture, same kernel APIs), full policy control, entire
app ecosystem for free, and a maintained upstream.

---

## 4. Option C — fully custom protocol

What it would actually buy versus a Wayland compositor:

- **Performance: nothing measurable.** The custom process would use the
  same DRM/KMS, GBM/EGL, libinput. Wayland's per-frame protocol cost is a
  handful of socket messages; that is not where frames are won or lost.
- **Footprint: nothing meaningful.** libwayland-server is a small C
  library; the memory in any compositor is buffers and GPU state, which a
  custom stack needs identically.

What it would cost:

- **The entire application ecosystem.** No GTK, Qt, Electron, Firefox,
  Wine app runs — until we implement… a Wayland and/or X11 compatibility
  server on top, at which point we have built a Wayland compositor with
  extra steps. (This also undercuts the companion research on Windows-app
  support: Wine targets X11/Wayland.)
- **Years of solved-problem re-engineering:** multi-GPU, hotplug, HiDPI
  and mixed-DPI, fractional scaling, color management/HDR, screen
  capture, accessibility, IME, clipboard/DnD, tablet/touch, VRR, session
  locking… each already done and maintained in the Wayland ecosystem.

The legitimate kernel of this idea — "ULTRA OS should have its *own*
windowing semantics" — is fully served by **owning the compositor and
shipping private protocol extensions** (§3). RISC OS itself is the
cautionary tale here: a wholly custom stack means carrying the whole
stack alone, forever.

**Verdict: rejected** as a wholesale approach; adopted in spirit as
`ultra_*` extensions on Wayland.

---

## 5. Summary comparison

| Criterion | X11 (Xorg) | **Wayland (own compositor)** | Custom protocol |
|---|---|---|---|
| Frame path | draw → copy to server → composite (extra copy) | dmabuf pass or **direct scanout** (zero-copy) | same as Wayland |
| Processes in the hot path | 3 (server, WM, compositor) | **1** | 1 |
| Idle RAM for the display layer | Xorg + WM + compositor, typically well above a lean compositor | **Low tens of MB** (wlroots-class) | comparable, after years of work |
| Tear-free / every-frame-perfect | not guaranteed | **guaranteed by design** | must be built |
| RISC OS interaction policies | possible (companion doc) | **first-class, one place** | first-class |
| App compatibility | all legacy, shrinking future | **everything** (native + XWayland) | none without a compat layer |
| Upstream maintenance | maintenance mode / declining | **active, industry standard** | ULTRA OS alone |
| Security/isolation | none between clients | **strong, compositor-enforced** | must be designed |

## 6. Recommendation and path for UltraCanvas

**Build ULTRA OS on Wayland with its own compositor (wlroots-based to
start), XWayland enabled for legacy apps, and private `ultra_*` protocol
extensions for native features. Do not adopt Xorg as a foundation; do not
build a custom protocol.**

Concrete steps on the UltraCanvas side:

1. **Keep the current Xlib backend** (`OS/Linux/`) short-term — it runs
   unchanged under XWayland, including on the future ULTRA OS compositor.
2. **Add a native Wayland client backend** (already on the README
   roadmap): `xdg-shell` windowing + `wl_surface`, reusing the existing
   EGL context manager (`GLContextManagerEGL_Linux.cpp`, which is
   display-agnostic) and Cairo/GL rendering; clipboard, DnD and cursor
   modules need Wayland counterparts.
3. **Start the compositor as a separate module/repo** consuming wlroots,
   with ULTRA OS policy (stacking, focus, Select/Menu/Adjust rewriting,
   shell surfaces for iconbar/pinboard) behind a C++ interface so the
   base library remains replaceable.
4. Native UltraCanvas apps later negotiate `ultra_*` extensions with the
   compositor to get faithful RISC OS semantics, per the companion doc.
