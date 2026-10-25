- **A Windows crash report names the functions of the application's own
  `.exe`.** The function beside each frame came from the module's export
  table, and an application's `.exe` exports nothing, so its frames were bare
  offsets: ArtCreator's crash on picking the Shape Editor read
  `ArtCreator.exe+0x60B98` five times over and could not be traced without
  the matching binary. The reporter now also reads the COFF symbol table that
  a MinGW link (GNU ld or lld) keeps in every unstripped `.exe` and `.dll`,
  from the module's file and only once the file's header shows it is still the
  loaded module, and takes the nearer of the two names. That names every
  function of an `.exe` and, in any module, code with internal linkage the
  export table never had: lambdas (`ArtCreatorWindow::BuildToolPalette::{lambda}`),
  classes in an anonymous namespace (`(anonymous namespace)::ShapeEditorTool::Load`),
  `static` functions. A stripped binary still gives the module and offset.
