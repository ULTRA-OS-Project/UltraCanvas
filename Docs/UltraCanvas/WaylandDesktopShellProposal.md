# Wayland for UltraDesktop — Decision and Plan

**Status:** Decided, phase 1 built (the windowing backend), phase 2 built:
the layer shell (the desktop window and its reserved edges), foreign
toplevels (the window list, the monitor, activate / minimize / close), the
virtual desktops (ext-workspace), screenshots and global shortcuts (the
portals), xdg-activation (RaiseAndFocus), the panels and popups a parent
places, file drags and drops, the session lock and GL surfaces; the
Wayland build runs in CI. What is left is in §4.11.
**Scope:** the UltraCanvas windowing backend on Linux, the
`UltraCanvasDesktopShell` backend, and what UltraDesktop needs from both to
be the shell of a Wayland session.
**Last Modified:** 2026-11-04

## 1. The decision

UltraDesktop's target is ULTRA OS's own session, whose compositor is one
ULTRA OS ships and builds on wlroots. The desktop shell therefore takes the
**wlroots protocol route**: it speaks the layer-shell and
foreign-toplevel protocols to the compositor, and the freedesktop portals
where a protocol would be a compositor-private one. GNOME and KDE are
explicitly out of scope: a Wayland session of either shows UltraDesktop's
windows as ordinary windows, and the shell's features answer "not available
on Wayland" there, as the Windows build answers beside Explorer for the
things only the shell may do.

What each piece of the shell uses:

