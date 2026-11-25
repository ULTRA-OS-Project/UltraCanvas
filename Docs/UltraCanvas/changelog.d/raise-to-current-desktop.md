- **A window asked to come forward comes to the virtual desktop in view
  (X11).** `RaiseAndFocus` asked the window manager to activate the window and
  nothing else. For a window on another virtual desktop (workspace) the
  manager answers that as it likes: openbox and metacity only mark it "wants
  attention" where it lies, so nothing happened that the person could see,
  and icewm changes the desktop under them. A second start of a single-instance
  application ended there (`UltraCanvasSingleInstance::BringToFront`): with
  UOS-Settings or UltraClipboard open on desktop 1, UltraDesktop's *Desktop
  settings...* and its clipboard button did nothing on desktop 2. The window
  now asks to be moved to the current desktop first (`_NET_WM_DESKTOP`), then
  to be activated, and the manager handles the two in that order: it appears
  where it was asked for, in front, with the keyboard - measured under
  openbox, metacity, icewm and xfwm4.
  - `UltraCanvasWindowBase::MoveToCurrentDesktop()` is that move on its own,
    and takes the window this one belongs to (its parent) along, so a dialog
    brought over does not leave its window behind; `IsOnCurrentDesktop()`
    says whether a shown window is where the person can see it. Both are
    X11's so far: on the other platforms they answer "nothing to move" and
    "yes", and the system decides as before (a Wayland compositor alone: no
    protocol lets a client name a workspace).
  - A window on every desktop stays on every desktop, and one the manager
    names no desktop for (no manager, a popup) is left alone.
  - `BringToFront` brings the main window over behind an open dialog too.
  - `Tests/WindowCurrentDesktopTest.cpp` plays the window manager on a
    display without one and reads what a manager would be asked; it runs
    with the framework's window tests in CI.
