- **An application can run on a runtime it does not carry**
  (`ULTRACANVAS_RUNTIME_DIR`, `kRuntimeDirVariable` in
  `UltraCanvasConfig.h`). UltraStore installs an app beside the runtime
  component it was built against and names that folder when it starts it;
  `GetResourcesDir()` takes the runtime's `share/` (`Resources/` on Windows)
  when the app's own folders have no `media/`, and UltraNet's default
  plug-in folder the runtime's `bin/Plugins/UltraNet` (`Plugins/UltraNet`
  on Windows) when the app's have no plug-in. The app's own come first.
- **`LaunchDetachedProcess` takes an environment for the started program**
  (an overload, as `RunProcessCaptured` takes it): set in the detached child
  on POSIX, an environment block on Windows. The Windows block is built by
  one helper for both.
- **UltraNet looks in a macOS bundle's `Contents/PlugIns/UltraNet`** before
  the folders beside the executable: where `package-macos.sh` now puts the
  plug-ins, spelled with its capital I, which the walk up from the
  executable found only on a volume that ignores case.
- **UltraMail is in the macOS suite** (`package-macos.sh`), with UltraNet's
  plug-ins in its `Contents/PlugIns/UltraNet/` (`ultranet_plugins_of`) and
  their libraries in the shared `Frameworks/`; it had been built on macOS and
  never packaged. Every Mach-O file under a bundle's `PlugIns/` is signed,
  not only the top-level `.dylib`.
- **`package-macos.sh --store-app <App>`** cuts any app of the suite out on
  its own, into `dist-store-macos/` - the rewrite `--start-app` makes for
  UltraCanvasStart, with the translations and emoji fonts kept and every
  plug-in's closure in the bundle's own `Frameworks/` - for UltraStore. The
  proof run of an app on its own (`--version`) has a time limit now: an app
  that does not know the option opens its window and never ends, and only
  the loader refusing a library fails the package (UltraCanvasStart must
  still answer).
