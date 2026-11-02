- **A Wayland windowing backend, behind `-DULTRACANVAS_WAYLAND=ON`.** The
  framework runs as a native Wayland client: `UltraCanvasApplication` and
  `UltraCanvasWindow` are then `UltraCanvasWaylandApplication` and
  `UltraCanvasWaylandWindow` (`OS/Linux/Wayland/`), every window a
  `wl_surface` with an `xdg_toplevel` drawn through shared-memory buffers
  at the output's scale, the seat's keyboard through xkbcommon (keymap,
  modifiers, key repeat) and its pointer (buttons, double clicks, wheel
  notches, smooth scrolling folded into notches), the cursor from the
  theme through libwayland-cursor, and the clipboard the seat's
  `wl_data_device` with the same MIME types as the X11 backend, so a copy
  pastes between the two sessions alike. Server-side decorations are asked
  for an ordinary window and none for a borderless one; maximize,
  fullscreen and minimize go through xdg-shell. The default stays X11: one
  library speaks one display protocol, and a consumer of the Wayland build
  sees `ULTRACANVAS_DISPLAY_WAYLAND=1`. Dependencies: libwayland-client,
  libwayland-cursor, wayland-protocols, libxkbcommon and wayland-scanner,
  which generates the xdg-shell and xdg-decoration bindings into the build
  directory. GL surfaces are off in a Wayland build for now (the context
  managers are GLX and EGL-on-Xlib), and so are file drags out of a window;
  a client neither sets nor learns its windows' positions on Wayland, so
  `SetWindowPosition` keeps the wish and `RaiseAndFocus` waits for
  xdg-activation. The decision behind it and the plan for the desktop
  shell's own protocols: `Docs/UltraCanvas/WaylandDesktopShellProposal.md`.
- **UltraCanvasDesktopShell on Wayland: the `wayland` backend, phase 1.**
  `ListScreens` and `GetScreenSize` from the compositor's outputs (the
  first announced counts as the primary), the devices from the shared
  Linux probes, the keyboard layout from the seat's keymap; the session's
  end and lock, the loudspeaker and the notification area are the shared
  Linux files and work as on X11 (a tray click's position is the last
  pointer position in the shell's own window). The window list, virtual
  desktops, reserved edges, screenshots and global shortcuts answer false
  with "not available on Wayland yet" until the layer-shell and
  foreign-toplevel protocols and the portals are in (phase 2 of the
  proposal).
- **The X11-free parts of the Linux backend are shared files.** The
  fontconfig and bundled-font code (`OS/Linux/UltraCanvasLinuxFonts`), the
  keysym-to-`UCKeys` table (`UltraCanvasLinuxKeys`), the procfs and sysfs
  device probes (`UltraCanvasLinuxDeviceProbes`) and the file-URI helpers
  (`UltraCanvasLinuxUriList`) moved out of the X11 application, desktop
  shell and clipboard files, which now call them; the X11 build is
  unchanged. `UltraCanvasLinuxNativeDialogs.cpp` builds without X11 (the
  GTK dialogs stay above instead of transient for a Wayland window).
