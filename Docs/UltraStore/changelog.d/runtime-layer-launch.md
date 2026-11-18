- **Open starts an app on its runtime layer.** The store installed an app's
  runtime and libraries beside it and then started the app with nothing on
  its library path, so an app whose package left the runtime out installed
  and did not start - and every package had to carry the whole runtime, a
  hundred megabytes each. `PlanLaunch` (`StoreQuery.h`) now puts the
  components the app was installed with - the exact versions of its
  `depends`, and theirs in turn - first on `LD_LIBRARY_PATH` (a component's
  `lib/`) or `PATH` (its folder, on Windows), and names the runtime's folder
  in `ULTRACANVAS_RUNTIME_DIR`, where the framework finds media and
  UltraNet's plug-ins after the app's own. A self-contained app (no
  `depends`) starts as before; on macOS nothing is added, an app there being
  a bundle with its own `Frameworks/`. The README's *Packaging an
  application for the store* describes the three package shapes of
  `scripts/package-store-app.sh`.
