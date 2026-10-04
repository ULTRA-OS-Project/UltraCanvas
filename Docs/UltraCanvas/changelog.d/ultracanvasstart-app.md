- **UltraCanvasStart, the setup application, joins the build.** `Apps/UltraCanvasStart`
  (`BUILD_ULTRACANVASSTART`, on by default) with its engine test suite
  (`ULTRACANVAS_BUILD_ULTRACANVASSTART_TESTS`, run by CI), the product entry in
  `cmake/UltraCanvasVersion.cmake`, the icon under `media/appicon/` and a
  `--check` smoke run on every CI leg. See `Docs/UltraCanvasStart/CHANGELOG.md`.
