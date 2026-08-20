# RISC OS-Style Mouse Behavior for Unmodified Linux Applications — Research

**Date:** 2026-08-20
**Status:** Research — no implementation yet
**Scope:** Can ULTRA OS make *all existing* Linux applications (GTK, Qt,
Electron, Java, …) follow the RISC OS three-button mouse model on X11 and
Wayland **without recompiling them**? Specifically:

1. **Adjust-click focuses without raising** — a window clicked with the
   right button gets input focus but does *not* come to the front.
2. **Menu on middle click** — the context menu opens on the *middle*
   button, not the right one.
3. **Adjust adds to selections** — right-clicking adds/toggles items in an
   existing selection instead of opening a menu.

---

## 1. The key insight: the three behaviors live in three different layers

Whether a behavior can be imposed on unmodified applications depends
entirely on *which layer of the stack owns it*:

| # | Behavior | Owned by | Imposable without recompiling? |
|---|---|---|---|
| 1 | Focus without raise | **Window manager / compositor** (server-side policy) | **Yes, 100%** — apps have no say |
| 2 | Menu on middle button | **Toolkit convention** ("Button 3 opens menus") inside each app | **Yes, ~100%** — by rewriting the button *code* before the app sees it |
| 3 | Adjust adds to selection | **Widget logic** inside each app (list views, text areas, canvases) | **Only approximable** — by synthesizing modifier+left-click; never 100% faithful |

So the initial intuition ("likely not possible") is right only for #3.
Behaviors #1 and #2 are fully achievable for every application, because
they can be implemented *below* the application: #1 is decided by the
window manager/compositor and #2 only requires that the application
receive a different button number than the one physically pressed.

---

## 2. Behavior 1 — focus without raise on Adjust-click

### X11

