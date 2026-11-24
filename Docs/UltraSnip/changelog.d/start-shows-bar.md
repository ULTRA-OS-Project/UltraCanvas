- **Starting UltraSnip while it runs in the background no longer says
  "UltraSnip is already running".** When the copy UltraDesktop keeps resident
  could not be reached (a build without the UltraMessage bus), a start from
  the applications showed that message and no window at all. It now opens
  the bar itself, as a start without a running copy does; a `--background`
  start still leaves the resident copy alone and shows nothing.
- **The bar opens on the screen the mouse pointer is on.** At the top
  centre of that monitor, over the dimmed screen, rather than always on the
  primary one - also when a resident UltraSnip's closed window comes back.
  On Wayland and macOS, which do not say where the pointer is, it stays the
  primary screen.
