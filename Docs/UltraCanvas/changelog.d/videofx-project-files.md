- **VideoFX 0.7.0: project files.** A whole edit can be saved as a
  `.vfxproj`, a small JSON file. It holds a timeline (or a slideshow's photos
  and options), its music and its export settings, and refers to media by
  path. New: `VideoFXProject` (`FromTimeline`, `FromSlideshow`),
  `VideoFX_SaveProject`, `VideoFX_LoadProject`, `VideoFX_ProjectToJson`,
  `VideoFX_ProjectFromJson`, `VideoFX_RenderProject` and
  `kVideoFXProjectFormatVersion` (1).
  - Media inside the project's folder are stored relative to it, so the
    folder can move as a whole. Loaded paths come back absolute.
  - Loading reports media that have gone (`missingMedia`) and still loads.
  - A file from a newer VideoFX is refused rather than half-read. Unknown
    keys are ignored and missing keys keep their defaults.
  - Errors name the place in the file, such as
    `segments[2].transitionIn.type`.
  - Saving is atomic. Images held only in memory are refused.
  - Works without FFmpeg too; only rendering needs the engine.
  - `videofx`: `--save-project FILE` on any edit command, `videofx project`
    (summary and missing media) and `videofx render` (render a project).
- **VideoFX is a shared library.** It is built as `libVideoFX.so`,
  `libVideoFX.dylib` or `libVideoFX.dll` instead of a static archive. It is
  not part of the UltraCanvas core, so FFmpeg is loaded only by applications
  that do video, and they share one copy. The soname carries the minor
  version (`libVideoFX.so.0.7`) while VideoFX is 0.x. On Windows every symbol
  is exported, as in the core, and the DLL goes to `bin/` beside `videofx.exe`.
  `-DVIDEOFX_BUILD_SHARED=OFF` keeps a static build (the default on Android).
  The Linux package leaves the library out until an app links it.
