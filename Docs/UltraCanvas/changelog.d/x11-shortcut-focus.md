- **A window opened by a global shortcut keeps the focus (X11).**
  `UltraCanvasGlobalShortcut` fired on the key press, while its passive grab
  still held the keyboard; a window it opened took the focus during the grab
  and the grab's end handed it focus events the window manager followed by
  giving the focus back - UltraDesktop's clipboard panel, which closes when
  it loses the focus, shut again at once about one Super+V in two.
  - The shortcut now fires when its key is released, and asks for
    detectable auto-repeat, so a held combination fires once.
  - The X11 event loop drops `FocusIn` / `FocusOut` whose mode is
    `NotifyGrab` or `NotifyUngrab`: a keyboard grab starting or ending (a
    shortcut held, a window manager's key binding) does not take the focus
    from a window. A real change during a grab still arrives
    (`NotifyWhileGrabbed`).
