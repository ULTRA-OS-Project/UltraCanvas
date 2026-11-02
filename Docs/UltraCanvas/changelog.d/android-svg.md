- **Android reads SVG.** The Android dependency build now gives libvips its
  `rsvg` feature, as on macOS: vcpkg's librsvg 2.40, the last C release (no
  Rust), with gdk-pixbuf, libcroco and libxml2. Without it every `.svg` -
  the demo's icons among them - failed on the emulator with "is not a known
  file format". The core also picks librsvg up through pkg-config for its
  parse-once SVG documents, as on the desktop. The emulator smoke test now
  fails when any SVG fails to load.
