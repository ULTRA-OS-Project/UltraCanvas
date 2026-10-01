- **Configuring with `BUILD_DEMO_APP=OFF` failed on `fmt::fmt`.** The core
  library resolved fmt with `find_package`, whose imported target is visible
  only inside `UltraCanvas/`; the applications added from the top level
  (Texter, UltraFiler, UltraViewer) link the same name and stopped the
  generate step with "target fmt::fmt not found". It only ever worked because
  the demo app's own FetchContent happened to supply a global target. The
  core now promotes the found target to global (`IMPORTED_GLOBAL`), so every
  application sees it whether or not the demo is built, and the demo's
  duplicate lookup is gone. Packagers can build without the demo.
- **The build tree finds the resources, on every desktop platform.**
  `GetResourcesDir()` looked for `media/` only in the packaged place
  (`<exe dir>/share/` on Linux, `exe/Resources/` on Windows, the bundle's
  `Contents/Resources/` on macOS), which no build tree had: an application
  started from `build/` found no icons, fonts or wallpaper until someone
  linked or copied the resources by hand. `SetResourcesDir` (1.1.0) now
  probes the packaged place first and then `<exe dir>/share/` and
  `<exe dir>/../share/` on Windows and macOS as it already did on Linux, and
  configuring links `build/share/media` and `build/share/Docs` to the
  repository's directories (a symlink; on Windows a directory junction when a
  symlink needs privileges the build does not have, and a copy as the last
  resort). Links, not copies, where possible: `media/` is over 100 MB and a
  link follows edits. The probe goes through `PathFromUtf8`, so a build path
  with non-ASCII characters works on Windows too.
