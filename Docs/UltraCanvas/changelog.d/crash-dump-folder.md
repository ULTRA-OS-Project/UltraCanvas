- **The crash dump folder can be found by other code.**
  `UltraCanvasCrashDumpFolder()` (new header `UltraCanvasCrashDumpFolder.h`,
  header-only) returns the folder the Windows crash reporter writes its
  minidumps to: `ULTRACANVAS_CRASH_DUMP_DIR` when set, else
  `%LOCALAPPDATA%\UltraCanvas\CrashDumps`, and empty on the platforms that
  write none. The pure `CrashDumpFolderFor(platform, customDir, localAppData)`
  is the testable rule. The crash reporter now reads the folder from there,
  so a tool that shows it to the user (UltraFiler's Extras > Open location)
  cannot disagree with it. A folder named by `ULTRACANVAS_CRASH_DUMP_DIR` now
  has its missing parents created; before, the dump was silently skipped.
  Covered by `DataFolderTest`.
