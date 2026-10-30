- **UltraCanvasDesktopShell ends the session and locks the screen on Linux,
  and reads and sets the loudspeaker's volume.** `EndSession` goes through
  logind on the system bus - `Reboot(true)` and `PowerOff(true)`, which ask
  polkit as the desktops' own menus do, and `Terminate` on the program's own
  session to sign out - so `CanEndSession()` is true wherever logind answers,
  not only on Windows. The new `CanLockSession()` / `LockSession()` lock the
  screen: `LockWorkStation` on Windows; on Linux logind's `Session.Lock`,
  which sends the signal the screen locker waits for, and without logind
  `org.freedesktop.ScreenSaver.Lock` on the session bus. The new
  `ReadAudioOutput()` (a `DesktopAudioOutput`: the default output's name,
  volume and mute), `SetAudioOutputVolume()` and `SetAudioOutputMuted()`
  talk to PulseAudio or PipeWire through `libpulse.so.0` loaded at run time
  - no build dependency, no autospawn, a two-second deadline - and on
  Windows to the default render endpoint's `IAudioEndpointVolume`. The
  files: `OS/Linux/UltraCanvasLinuxDesktopShellSession.cpp` and
  `UltraCanvasLinuxDesktopShellAudio.cpp`.
- **A tap of the Super key is a global shortcut on X11 too.**
  `UltraCanvasGlobalShortcut::Start("Super", ...)` refused on X11 because a
  grab of the bare key would have taken every Super+X combination from the
  window manager and the other programs. It now watches XInput2's raw key
  events on the root window instead, which any client may without a grab,
  and reports a press and release of either Super key with no other key
  between - as the Windows hook does. The X11 build links `xi` (libxi-dev,
  which GTK 3's development package already depends on; added to the CI and
  cloud-session package lists all the same).
- **A tray icon's menu as data.** `DesktopTrayIcon::menu` holds the icon's
  menu as `DesktopTrayMenuItem`s (label, icon, enabled, separator, check
  mark, submenu) where the program hands it over rather than drawing it -
  the StatusNotifierItem's dbusmenu on Linux - and
  `UltraCanvasTrayHost::ActivateMenuItem(id, itemId)` reports the choice.
  Empty on Windows, where the right click opens the program's own menu, as
  before.
