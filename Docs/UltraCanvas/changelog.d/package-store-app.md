- **`scripts/package-store-app.sh <App>`: one application as an UltraStore
  package**, the app on its own: everything it loads in one folder, so it
  starts in any store, one from before the runtime layer too. The script
  cuts it out of the suite
  package `package-linux.sh` or `package-win.sh` made, the way
  `package-ultracanvasstart.sh` cuts UltraCanvasStart: the executable and
  its launcher, the plug-in families it loads (UltraIM with ChatCube alone),
  the closure the loader resolves inside the suite with libvips', libheif's
  and ImageMagick's run-time modules where those are in it, the media the
  framework loads and the application's own data. It checks the package
  from inside (every library resolves within it; an application that
  answers `--version` runs from it) and ends with the `ultrastoreadmin`
  lines, the entry and the glibc variant spelled out. The version is line 1
  of the application's changelog, the id `org.ultraos.<app>`.
  - CI's Linux and Windows builds make UltraMail's package on every run and
    upload it as `UltraMail-store-<os>-<arch>`.
- **`package-store-app.sh --runtime` and `<App> --on-runtime`**: the
  UltraCanvas runtime as the store component `org.ultraos.runtime` at the
  framework's version - the core library and its closure, the plug-in
  families every app may load, the run-time modules and the media, laid out
  at the top of the archive as the component's folder - and an app without
  what that runtime carries, its release depending on it: a few megabytes
  instead of a hundred. The app's launcher takes libvips', libheif's and
  ImageMagick's module folders from the runtime's (`ULTRACANVAS_RUNTIME_DIR`)
  when it has none of its own, and an app on the runtime is run from the
  staged runtime with the environment the store gives it before the package
  is declared done. On macOS the script zips the bundle `package-macos.sh
  --store-app` made, checks its signature and prints its `--min-os`.
  - CI makes UltraMail's package on macOS too, and on Linux and Windows the
    runtime and UltraMail on it (`UltraCanvas-runtime-store-<os>-<arch>`).
