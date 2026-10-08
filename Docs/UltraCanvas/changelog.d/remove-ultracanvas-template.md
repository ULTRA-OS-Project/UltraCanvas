- **`UltraCanvasTemplate` is removed.** The layout-template class
  (`UltraCanvasTemplate.h` / `.cpp`, with its builder and presets) was never
  named in a build file in the repository's history and no longer compiled
  (61 errors against today's API). Its only examples lived in
  `Apps/UltraCanvasToolbarExample.cpp`, a standalone program that was never
  built either and was deleted in May 2026. What it offered exists in working
  form: `UltraCanvasToolbarBuilder` and `ToolbarPresets` (toolbar, status
  bar, ribbon, sidebar), flex and grid layout on every container,
  `PlaceChildAt` for absolute placement, a movable toolbar, and
  `UltraCanvasElementPlugins::Create(typeName)` for creating elements by
  name. Its placement engine wrote children's bounds directly, which the
  layout pass overwrites, and it painted its own drag handle. The element
  catalogue's exemption list called it "the skeleton new elements are copied
  from"; that line is gone with it.
