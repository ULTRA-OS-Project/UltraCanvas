- **A clock on the right bar.** The framework's gauge element in its analog
  clock mode sits at the top of the right bar, the date under it and the
  whole date and time in its tooltip. A click opens the clock panel: the time
  large as an analog face and in figures, today in words, and the month on
  the calendar view with today marked. Escape or a click elsewhere closes it.
- **Pinned applications.** Right-click a running window's taskbar button, or
  a tile in the Applications window, and choose *Pin to taskbar*: the
  application joins the pinned group after the RAM disc and UltraFiler, with
  its own icon, one click to start, and *Unpin from taskbar* on its right-click
  menu. The pins are kept in the settings file (`pinnedApps`) by the
  program's path - the shortcut or the Store application's id on Windows -
  so they survive the application being renamed, and a pin whose application
  is gone shows again when it is back.
- **The loudspeaker's volume.** A click on the speaker icon opens the volume
  panel: the output's name, its volume on a slider and its mute on a switch,
  through the sound server (PulseAudio or PipeWire on Linux, Core Audio on
  Windows); where none answers the panel says so. The icon's tooltip names
  the volume ("Loudspeaker: 63%, playing"), a red dot marks it muted, and a
  change made elsewhere - the keyboard's volume keys - shows within ten
  seconds. The Task Manager is still one click away on its own icon.
- **Lock screen, and sign out, restart and shut down on Linux.** The settings
  button's right-click menu offers *Lock screen* (also Super+L) and, on
  Linux through logind as on Windows as the shell, *Sign out*, *Restart* and
  *Shut down*, each asked first. (Framework changelog:
  `desktopshell-session-audio-super-tap`.)
- **A tap of the Super key opens Applications on Linux too**, where the
  session has no other desktop to own the key; beside Explorer on Windows it
  stays the Start menu's.
- **The desktop sits on the primary monitor on Linux too.** One window
  across the whole X11 screen put the bars on the far edges of two monitors
  and, with monitors of different sizes, into the dead area where the
  shorter one ended, with the info panel out of sight. The desktop window
  now covers the primary monitor (as it did as the Windows shell) and every
  other monitor gets a wallpaper window; the clipboard, clock and volume
  panels and the notification toasts stay on the primary, and a primary
  that moves while the desktop runs is followed within seconds (the desktop
  window is made anew there and the old one closed). The bars are
  reserved with the window manager only along the edges of the whole screen
  the primary lies on (framework changelog:
  `desktopshell-session-audio-super-tap`).
- **Tray icons' menus on Linux.** With the framework's new StatusNotifierItem
  tray host (framework changelog: `desktopshell-linux-tray-host`), the
  programs' tray icons appear on the info panel on Linux as they do instead
  of Explorer on Windows, and a right click draws the icon's own menu, with
  its submenus and check marks, as a desktop menu.
