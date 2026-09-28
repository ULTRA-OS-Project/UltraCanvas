- **The macOS package shrinks by about 560 MB.** `package-macos.sh` copied all
  of `media/` (121 MB) into every `.app`, and with six bundles the artifact
  reached 920 MB against Linux's 190 MB, which ships `media/` once. About
  112 MB of it is the DemoApp's sample content (3D models, videos, pictures,
  vector drawings, sound, e-books), which only the demo opens. Only
  `UltraCanvasDemo.app` carries it now - and the demo's example sources -
  and the other bundles get the runtime part of `media/` (icons, fonts,
  MicroTeX, OCR data, app icons, ...), 9 MB each. The samples are an exclusion
  list (`DEMO_SAMPLE_MEDIA`), so a runtime folder added later is shipped by
  default.
- **The macOS packager strips its binaries,** as the Linux one does:
  `strip -S -x` on every executable, plug-in and bundled dylib, after the
  install-name rewrites and before signing. It keeps the global symbols, which
  the dlopen()ed LaTeX module binds to; only the debug map and the local
  symbols go. Each bundle's log line reports the before/after size.
