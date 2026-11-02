- **ULTRA OS's own wallpaper.** The desktop starts with `media/desktop/ULTRA_OS_A.jpg`
  (2752 × 1536, the picture supplied for ULTRA OS, converted from HEIC to
  JPEG so every build reads it) in place of the framework's landscape, which
  stays the fallback where a package has no `media/desktop`. A wallpaper set
  in the settings file is used as before.
- **The wallpaper comes up from the disk cache.** The desktop's wallpaper
  is decoded and fitted to the monitor once; the fitted picture is kept in
  the framework's thumbnail disk cache as a QOI blob (framework changelog:
  `opaque-pixmaps-formats`), and the next start inflates it in 10 ms on a
  1280 × 800 monitor (75 ms at 3840 × 2160) instead of decoding and
  resizing the JPEG (40 ms and 155 ms). As a picture
  without an alpha channel it is held without one (RGB24).
