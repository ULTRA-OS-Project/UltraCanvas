- **The Wayland desktop shell backend, phase 2 begun: the layer shell and
  foreign toplevels.** In a build with `ULTRACANVAS_WAYLAND=ON`, on a
  compositor that speaks the wlroots protocols (every wlroots compositor -
  the one ULTRA OS ships - and KWin):
  - A `WindowType::Desktop` window is a `zwlr_layer_surface_v1` on the
    background layer of the output its `desktopArea` names, anchored to every
    edge, so the compositor sizes it to the output and keeps it under every
    window; `ReserveScreenEdges` becomes one transparent, input-less layer
    surface per reserved edge on the top layer with the edge's exclusive zone
    (the layer shell's form of a strut), so maximized windows make room for
    UltraDesktop's bars. Without the layer shell a Desktop window stays a
    maximized toplevel and reserves nothing.
  - `ListWindows`, `GetActiveWindow`, `ActivateWindow`, `MinimizeWindow`,
    `CloseWindow` and `UltraCanvasDesktopShellMonitor` run on
    `wlr-foreign-toplevel-management-unstable-v1`, over a connection of the
    backend's own (any thread, the application's loop untouched); the app id
    stands in for the class, the core finds the icon from it.
  - The two protocol definitions are vendored under
    `OS/Linux/Wayland/protocols/` (`THIRD_PARTY_LICENSES.md`), since
    wayland-protocols does not carry them.
  - `KillProcess` moved to the shared `UltraCanvasLinuxDesktopShellSession.cpp`;
    the Wayland backend now has every entry point the backend contract
    gained since phase 1 (`GetWindowBounds`, `MoveWindowTo`,
    `CaptureScreenArea`), the ones no protocol serves answering false.
  - The virtual desktops are `ext-workspace-v1` on the same connection (the
    protocol vendored too, wayland-protocols carries it only from 1.39): the
    workspaces of the primary output's group in order, the active one the
    current desktop, activate + commit to switch, create / remove to set the
    count where the compositor allows. One desktop on a compositor without
    it (sway 1.9).
  - Screenshots and global shortcuts go through the freedesktop portals
    (`OS/Linux/Wayland/UltraCanvasWaylandPortals.cpp`, over GDBus on a
    context of their own): `CaptureScreen`, `CaptureScreenImage` and
    `CaptureScreenArea` through `org.freedesktop.portal.Screenshot` (the
    portal's PNG moved to the caller's path, or read and deleted at once),
    `UltraCanvasGlobalShortcut` through `org.freedesktop.portal.GlobalShortcuts`
    (a session, the accelerator as the preferred trigger in the shortcuts
    specification's spelling, `Activated` to the callback, the session
    closed on Stop). Without a portal every call fails with the reason.
  - `RaiseAndFocus` asks for the focus through xdg-activation: a token
    against the last input serial, the seat and the focused surface, then
    the activation. The compositor decides (sway focuses with
    `focus_on_window_activation focus`, marks urgent by default).
  - `DesktopShellTest` drives the backend under sway headless: a window of
    its own listed, reported, activated and closed; a Desktop window whose
    reserved edges sway's workspace makes room for; two windows and the
    first's RaiseAndFocus making it active; the virtual desktops read. The
    portals are a CTest entry of their own, `DesktopShellPortalTest`,
    against a fake portal the test exports (16 checks). Still open:
    popups with a positioner, drags, the session lock, GL surfaces, the
    Wayland build in CI - `Docs/UltraCanvas/WaylandDesktopShellProposal.md`.
