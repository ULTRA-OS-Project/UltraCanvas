- **`scripts/package-store-app.sh <App>`: one application as an UltraStore
  package.** The store unpacks a package into `apps/<id>/<version>/` and
  starts its entry with nothing on the library path, so an application has
  to bring the libraries it loads. The script cuts it out of the suite
  package `package-linux.sh` or `package-win.sh` made, the way
  `package-ultracanvasstart.sh` cuts UltraCanvasStart: the executable and
  its launcher, the plug-in families it loads (UltraIM with ChatCube alone),
  the closure the loader resolves inside the suite with libvips', libheif's
  and ImageMagick's run-time modules where those are in it, the media the
  framework loads and the application's own data. It checks the package
  from inside (every library resolves within it; an application that
  answers `--version` runs from it) and ends with the `ultrastoreadmin
  package add` line, entry and glibc variant spelled out - on Linux the
  admin backend would otherwise suggest the bare executable, which does not
  find `lib/`. The version is line 1 of the application's changelog, the id
  `org.ultraos.<app>`. macOS is not covered yet.
  - CI's Linux and Windows builds make UltraMail's package on every run and
    upload it as `UltraMail-store-<os>-<arch>`.
