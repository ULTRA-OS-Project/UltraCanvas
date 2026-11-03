- **`videofx render` draws UltraVideo's titles as UltraVideo shows them.** The
  command-line tool drew every title with FFmpeg's text filter: one font
  file, plain, left-aligned - a title with a font of its own, a bold or
  italic word, underline, alignment or spacing came out plain. Now:
  - VideoFX 0.18.0: a text overlay carries that formatting
    (`VideoFXOverlay::fontFamily`, `richText` / `richTextHeight`, `textAlign`,
    `lineSpacing`, `letterSpacing`, saved in project files), and a host's text
    renderer (`VideoFX_SetTextRenderer`) draws such an overlay into a picture
    laid over the frame - asked on the calling thread by
    `VideoFXExportJob::Start`, `VideoFX_Export` / `VideoFX_RenderProject` and
    `VideoFXPreview::Open`, never on the export's own thread
    (`VideoFX_DrawTextOverlays`). VideoFX stays free of UltraCanvas; built on
    its own it draws these titles plain, as before.
  - `VideoFXUltraCanvasText` (`VideoFX/ultracanvas/`), a library beside
    VideoFX: UltraCanvas's text engine as that renderer - the drawing that
    was UltraVideo's own (`UltraVideoTitles`), now shared.
    `VideoFX_UseUltraCanvasText()` installs it; UltraVideo does, and the
    `videofx` tool does when built in the UltraCanvas tree (`videofx
    --version` says which).
  - The tool reads UltraVideo's `.ultravideo`: a program's file that carries
    the engine's project under `"project"` is read for it. It never could -
    `videofx render trip.ultravideo` said "Not a VideoFX project file",
    though UltraVideo's README promised it.
  - **One font registration per platform.** Each platform's start-up font
    set-up moved out of its application class into
    `RegisterPlatformBundledFonts()` (`OS/<Platform>/`): fontconfig on Linux,
    the BSDs, Android and in the browser, CoreText for this process only on
    macOS, and on Windows the Windows font folders, the generic aliases, the
    bundled fonts through fontconfig and GDI and a fontconfig-backed Pango
    font map. Every application's `LoadBundledFontsNative` calls it, and so
    does `RegisterBundledFontsWithoutApplication()` - what a tool that draws
    text, with no `UltraCanvasApplication`, calls instead: the generated
    fonts.conf where the system has none, then the platform's registration,
    waiting on Windows for the system fonts' scan (a tool has no windows to
    switch over later). So `videofx` sets titles in the bundled fonts and the
    system's on every platform, macOS included, from the code the
    applications run - no second copy.
  - `UltraVideoDocumentTest` checks that the saved project carries the
    formatting the tool reads, and that the engine lays over the frame the
    very picture the program draws.
