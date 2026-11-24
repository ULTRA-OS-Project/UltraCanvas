- **A window manager inside the program: `UltraCanvasEmbeddedWindowManager`.**
  For a backend that draws many windows onto one surface it keeps them
  stacked (desktop, windows, always-on-top, notifications, popups, tooltips,
  each band above the last), focused, placed (cascaded, a dialog centred over
  its window), moved by their title bars, resized by their edges, maximised
  into the work area a desktop's bars leave and minimised, and composites
  them - only what changed - with a shadow under each and the pointer on top.
  Platform-neutral; `Tests/EmbeddedWindowManagerTest` covers it with no
  display. Its title bar, `UltraCanvasEmbeddedWindowFrame`, is a window of its
  own built from an `UltraCanvasLabel` and three `UltraCanvasButton`s.
- **Android: desktop mode** (`UltraCanvasAndroidDesktop`). After `Enable()`
  every window is one of the embedded window manager's, drawn on an external
  display when one is connected (a `Presentation` that `UltraCanvasActivity`
  puts on the first presentation display - a USB-C cable or dock, a wireless
  display, the emulator's simulated second display) and on the phone's own
  screen otherwise. `CreatePhoneWindow` makes the window the phone shows
  meanwhile. A mouse is captured while the desktop is on the external display
  (`requestPointerCapture`), so it moves the desktop's pointer whichever
  display Android would have put its own on; the phone's screen can be a
  touchpad (`TouchpadInput`). Keys and the keyboard's text go to the focused
  window of the desktop, and the back button is Escape there. Without
  `Enable()` the Android backend is the one-window backend it was.
- **`UltraCanvasDesktopShell` answers from an in-program window manager.**
  Where no platform backend does (Android), the window list, activating,
  minimising and closing a window, the screen, the strips a desktop's bars
  reserve, the shell monitor and the applications come from the
  `EmbeddedHost` the desktop mode installs - so UltraDesktop's taskbar and
  Apps window work there.
- **`UltraCanvasHostedApps`: applications that run inside their host
  program.** Registered with an entry point, they are listed by
  `UltraCanvasDesktopShell::ListApplications` where the platform has none of
  its own, and `LaunchApplication` starts them in the process.
