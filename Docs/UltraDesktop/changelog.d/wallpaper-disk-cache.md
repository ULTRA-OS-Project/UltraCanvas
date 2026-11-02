- **The wallpaper comes up from the disk cache.** The desktop's wallpaper
  is decoded and fitted to the monitor once; the fitted picture is kept in
  the framework's thumbnail disk cache as a QOI blob (framework changelog:
  `opaque-pixmaps-formats`), and the next start inflates it in 10 ms on a
  1280 × 800 monitor (75 ms at 3840 × 2160) instead of decoding and
  resizing the picture. A picture without an alpha channel is held
  without one (RGB24).
