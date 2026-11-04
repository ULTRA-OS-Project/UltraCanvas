- **The Wayland desktop shell's remaining pieces: panels and popups placed
  by their parent, drag and drop, the session lock, GL surfaces, and the
  Wayland build in CI** (`Docs/UltraCanvas/WaylandDesktopShellProposal.md`,
  sections 4.6 to 4.10). In a build with `ULTRACANVAS_WAYLAND=ON`:
  - A window with `parentWindow` of type Popup, Borderless, Tool or
    Notification is placed where `config.x` / `config.y` say, relative to
    its parent: a child of a layer-shell desktop as a layer surface of its
    own on the top layer (keyboard focus on demand, so a panel takes the
    keys when clicked and loses them - a blur - when the desktop is), a
    child of a toplevel as an `xdg_popup` with a positioner, made on Show
    and repositioned through xdg_wm_base 3. UltraDesktop's popup panels and
    clipboard panel get the desktop window as their parent in a Wayland
    build (`SetParentWindow`), so they open beside their bars instead of
    being tiled as toplevels.
  - File drags out of a window (`StartNativeFileDrag`: a `wl_data_source`
    with `text/uri-list`, copy and move, the target's answer to the
    callback) and drops in (`DragEnter` / `DragOver` / `DragLeave` / `Drop`
    with the paths, as XDnD on X11); a save drag has no Wayland form and
    answers false.
  - `WindowType::SessionLock`, the lock screen a program draws: an
    `ext-session-lock-v1` lock surface on Wayland (the application holds
    the one lock, taken with the first such window and released with the
    last; a compositor that refuses closes them), a fullscreen window kept
    above elsewhere.
  - GL surfaces on Wayland: the EGL context manager opens its display on
    the Wayland connection, else Mesa's surfaceless platform; GLX is left
    out of a Wayland build and `ULTRACANVAS_ENABLE_GL` is no longer forced
    off.
  - `.github/workflows/wayland-build.yml`: the framework, DesktopShellTest
    and UltraDesktop built with `ULTRACANVAS_WAYLAND=ON` on ubuntu-24.04,
    DesktopShellPortalTest under a session bus, DesktopShellTest under sway
    headless with a virtual keyboard, and a screenshot of UltraDesktop under
    sway as an artifact.
  - DesktopShellTest's Wayland section checks each: the lock granted and
    released, a popup placed at 40,30 as asked, a panel at its size, a drag
    started or refused with the compositor's reason, a GL surface drawn
    with the renderer EGL gave it.
