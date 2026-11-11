- **`UltraCanvasGlobalShortcut::StartExclusive`: a key taken from everyone
  else, the system included.** `Start` registers a hot key, which a key the
  system holds refuses - Print Screen while Windows 11 gives it to the
  Snipping Tool. `StartExclusive` registers none on Windows: a
  `WH_KEYBOARD_LL` hook on the shortcut's thread swallows the press made
  with exactly the given modifiers, and the release that ends it, so the
  key reaches only this program (`Alt+Print` stays the system's when
  `"Print"` is taken). On X11 the grab `Start` makes is exclusive already;
  on Wayland the compositor decides. The backend's `ShortcutOpen` takes an
  `exclusive` flag.
