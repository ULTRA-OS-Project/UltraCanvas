- **UltraCanvasSessionLock: a call when the desktop session locks.**
  `AddSessionLockListener(onChange)` reports the screen locking - by hand or
  by the screensaver - and unlocking again, on the UI thread, once per
  change, so a program can forget what should not outlive the user stepping
  away. Linux and the BSDs hear logind's `Lock` / `Unlock` and `LockedHint`
  for their session and the screensaver's `ActiveChanged`
  (`org.freedesktop.ScreenSaver`, `org.gnome.ScreenSaver`) through GDBus on
  the application's own loop; Windows its WTS session notifications; macOS
  the loginwindow's `com.apple.screenIsLocked` / `screenIsUnlocked`. The
  platform is watched only while a listener is registered;
  `NotifySessionLockChanged` lets a host with a lock of its own, or a test,
  report one. `ULTRACANVAS_HAS_NATIVE_SESSION_LOCK` (PUBLIC) is set where a
  backend exists. See `UltraCanvasSessionLock.md`.
- **The media viewer forgets Unlock's passwords when the user steps away.**
  When the session locks, or its window has had no key or mouse input for ten
  minutes (`SetForgetPasswordsAfterIdle`, zero for never;
  `ForgetPasswordsIfIdle`), the kept passwords are forgotten and a file shown
  unlocked is shown locked again - `ForgetPasswords()` now does both. The
  viewer listens only while it holds a password or an unlocked file.
  - A file opened with its password says so: *unlocked* beside *PDF* or
    *UC DOCUMENT* in the info bar, the encryption in a PDF's Details, and
    `IsUnlocked()`.
  - `SetSettingsAction(action)`: a gear at the right end of the info bar for
    the host's settings, hidden until one is set - UltraViewer's *Forget
    passwords after* lives behind it.