Focus and stacking order are exclusively window-manager policy. No
application raises itself on click — the WM does it for them (apps *can*
call `XRaiseWindow`, but almost none do; the "click brings to front"
behavior everyone sees is the WM's click-to-focus handler).

Implementation in an ULTRA OS window manager (or a patched/configured
existing one):

- Passive-grab the mouse buttons on every client frame with
  `XGrabButton(dpy, Button3, AnyModifier, frame, …, GrabModeSync, …)`.
- On `ButtonPress` of Button 3: call `XSetInputFocus()` **without**
  `XRaiseWindow()`, then `XAllowEvents(dpy, ReplayPointer, time)` so the
  click is replayed to the application unchanged (the app still sees the
  click; it just doesn't come with a raise).
- On Button 1 (Select): focus **and** raise as usual.

Proof that this is routine: FVWM (`ClickToFocus` without
`ClickToFocusRaises`), KWin ("Click raises active window" checkbox can be
turned off), Openbox (`<raiseOnClick>false</raiseOnClick>`) all already
ship per-button-agnostic versions of exactly this policy. Making it
*per-button* (Select raises, Adjust doesn't) is a small delta on the same
mechanism, and FVWM's fully scriptable per-button `Mouse` bindings can
express it today — useful for a zero-code prototype.

### Wayland

On Wayland this is even more naturally the compositor's job: clients
never see global input, cannot raise themselves (they can only *request*
activation via `xdg-activation`, which the compositor may ignore), and
keyboard focus is assigned solely by the compositor.

- **If ULTRA OS ships its own compositor** (wlroots, Smithay, Louvre):
  in the pointer-button handler, when the button is BTN_RIGHT give the
  surface keyboard focus but skip the reorder of the scene graph /
  stacking list. Trivial, ~10 lines of policy code.
- **If ULTRA OS rides on someone else's compositor** (GNOME Mutter, KDE
  KWin): not implementable from outside. Wayland has *no* protocol for a
  third-party process to intercept clicks or dictate stacking. It would
  have to be a Mutter extension (JS) or a KWin script — per-desktop,
  fragile, and out of scope for "all of Linux".

**Verdict: fully achievable on both X11 and Wayland, for all apps,
provided ULTRA OS controls the WM (X11) / compositor (Wayland).** This is
the strongest argument for ULTRA OS shipping its own WM/compositor rather
than a desktop shell on top of a foreign one.

---

## 3. Behavior 2 — context menu on middle click

Applications open context menus when they receive a **logical button 3 /
BTN_RIGHT** event. That number is hard-wired convention in every toolkit
(GTK `gdk_event_triggers_context_menu()`, Qt `Qt::RightButton` →
`QContextMenuEvent`, Chromium, etc.). We cannot change the convention —
but we can change **which physical button produces that logical event**,
at any of three layers below the application:

### Option A — evdev/libinput level (works for X11 *and* Wayland, all apps)

Rewrite events between the kernel and the display server:

- **udev hwdb** can statically swap buttons per device
  (`EVDEV_ABS`/keycode remaps): `BTN_MIDDLE → BTN_RIGHT` and
  `BTN_RIGHT → BTN_MIDDLE`. Zero-daemon, but static — no conditional
  logic.
- **Interception tools** (`evsieve`, `interception-tools`) run as a small
  daemon that grabs `/dev/input/event*` (EVIOCGRAB), rewrites events, and
  re-emits them through `uinput`. This supports *conditional* rewriting
  (needed for behavior 3) and works identically under X11 and Wayland
  because it sits below both.

### Option B — X11 pointer mapping (X11 only)

`XSetPointerMapping()` / `xinput set-button-map <dev> 1 3 2` swaps logical
buttons 2 and 3 server-wide. One line, works today, X11 only.

### Option C — own compositor (Wayland; also covers XWayland)

The compositor receives BTN_MIDDLE from libinput and forwards BTN_RIGHT
to the client in `wl_pointer.button`. Because XWayland receives its input
*from the compositor*, X11 apps running under it inherit the mapping for
free. This is the cleanest long-term option because the rewrite can be
**per-surface** (e.g. disabled for a game that grabbed the pointer, or
for UltraCanvas-native apps that implement real RISC OS semantics
themselves — see §6).

### Consequences and edge cases

- The physical middle button no longer produces middle-click paste or
  middle-drag scroll. RISC OS has no middle-paste, so for ULTRA OS this
  is presumably acceptable; X primary-selection paste could be re-bound
  (e.g. Shift+Menu) if wanted.
- Apps that read raw evdev directly (some games via SDL in raw mode) or
  XInput2 raw events bypass Options B/C but are still covered by A.
- Menu-*hold* behavior differs: RISC OS menus stay open while the button
  is held and select on release; Linux apps mostly open on press and
  keep the menu open after release. That difference is inside the apps'
  menu code and **cannot** be fixed externally.

**Verdict: achievable for effectively all applications.** The app
genuinely opens its own context menu — we only changed which physical
button delivers "button 3".

---

## 4. Behavior 3 — Adjust adds to selection (the hard one)

"Add to selection" is **not a protocol concept**. There is no X11 or
Wayland event that means "extend selection"; each toolkit implements
selection inside its widgets, and the universal Linux convention is:

- **Ctrl + Left-click** — toggle item in/out of a discontiguous selection
  (icon views, list views, file managers) — closest to RISC OS Adjust.
- **Shift + Left-click** — extend a contiguous range (text, lists).

No unmodified application will ever interpret a bare right-click as
"add to selection". The only universal lever we have is to make the
right button *deliver* one of the above combinations:

### The event-synthesis approach

Intercept physical BTN_RIGHT press and deliver **KEY_LEFTCTRL down →
BTN_LEFT down … BTN_LEFT up → KEY_LEFTCTRL up** instead:

- **evdev level** (`evsieve` map rules, or a small custom uinput daemon):
  works under X11 and Wayland alike, all apps.
- **Own Wayland compositor**: send a synthetic `wl_keyboard.modifiers`
  (Ctrl latched) around a forwarded BTN_LEFT. Per-surface control again.
- **X11 WM**: `XGrabButton(Button3)` + XTEST
  (`XTestFakeKeyEvent`/`XTestFakeButtonEvent`) replay.

This composes correctly with behavior 1: the WM/compositor first applies
focus-without-raise, *then* forwards the rewritten event to the app.

### Why it can only ever be an approximation

| Problem | Detail |
|---|---|
| Ctrl vs Shift is context-dependent | Adjust in a file-manager icon view should be Ctrl+click (toggle); Adjust in a text area on RISC OS *extends* the selection, which on Linux is Shift+click. The rewriter cannot know what widget is under the pointer in a foreign app. One mapping must be chosen (recommend **Ctrl**, matching Filer-style toggling); text-area behavior will then differ from RISC OS. |
| Right-drag apps break | Blender, CAD packages, many games, image editors use right-button drags. Needs a per-app exemption list (possible in a compositor: match `xdg_toplevel.app_id`; possible in evsieve only globally). |
| Modifier side effects | Injecting Ctrl can combine with real keyboard state (user holding a key) or trigger Ctrl-click behaviors that are *not* selection (e.g. Ctrl+click opens link in new tab in browsers — actually roughly Adjust-like, sometimes desirable). |
| No menu suppression needed, but no menu either | Once right = Ctrl+left, the *only* way left to reach app context menus is the remapped middle button (behavior 2) — consistent with RISC OS, but users of conventional Linux muscle memory lose right-click menus system-wide. Should be a per-app/per-session toggle. |
| Adjust semantics beyond selection | RISC OS also uses Adjust for "opposite scroll direction" on scrollbars, "close window without closing app", Adjust-click on window furniture, etc. None of that is reachable externally. |

### Per-toolkit alternatives (rejected as primary mechanism)

Runtime injection *without recompiling* is technically possible per
toolkit — GTK3 modules (`GTK_MODULES`), Qt platform themes / plugins
(`QT_QPA_PLATFORMTHEME`), `LD_PRELOAD` shims hooking
`gtk_gesture_click` / `QApplication::notify` — and could implement
*faithful* Adjust semantics by calling real widget selection APIs. But:
GTK4 removed loadable modules; Electron/Chromium, Firefox, Java, Flutter
each need separate hooks; every toolkit major version breaks them. This
path is a maintenance treadmill and still not universal. It could later
*augment* the event-synthesis approach for the two big toolkits, not
replace it.

**Verdict: only approximable.** Right-click → Ctrl+Left-click synthesis
gives RISC OS-like "Adjust adds to selection" in file managers, list
views and icon grids — the places where it matters most — but it will
never be pixel-faithful RISC OS Adjust everywhere, and it needs an
exemption mechanism for right-drag applications.

---

## 5. Wayland constraint worth stating explicitly

Everything above assumes ULTRA OS controls a layer below the apps. On
Wayland this is non-negotiable by design: the protocol deliberately
forbids third-party processes from globally observing or injecting input
(no XTEST equivalent; `libei`/remote-desktop portals allow *injection*
with user consent but not *interception with rewriting*). Therefore:

> **On Wayland, the only complete solution is ULTRA OS's own compositor.**
> The evdev-level rewriter (Option A) is the only piece that works even
> on foreign compositors — it covers behaviors 2 and 3, but behavior 1
> (focus without raise) is compositor policy and cannot be added from
> outside GNOME/KDE.

On X11, by contrast, everything can be done today by any WM plus XTEST —
X11's permissiveness is exactly why this is easy there.

---

## 6. Recommended architecture for ULTRA OS

Three layers, in build order:

1. **Prototype (days, zero code):** X11 session with
   - FVWM per-button `Mouse` bindings: Button 1 = focus+raise,
     Button 3 = focus only (behavior 1);
   - `evsieve` (or `xinput set-button-map` for a first taste) mapping
     physical middle → logical right (behavior 2) and physical right →
     Ctrl+left (behavior 3).
   This validates the UX decisions (Ctrl vs Shift, which apps need
   exemptions) before any code is written.

2. **X11 production:** fold the same policies into the ULTRA OS window
   manager: per-button focus policy via `XGrabButton`/`XAllowEvents`,
   button rewriting via grab + XTEST replay, per-app exemptions keyed on
   `WM_CLASS`.

3. **Wayland production (the real target):** ULTRA OS compositor
   (wlroots-based is the pragmatic choice) implementing all three
   behaviors in the input-dispatch path, with per-surface policy keyed on
   `app_id`. XWayland clients inherit everything automatically because
   their input flows through the compositor.

**UltraCanvas-native applications should not rely on the shim at all**:
the framework's event layer (`UCEvent` already carries the button) should
expose first-class Select/Menu/Adjust semantics so ULTRA OS-native apps
get *faithful* RISC OS behavior (menu-on-release, Adjust-scroll,
Adjust-close, correct extend-vs-toggle per widget). The compositor then
exempts UltraCanvas windows from rewriting (they want the raw buttons)
and the synthesis layer remains purely a **compatibility shim for foreign
apps**. This is a separate implementation task on the UltraCanvas side.

---

## 7. Summary

| Behavior | X11 | Wayland (own compositor) | Wayland (GNOME/KDE) | Fidelity for unmodified apps |
|---|---|---|---|---|
| 1. Adjust-click focuses without raising | ✅ own/configured WM | ✅ trivial | ❌ not from outside | 100% |
| 2. Menu on middle click | ✅ button remap (3 ways) | ✅ per-surface rewrite | ⚠️ evdev remap only | ~100% (menu-hold semantics excepted) |
| 3. Adjust adds to selection | ⚠️ synth Ctrl+Left | ⚠️ synth Ctrl+Left, per-surface | ⚠️ evdev synth only | Approximation: good in lists/icon views, imperfect in text, needs right-drag exemptions |

So: **not "impossible" — two of the three behaviors are fully achievable
for every unmodified Linux application, and the third is achievable as a
useful approximation.** The enabling decision is architectural: ULTRA OS
must own the window manager on X11 and the compositor on Wayland; an
evdev-level input rewriter covers whatever must also work outside ULTRA
OS's own session.
