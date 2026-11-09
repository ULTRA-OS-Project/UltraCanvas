- **CI builds the widget and chart modules.** A new workflow,
  `.github/workflows/element-modules.yml`, configures the lean build (no
  widget and no selectable chart compiled in) on ubuntu-24.04. It builds the
  core and every `ultracanvas-widget-*` and `ultracanvas-chart-*` module, and
  runs `ElementModulesTest`, `UltraCanvasThemeTest` and `VectorEditTest`
  against them. It also checks that the configure refuses ArtCreator when its
  widgets are left out. `build.yml` compiles every widget in, so before this
  the modules were never built in CI. The chart modules' missing framework
  headers id went unseen that way.
