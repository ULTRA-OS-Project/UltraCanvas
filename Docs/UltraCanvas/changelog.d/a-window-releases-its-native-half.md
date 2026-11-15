- **A window that is never closed now lets go of its OS window.** Closing a
  window ran the whole native teardown - the OS window, its drawing surface,
  its input context, its drag-drop registration. *Destroying* one ran none of
  it: `~UltraCanvasWindowBase` cannot call a virtual override, so a window
  that went without `Close()` first - the application's own windows at
  shutdown, or simply the last `shared_ptr` going away - left all of it
  behind. The X server kept the window until the connection closed, which is
  why nobody noticed. Each platform window class releases its native half in
  its own destructor now, as the Wayland and WASM ones already did. New
  `WindowShutdownTest` asks the X server whether the windows really went, and
  destroys an application with a window and an open popup still on it - the
  shape of the SIGSEGV that the first display-backed CI run turned up.
