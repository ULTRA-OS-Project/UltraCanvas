- **A key combination for the whole desktop.** `UltraCanvasGlobalShortcut`
  (`UltraCanvasDesktopShell.h`) calls back when `"Super+V"`, `"Ctrl+Alt+H"`
  or another combination is pressed, whichever window has the focus: on X11 a
  passive `XGrabKey` on the root window, with the Caps Lock and Num Lock
  variants, on a connection and thread of its own. `Start` fails and says why
  when the combination cannot be read, when another program holds it, and
  on the platforms without a backend; `Stop` joins the thread. UltraDesktop
  opens its clipboard panel with it.
