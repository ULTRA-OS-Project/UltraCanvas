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
  - `RegisterBundledFontsWithoutApplication()`: a tool that draws text, with
    no `UltraCanvasApplication` to do it at its start, sets text in the
    framework's bundled fonts (titles' default, Ubuntu) as the applications
    do - on the fontconfig platforms; on macOS it sees the installed fonts.
  - `UltraVideoDocumentTest` checks that the saved project carries the
    formatting the tool reads, and that the engine lays over the frame the
    very picture the program draws.
