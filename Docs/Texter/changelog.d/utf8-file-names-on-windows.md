- **Files with Thai, CJK or other non-Western names now open on Windows.**
  When Explorer's *Open with*, the MSIX package's file types or a second
  start hands Texter a file, the name arrives as `main()`'s narrow argument,
  which Windows encodes in the ANSI code page. A name that code page could not
  hold arrived as `?` and did not open. Texter's executable now embeds the
  framework's manifest with the UTF-8 code page, as UltraViewer's does
  (`ultracanvas_embed_windows_manifest(Texter)`), so the name arrives intact
  on Windows 10 1903 and later. The MSIX package requires 1903 or later for
  this reason.