| Shell feature (`UltraCanvasDesktopShell`) | Protocol or service | Status |
|---|---|---|
| The bars and the desktop window itself | `wlr-layer-shell-unstable-v1`: the desktop window on the *background* layer of its output, the bars' edges as exclusive zones on the *top* layer (the Wayland form of `ReserveScreenEdges`) | **phase 2, done** (§4.1) |
| Window list, the active window, activate / minimize / close | `wlr-foreign-toplevel-management-unstable-v1` (and `ext-foreign-toplevel-list-v1` for the list alone where the compositor has only that) | **phase 2, done** for the wlroots protocol (§4.2); the ext list is open |
| Virtual desktops | `ext-workspace-v1` | **phase 2, done** (§4.3) |
| The monitor (something changed) | the events of the foreign-toplevel and workspace protocols, folded into `MonitorWait` | **phase 2, done** (§4.2) |
| Screenshots | the `org.freedesktop.portal.Screenshot` portal (`wlr-screencopy-unstable-v1` directly where the session's portal backend is absent is open) | **phase 2, done** (§4.4) |
| Global shortcuts, the Super tap | the `org.freedesktop.portal.GlobalShortcuts` portal (the compositor's own key bindings decide; no client may grab a key on Wayland) | **phase 2, done** (§4.4) |
| The screens | `wl_output` (position, size, scale) — `ListScreens`, `GetScreenSize` | **phase 1, done** |
| The end of the session, the lock | logind on the system bus, as on X11; the locker itself through `ext-session-lock-v1` (`WindowType::SessionLock`) for the lock screen a program draws | **phase 1 (logind), phase 2 (the locker), done** (§4.8) |
| The loudspeaker | the sound server, as on X11 | **phase 1, done (shared)** |
| The notification area | StatusNotifierItem over D-Bus, as on X11; the click position is the last pointer position in the shell's own window (Wayland tells a client no screen position) | **phase 1, done (shared)** |
| Devices (camera, microphone, battery, keyboard layout) | procfs / sysfs as on X11; the layout from the seat's xkb keymap | **phase 1, done** |
| In place of the system's desktop | none: the session file starts UltraDesktop, as on X11 | done |

The portals need `xdg-desktop-portal` with a backend that answers for the
session (`xdg-desktop-portal-wlr` on a wlroots compositor); ULTRA OS's
session ships one.

### 1.1 Why not per-compositor D-Bus

The other route — GNOME Shell's and KWin's private D-Bus interfaces — would
have meant one backend per desktop, each tied to that desktop's release
cycle, and none of them the compositor ULTRA OS ships. The wlroots
protocols are implemented by every wlroots compositor (sway, labwc, Hyprland,
river, cage, wayfire …), by KWin for the layer shell and foreign toplevels,
and are what every third-party panel and launcher on Wayland uses. The
portal route for screenshots and shortcuts works on every session with a
portal backend, GNOME and KDE included, so those two features are not even
limited to the target.

## 2. Why the windowing backend had to come first

Until this change the framework spoke X11 only: `UltraCanvasApplication`
and `UltraCanvasWindow` were the X11 classes by compile-time alias, every
window a `cairo_xlib_surface_t` on an X window, the clipboard the X
selections. A Wayland shell protocol attaches to a `wl_surface`, so no
desktop-shell backend could exist before the framework had windows that
*are* `wl_surface`s. Phase 1 is that: the framework runs as a native Wayland
client.

## 3. Phase 1: the Wayland windowing backend (built)

### 3.1 Build

`cmake -DULTRACANVAS_WAYLAND=ON` builds the Wayland backend in place of the
X11 one; the default stays X11. One library speaks one display protocol —
the two backends are not in the same binary, because `UltraCanvasApplication`
and `UltraCanvasWindow` are compile-time aliases that every application and
module names directly. A consumer sees `ULTRACANVAS_DISPLAY_WAYLAND=1` as a
public define.

Dependencies: `libwayland-dev` (client and cursor), `wayland-protocols`
(1.18 or later), `libxkbcommon-dev`, `wayland-scanner`. The X11 libraries
stay linked: the keysym table is `<X11/keysym.h>`'s, and the applications'
`main()` calls X11's thread initialisation on every Linux build. The protocol bindings
(xdg-shell, xdg-decoration) are generated by `wayland-scanner` into the
build directory at build time; nothing generated is committed.

GL surfaces run on EGL opened on the Wayland connection (§4.9); GLX is
left out of a Wayland build.

### 3.2 The files

All under `UltraCanvas/OS/Linux/Wayland/`:

| File | What |
|---|---|
| `UltraCanvasWaylandApplication.{h,cpp}` | the connection and the registry (compositor, shm, seat, outputs, `xdg_wm_base`, `zxdg_decoration_manager_v1`, `wl_data_device_manager`); the event loop; the seat's keyboard through xkbcommon (keymap, modifiers, key repeat from `repeat_info` on the framework's timers) and pointer (enter / leave / motion / buttons / axis frames, double clicks as X11 counts them) turned into `UCEvent`s |
| `UltraCanvasWaylandWindow.{h,cpp}` | a `wl_surface` with an `xdg_toplevel`; the frame as a cairo image surface at the physical size (device scale from the outputs the surface is on), copied into a free `wl_shm` buffer on `InvalidateWindowNative`, attached, damaged and committed; configure → `WindowResize`, close → `WindowCloseRequest`; server-side decorations asked for an ordinary window, none for a borderless one; maximize / fullscreen / minimize through xdg-shell; a Desktop window and its reserved edges on the layer shell, a panel of it as a layer surface, a popup of a toplevel as `xdg_popup`, a SessionLock window as a lock surface, RaiseAndFocus through xdg-activation, file drags (§4) |
| `UltraCanvasWaylandCursor.cpp` | the cursor from the theme (`XCURSOR_THEME`, `XCURSOR_SIZE`) at the output's scale through libwayland-cursor; a cursor from a PNG as an shm buffer |
| `UltraCanvasWaylandClipboard.{h,cpp}` | `wl_data_device`: offers read through a pipe, a `wl_data_source` serving what this program copied, the same MIME types as the X11 backend (`text/plain;charset=utf-8`, `text/html`, `text/uri-list` with `x-special/gnome-copied-files`, `image/png`, `x-kde-passwordManagerHint`); file drags out and drops in (§4.7) |
| `UltraCanvasWaylandDesktopShell.cpp` | the desktop-shell backend: `ListScreens` and `GetScreenSize` from the outputs, the devices, the keyboard layout (phase 1); the window list, the monitor, the reserved edges and the virtual desktops (phase 2, §4) |
| `UltraCanvasWaylandPortals.cpp` | screenshots and global shortcuts through the freedesktop portals (§4.4) |
| `protocols/` | the protocol XML vendored for the backend: the wlroots layer shell and foreign toplevels (wayland-protocols does not carry them), ext-workspace (it carries that only from 1.39) |

The pieces both backends need moved out of the X11 files into shared ones
under `OS/Linux/`: `UltraCanvasLinuxFonts` (fontconfig, the bundled fonts),
`UltraCanvasLinuxKeys` (the keysym to `UCKeys` table), `UltraCanvasLinuxDeviceProbes`
(procfs / sysfs), `UltraCanvasLinuxUriList` (file URIs). The session
(logind), the loudspeaker (libpulse), the tray host (D-Bus), the GTK dialogs,
the accessibility bridge and everything else under `OS/Linux` were never
X11-bound and compile into both.

### 3.3 Where the backend's shape differs from X11

- **Positions.** A Wayland client neither learns nor sets its toplevels'
  positions. `SetWindowPosition` keeps the wish and `GetWindowPosition`
  returns it; `pointerGlobal` is the window-relative position. A child
  placed by its parent is the exception (§4.6).
- **Raise and focus.** `RaiseAndFocus` asks through xdg-activation (§4.5);
  the compositor decides.
- **Minimize.** xdg-shell reports no minimized state back; `Minimize` takes
  it as done, `Restore` commits a frame anew.
- **Icons.** xdg-shell has no window icon: the compositor shows the icon of
  the desktop entry the app id names (the program's name).
- **Native dialogs.** GTK's dialogs run on GTK's own Wayland connection
  and cannot be made transient for our surfaces (that needs `xdg-foreign`);
  they are kept above instead.
- **Drag and drop**: §4.7.
- **Image cursors** are PNG only (cairo reads them); other pictures leave
  the themed cursor up.

### 3.4 Checked

Built and run under `weston --backend=headless --renderer=pixman --debug`
(weston 13; the debug flag lets `weston-screenshooter` capture the
output): the framework, DemoApp and UltraDesktop start as Wayland clients,
open their windows and draw - the demo at its own size with the component
tree and the Menus page, the desktop maximized under weston's panel with
the wallpaper, the bars, the clock and the device icons. DesktopShellTest
passes against the `wayland` backend (66 checks; the phase-2 features
report their reason). An X11 build of the same sources is unchanged.
Pointer and keyboard input could not be driven headless (weston's headless
backend has no input devices); that is checked on a real session.

## 4. Phase 2: the desktop shell backend (begun)

In the order UltraDesktop needs them. The two wlroots protocols are not in
wayland-protocols; their XML travels with the backend under
`OS/Linux/Wayland/protocols/` (MIT-style licence, see
`THIRD_PARTY_LICENSES.md`) and is generated like the others. The layer
shell's argument named `namespace` is `scope` in the vendored copy, so the
generated header compiles as C++.

### 4.1 The layer shell (built)

On a compositor with `zwlr_layer_shell_v1` (every wlroots compositor, KWin)
a `WindowType::Desktop` window is no `xdg_toplevel` but a
`zwlr_layer_surface_v1` on the *background* layer of the output its
`WindowConfig::desktopArea` names (the one at that position; the first
output when none is named), anchored to all four edges with exclusive zone
−1, so the compositor sizes it to the output and keeps it under every
window; keyboard focus is *on demand* (layer shell 4) so the launcher's
search field gets the keys when clicked. Without the layer shell (GNOME's
compositor) the window stays a maximized toplevel, as in phase 1.

`ReserveScreenEdges` on such a window becomes one layer surface per
reserved edge on the *top* layer - fully transparent, with an empty input
region so clicks reach the bar the desktop draws under it, anchored to its
edge and the two beside it, the edge's thickness as its exclusive zone.
One layer surface carries one exclusive zone on one edge and the desktop's
own is anchored to all four, so this is the layer shell's form of an X11
strut. The caller's physical pixels are rounded up to the output's logical
ones. The desktop shell backend finds the window among the application's
(`UltraCanvasWaylandWindow::SetReservedEdges`), so the call is the UI
thread's, which is where UltraDesktop makes it; on a window that is no
layer surface it answers false.

### 4.2 Foreign toplevels (built)

`wlr-foreign-toplevel-management-unstable-v1` gives `ListWindows`,
`GetActiveWindow`, `ActivateWindow` (which un-minimizes first),
`MinimizeWindow`, `CloseWindow` and the monitor's events. The backend
speaks it on a `wl_display` connection of its own, as the X11 backend reads
EWMH on its query connection: `ListWindows` is called from any thread and
the monitor's thread blocks on the socket, and neither may run inside the
application's event loop. Every request on that connection runs under one
mutex; the monitor polls the socket outside it and reads inside it (a read
of a socket another thread's round trip emptied does not block), and a
round trip elsewhere that dispatched a change nudges the monitor through
its pipe. The id a window is listed by is the backend's own counter, stable
for the handle's life, since the protocol numbers nothing; the app id is
reported as both `appClass` and `appName`, and the core resolves the icon
from it as it does from a class. The protocol names no process and no
position: `processId` is 0, `GetWindowBounds` and `MoveWindowTo` answer
false. A compositor without the manager (weston, GNOME) connects, lists
nothing, and `UltraCanvasDesktopShellMonitor::IsNative()` is false.

### 4.3 Virtual desktops (built)

`ext-workspace-v1` (wayland-protocols 1.39; vendored like the wlroots ones,
since the 22.04 runners' wayland-protocols is 1.25) on the shell
connection. The virtual desktops are the workspaces of the group that
holds the first output (the primary, as `ListScreens` names it), hidden
ones left out, in the order of their coordinates, then their names as
numbers, then as announced; the active one is the current desktop.
`SetCurrentVirtualDesktop` is `activate` + `commit` where the workspace
has the capability, `SetVirtualDesktopCount` is `create_workspace` on the
group (named by number) or `remove` of the last ones, never the active
one, where the capabilities allow. The manager's `done` wakes the monitor.
A compositor without the manager (sway 1.9, weston) has one desktop. The
protocol ties no toplevel to a workspace and foreign toplevels name none,
so a window's `virtualDesktop` is −1 and `MoveWindowToVirtualDesktop`
answers false. Compositors that speak it: sway 1.11, labwc 0.9, KWin.

### 4.4 Screenshots and global shortcuts (built): the portals

`UltraCanvasWaylandPortals.cpp`, over GDBus (the tray host's arrangement:
a `GMainContext` of its own per call, so nothing needs the application's
loop and every call works from any thread). The portal is
`org.freedesktop.portal.Desktop` on the session bus, served by
`xdg-desktop-portal` with the session's backend (`xdg-desktop-portal-wlr`
on a wlroots compositor; it needs PipeWire for its screen capture). Each
exchange is a Request whose Response is awaited.

- `CaptureScreen` is `Screenshot.Screenshot` (not interactive): the portal
  writes a PNG and answers with its URI, which is moved to the caller's
  path. `CaptureScreenImage` reads that PNG into the BGRx rows
  `DesktopScreenImage` holds and deletes it at once - the portal cannot
  hand pixels over, so the file exists for the moment between the two,
  which the QR enrolment that uses it has to live with. `CaptureScreenArea`
  crops that. A portal that asks the user is waited for, up to a minute.
- `UltraCanvasGlobalShortcut` is `GlobalShortcuts`: `CreateSession`, one
  shortcut bound with the accelerator as its preferred trigger in the
  shortcuts specification's spelling (`Ctrl+Alt+V` → `CTRL+ALT+v`,
  `Super` alone → `LOGO`), the `Activated` signal when the compositor
  fires it, `Session.Close` on Stop. The compositor's own bindings decide
  whether the trigger is honoured. A portal that asks the user is waited
  for by the shortcut's thread, so `Start` returns at once with the
  session; one that refuses fails `Start` with the reason.

Without a portal (no `xdg-desktop-portal`, no session bus, a backend
without the interface - `xdg-desktop-portal-wlr` has no GlobalShortcuts)
every call fails with the reason. `wlr-screencopy` directly, for a session
without a portal, stays open.

### 4.5 RaiseAndFocus (built): xdg-activation

`xdg-activation-v1` (wayland-protocols 1.21, so from the system). A
window asks for the focus with a token the compositor issues against the
last input this program had - the serial of the last key, button or
keyboard enter, the seat, the surface that has the focus - then activates
its own surface with it. The compositor decides: wlroots rejects a token
whose serial it never gave the client, and sway then marks the window
urgent instead of focusing it (also its default policy,
`focus_on_window_activation urgent`); with a serial and
`focus_on_window_activation focus` it focuses. A compositor without the
protocol: nothing, as before. The desktop shell's `ActivateWindow` for
other clients' windows stays the foreign-toplevel request.

### 4.6 Panels and popups placed by their parent (built)

The one thing a Wayland client may position is a child relative to its
parent, so a window with `config.parentWindow` of type Popup, Borderless,
Tool or Notification is placed where `config.x` / `config.y` say, relative
to that parent:

- a child of a layer-shell desktop (UltraDesktop's panels beside its
  bars: `UltraDesktopPopupPanel` and the clipboard panel get the desktop
  window as their parent, in a Wayland build only) is a layer surface of
  its own on the *top* layer, anchored to the output's top-left corner
  with the position as its margins, keyboard focus *on demand*: it takes
  the keys when clicked and gives them back (a blur, which closes a panel)
  when the desktop is clicked, as the X11 panel does on focus loss;
- a child of a toplevel is an `xdg_popup` with a positioner (anchored at
  the point asked for, hanging to the bottom right, slid onto the screen
  where it would leave it), made when the window is *shown*, since
  UltraDesktop sets the position between creating and showing, and remade
  on every show, as a popup dismissed by the compositor is gone for good;
  `SetWindowPosition` on a shown popup is `xdg_popup.reposition`
  (xdg_wm_base 3). No grab is taken: a compositor validates one against a
  button still held, and a panel opens on the click's release.

A child without the layer shell or an xdg_surface parent is an ordinary
toplevel the compositor places, as before.

### 4.7 Drag and drop (built)

On the seat's `wl_data_device`, which the clipboard backend holds. A file
drag out of a window (`StartNativeFileDrag`) is a `wl_data_source`
offering `text/uri-list` with copy and move as its actions, started with
the serial of the last input (a button press, which is what a compositor
accepts; a seat without devices gives none, and the call answers false);
the target's choice of action and its finish or cancel reach the
callback as `(accepted, moved)`. No icon surface travels with the pointer
yet. A drag from another program over one of our windows is accepted for
`text/uri-list` (copy, or move where the source insists), the window gets
`DragEnter` / `DragOver` / `DragLeave`, and a drop reads the list through
a pipe and delivers `Drop` with the paths, as XDnD does on X11. A save
drag (XDS, `StartNativeSaveDrag`) has no Wayland form and answers false.

### 4.8 The session lock (built)

`WindowType::SessionLock`, new in `UltraCanvasWindow.h`: the lock screen a
program draws. On Wayland it is an `ext-session-lock-v1` lock surface on
its output (`desktopArea` names which, else the first): the application
holds one `ext_session_lock_v1` for all such windows, asked for with the
first and released - `unlock_and_destroy` - with the last, so closing the
lock windows unlocks; the compositor shows nothing else while it is held,
and one that refuses (`finished`) has the lock windows asked to close. A
lock surface is configured on creation and may neither be committed
before its first frame nor shown without a buffer, so such a window is
closed, never hidden. Elsewhere (X11, Windows, macOS, a Wayland
compositor without the protocol) it is a fullscreen borderless window kept
above every other, which the platform does not enforce. UltraDesktop's own
"Lock screen" stays logind's: it asks the session's locker; this is what a
locker written on the framework uses.

### 4.9 GL surfaces (built)

A GL surface renders into a pbuffer and is read back into the window's
picture, so the context manager needs a display, not a window-system
surface. The EGL manager opens its display on the Wayland connection
(`EGL_PLATFORM_WAYLAND`) in a Wayland build, else on Mesa's surfaceless
platform, else EGL's default; the GLX manager, an X11 API, is left out of
a Wayland build, and `ULTRACANVAS_ENABLE_GL` is no longer forced off. A
compositor without a GPU (sway headless with the pixman renderer) gets
Mesa's software renderer through the surfaceless platform.

### 4.10 CI

`.github/workflows/wayland-build.yml`: on ubuntu-24.04 (sway 1.9,
wayland-protocols 1.33, libjxl, libheif and MuPDF as packages) the
framework, DesktopShellTest and UltraDesktop are built with
`ULTRACANVAS_WAYLAND=ON` and GL on; `DesktopShellPortalTest` runs under a
session bus, `DesktopShellTest` under sway headless with
`focus_on_window_activation focus` and wtype's virtual keyboard
(`DESKTOPSHELLTEST_WAYLAND_KEYBOARD=1`, so the activation check is
mandatory), and a screenshot of UltraDesktop under sway goes up as an
artifact. The row runs for changes under `UltraCanvas/`, the desktop and
the test.

### 4.11 Open

An icon surface for a drag; `ext-foreign-toplevel-list-v1` for the list
alone on a compositor that has only that; `wlr-screencopy` for a session
without a portal; a grab for menus that open on a press.

Each step is a backend change behind the unchanged `UltraCanvasDesktopShell`
API, so UltraDesktop's code stays what it is; only where it assumed screen
positions (the popup panels) does it need the positioner.

## 5. Testing

`weston --backend=headless` runs the windowing backend without a GPU or a
display, but weston has neither the layer shell nor foreign-toplevel
management; the desktop shell backend is driven by **sway headless**, a
wlroots compositor that has both:

```bash
export XDG_RUNTIME_DIR=/tmp/sway-test && mkdir -p -m 700 "$XDG_RUNTIME_DIR"
printf 'output HEADLESS-1 resolution 1280x800\nxwayland disable\n' > /tmp/sway-test/config
WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1 WLR_RENDERER=pixman \
  sway -c /tmp/sway-test/config &
# sway names its socket wayland-1 and exports SWAYSOCK to its children
WAYLAND_DISPLAY=wayland-1 SWAYSOCK=$(ls "$XDG_RUNTIME_DIR"/sway-ipc.*.sock) \
  ULTRACANVAS_TEST_DISPLAY_REQUIRED=1 build-wayland/bin/DesktopShellTest
```

`DesktopShellTest`'s Wayland section (a build with `ULTRACANVAS_WAYLAND`)
opens a window in a program of its own and checks it is listed with its
UTF-8 title and app id, reported by the monitor, active, minimized (where
the compositor has the state; sway has none), activated again and closed;
then a Desktop window whose reserved edges it reads back from
`swaymsg -t get_workspaces` (the workspace loses 40 pixels at the top and
60 on the right, and gets them back when the desktop ends); then two
windows of one program, the second focused, and the first's
`RaiseAndFocus` making it the active one - with
`focus_on_window_activation focus` in sway's configuration and a keyboard
on the seat (`wtype -s 240000 ""` keeps a virtual one for four minutes;
`DESKTOPSHELLTEST_WAYLAND_KEYBOARD=1` then makes the check mandatory);
then the virtual desktops as the compositor reports them. The section
skips itself without `WAYLAND_DISPLAY`, and the window-list checks under a
compositor without the manager.

The portals are a CTest entry of their own, `DesktopShellPortalTest`
(`dbus-run-session -- DesktopShellTest --portals`), against a fake
`org.freedesktop.portal.Desktop` the test exports itself in a program of
its own: Screenshot answers with a 4×3 PNG, CreateSession and
BindShortcuts as the real portal does, Activated fires twice; the
backend's screenshots in their three forms, a bound shortcut with its
trigger spelling, its activations and the session's Close are checked.
That entry needs no compositor and runs wherever the Wayland build does.
Under sway the section also opens a SessionLock window in a program of its
own and reads back that the compositor granted the lock, a popup of a
toplevel and reads back where sway put it (40,30 as asked, 100×80), a
panel of a Desktop window and reads back its size, starts a file drag (a
seat without devices gives no serial, the virtual keyboard's is answered
with a cancel; either is the compositor's answer), and draws a GL surface,
reporting the renderer EGL gave it.
Against the real `xdg-desktop-portal` 1.18 + `xdg-desktop-portal-wlr`
under the headless sway in this container the screenshot was not
reachable: the daemon took the wlr backend for ScreenCast but exported no
Screenshot interface (and the backend needs PipeWire running), so the
backend's answer in that case - "the screenshot portal did not answer:
no such interface" - is what was seen; the exchange itself is the one the
fake mirrors, with the real portal's signatures. The Wayland build runs
in CI (§4.10).
