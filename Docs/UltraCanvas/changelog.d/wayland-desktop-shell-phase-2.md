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
  - `DesktopShellTest` drives the backend under sway headless: a window of
    its own listed, reported, activated and closed; a Desktop window whose
    reserved edges sway's workspace makes room for. Still open: the virtual
    desktops (`ext-workspace-v1`), screenshots and shortcuts (the portals),
    xdg-activation - `Docs/UltraCanvas/WaylandDesktopShellProposal.md`.
