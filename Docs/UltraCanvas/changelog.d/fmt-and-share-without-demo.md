- **Configuring with `BUILD_DEMO_APP=OFF` failed on `fmt::fmt`.** The core
  library resolved fmt with `find_package`, whose imported target is visible
  only inside `UltraCanvas/`; the applications added from the top level
  (Texter, UltraFiler, UltraViewer) link the same name and stopped the
  generate step with "target fmt::fmt not found". It only ever worked because
  the demo app's own FetchContent happened to supply a global target. The
  core now promotes the found target to global (`IMPORTED_GLOBAL`), so every
  application sees it whether or not the demo is built, and the demo's
  duplicate lookup is gone. Packagers can build without the demo.
- **The build tree finds the resources.** `GetResourcesDir()` looks for
  `media/` at `<exe dir>/share/`, the installed and packaged layout, which no
  build tree had: an application started from `build/` found no icons, fonts
  or wallpaper until someone linked `share/` by hand. Configuring on Linux
  and the BSDs now links `build/share/media` and `build/share/Docs` to the
  repository's directories (links, not copies: `media/` is over 100 MB and a
  link follows edits).
