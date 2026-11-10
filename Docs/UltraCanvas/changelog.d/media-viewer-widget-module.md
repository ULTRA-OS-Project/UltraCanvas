- **The MediaViewer can be a runtime module.** It joins the selectable
  widgets in `ULTRACANVAS_WIDGETS` as `"media-viewer"`
  (`MakeMediaViewerDescriptor`, `RegisterMediaViewerElement`). It is still
  compiled in by default. Left out, it is built as
  `ultracanvas-widget-media-viewer`, together with
  `UltraCanvasMediaViewerWindow`. That happens only with a shared core, the
  shipping configuration. The viewer reaches about 45 core objects, and a
  static host would have to keep all of them for it. UltraViewer, UltraFiler, UltraMail,
  EmailCleaner, ChatCube, UltraClipboard, UltraSnip and DemoApp use its
  classes, so the configure stops if one of them is built without it.
  Widget modules are now compiled with the core library's own include paths
  and compile definitions, so a feature switch means the same on both sides of
  the module boundary. The Element modules CI job now runs twice, with a static
  and with a shared core, and builds the viewer's module in the shared one.
