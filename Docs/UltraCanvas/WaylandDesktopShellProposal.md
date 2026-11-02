# Wayland for UltraDesktop — Decision and Plan

**Status:** Decided, phase 1 built (the windowing backend). Phase 2 (the
desktop shell's own protocols) is the next step and not started.
**Scope:** the UltraCanvas windowing backend on Linux, the
`UltraCanvasDesktopShell` backend, and what UltraDesktop needs from both to
be the shell of a Wayland session.
**Last Modified:** 2026-10-30

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
| The bars and the desktop window itself | `wlr-layer-shell-unstable-v1`: the bars on the *top* layer with exclusive zones (the Wayland form of `ReserveScreenEdges`), the wallpaper on the *background* layer, anchored to the output they belong to | phase 2 |
| Window list, the active window, activate / minimize / close | `wlr-foreign-toplevel-management-unstable-v1` (and `ext-foreign-toplevel-list-v1` for the list alone where the compositor has only that) | phase 2 |
| Virtual desktops | `ext-workspace-v1` | phase 2 |
| The monitor (something changed) | the events of the two protocols above, folded into `MonitorWait` | phase 2 |
| Screenshots | the `org.freedesktop.portal.Screenshot` portal; `wlr-screencopy-unstable-v1` directly where the session's portal backend is absent | phase 2 |
| Global shortcuts, the Super tap | the `org.freedesktop.portal.GlobalShortcuts` portal (the compositor's own key bindings decide; no client may grab a key on Wayland) | phase 2 |
| The screens | `wl_output` (position, size, scale) — `ListScreens`, `GetScreenSize` | **phase 1, done** |
| The end of the session, the lock | logind on the system bus, as on X11; the locker itself through `ext-session-lock-v1` when UltraDesktop draws the lock screen | phase 1 (logind), phase 2 (the locker) |
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

GL surfaces are off in a Wayland build for now (`ULTRACANVAS_ENABLE_GL` is
forced OFF with a message): the context managers are GLX and EGL-on-Xlib,
and EGL on a `wl_egl_window` is a later step.

### 3.2 The files

All under `UltraCanvas/OS/Linux/Wayland/`:

| File | What |
|---|---|
| `UltraCanvasWaylandApplication.{h,cpp}` | the connection and the registry (compositor, shm, seat, outputs, `xdg_wm_base`, `zxdg_decoration_manager_v1`, `wl_data_device_manager`); the event loop; the seat's keyboard through xkbcommon (keymap, modifiers, key repeat from `repeat_info` on the framework's timers) and pointer (enter / leave / motion / buttons / axis frames, double clicks as X11 counts them) turned into `UCEvent`s |
| `UltraCanvasWaylandWindow.{h,cpp}` | a `wl_surface` with an `xdg_toplevel`; the frame as a cairo image surface at the physical size (device scale from the outputs the surface is on), copied into a free `wl_shm` buffer on `InvalidateWindowNative`, attached, damaged and committed; configure → `WindowResize`, close → `WindowCloseRequest`; server-side decorations asked for an ordinary window, none for a borderless one; maximize / fullscreen / minimize through xdg-shell |
| `UltraCanvasWaylandCursor.cpp` | the cursor from the theme (`XCURSOR_THEME`, `XCURSOR_SIZE`) at the output's scale through libwayland-cursor; a cursor from a PNG as an shm buffer |
| `UltraCanvasWaylandClipboard.{h,cpp}` | `wl_data_device`: offers read through a pipe, a `wl_data_source` serving what this program copied, the same MIME types as the X11 backend (`text/plain;charset=utf-8`, `text/html`, `text/uri-list` with `x-special/gnome-copied-files`, `image/png`, `x-kde-passwordManagerHint`) |
| `UltraCanvasWaylandDesktopShell.cpp` | the desktop-shell backend of phase 1: `ListScreens` and `GetScreenSize` from the outputs, the devices, the keyboard layout; the phase-2 features answer false with "not available on Wayland yet" |

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
  returns it; `pointerGlobal` is the window-relative position. Popup panels
  that place themselves beside a bar land where the compositor puts a new
  toplevel until phase 2 gives them `xdg_popup` with a positioner (or the
  layer shell, for the bars' own panels).
- **Raise and focus.** `RaiseAndFocus` does nothing: only `xdg-activation`
  lets a client ask, and it needs a token from the activating input. Phase 2
  adds it where the shell activates a window.
- **Minimize.** xdg-shell reports no minimized state back; `Minimize` takes
  it as done, `Restore` commits a frame anew.
- **Icons.** xdg-shell has no window icon: the compositor shows the icon of
  the desktop entry the app id names (the program's name).
- **Native dialogs.** GTK's dialogs run on GTK's own Wayland connection
  and cannot be made transient for our surfaces (that needs `xdg-foreign`);
  they are kept above instead.
- **Drag and drop** (file drags out of a window) is not in phase 1.
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

## 4. Phase 2: the desktop shell backend (next)

In the order UltraDesktop needs them:

1. **The layer shell.** Vendor `wlr-layer-shell-unstable-v1.xml` (it is
   not in wayland-protocols) under `OS/Linux/Wayland/protocols/`, generate
   it like the others. A `WindowType::Desktop` window becomes a
   `zwlr_layer_surface_v1` on the background layer sized to its output;
   `ReserveScreenEdges` becomes the exclusive zone of a layer surface on the
   top layer. UltraDesktop's bars are drawn inside its one desktop window
   today, so the first form is one layer surface per monitor with its
   exclusive zones set from the bars' sizes; a bar per layer surface can
   follow.
2. **Foreign toplevels.** `wlr-foreign-toplevel-management-unstable-v1`
   gives `ListWindows`, `GetActiveWindow`, `ActivateWindow`,
   `MinimizeWindow`, `CloseWindow` and the monitor's events; the app id
   finds the desktop entry for the icon, as the X11 backend's class name
   does. `ext-workspace-v1` gives the virtual desktops.
3. **Portals.** `CaptureScreen` through the Screenshot portal (a file URI
   back, read into `DesktopScreenImage`); `UltraCanvasGlobalShortcut`
   through GlobalShortcuts (a session, `BindShortcuts`, the `Activated`
   signal; the Super tap is a shortcut the compositor binds). Both over
   GDBus, which the tray host already uses.
4. **xdg-activation** for `RaiseAndFocus` and `ActivateWindow`;
   `xdg_popup` for menus and the bars' panels; `wl_data_device` drags;
   `ext-session-lock-v1` for the lock screen UltraDesktop draws itself;
   EGL on `wl_egl_window` for GL surfaces.

Each step is a backend change behind the unchanged `UltraCanvasDesktopShell`
API, so UltraDesktop's code stays what it is; only where it assumed screen
positions (the popup panels) does it need the positioner.

## 5. Testing

`weston --backend=headless` with `WAYLAND_DISPLAY` set runs the whole
backend without a GPU or a display, in CI as under Xvfb today. weston
implements xdg-shell, xdg-decoration, the layer shell (`weston 10+`) and
screencopy, so phases 1 and 2 can both be driven by it; it has no
foreign-toplevel management, so the window list is checked against sway or
labwc headless (`WLR_BACKENDS=headless`) once phase 2 begins.
