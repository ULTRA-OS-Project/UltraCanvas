- **Slideshow, Album, ColorPaletteManager and VectorCanvas can be runtime
  modules too.** They join VideoTimeline in `ULTRACANVAS_WIDGETS`, registered
  as `"slideshow"`, `"album"`, `"color-palette-manager"` and
  `"vector-canvas"`. They are still compiled in by default. Left out, each is
  built as `ultracanvas-widget-<name>`. A widget can now name several anchors
  (`_ANCHORS`): functions, member functions or `&typeid(Class)` for the core
  objects its module needs, which `UltraCanvas_RegisterSelectedWidgets()`
  keeps in a static host. `ultracanvas_require_widgets` stops the configure
  when DemoApp, UltraVideo, UltraPaint or ArtCreator would be built without a
  widget it uses. `VideoTimelinePluginTest` is now `ElementModulesTest`,
  covering every selectable widget and the chart module.
- **Chart runtime modules could never be loaded.** They were compiled without
  `generated/include`, so they reported their framework headers as `unknown`
  and the loader refused them. They now carry the build's id, and
  `ElementModulesTest` loads `ultracanvas-chart-parallel-coordinate`.
