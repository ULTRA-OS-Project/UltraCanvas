- **The desktop's look on Linux and in the browser (T2 of the host theme
  proposal).** `GetHostAppearance()` on X11 and Wayland reads the portal's
  colour scheme over the session bus, GSettings (`color-scheme`,
  `gtk-theme`, `accent-color`, `text-scaling-factor`, `enable-animations`,
  `high-contrast`), the XSETTINGS manager's property (X11: `Net/ThemeName`,
  `Gtk/EnableAnimations`), `~/.config/kdeglobals` (the scheme, the window
  background's luminance, `AccentColor`, the animation factor) and
  `gtk-4.0/` or `gtk-3.0/settings.ini` - the KDE file first in a KDE
  session, the portal and GSettings only inside a desktop session, so a run
  under Xvfb stays light in the bundled fonts. A change reaches the
  handlers through the manager's `PropertyNotify` and a watch on the
  config folders (dconf's database, `kdeglobals`, the GTK folders).
  WebAssembly answers `prefers-color-scheme`, `prefers-contrast` and
  `prefers-reduced-motion`, and their `change` events. The readers are pure
  (`HostAppearanceLinux`), covered by `HostAppearanceTest`;
  `LinuxHostAppearanceTest` runs the detector on a made-up config home.
