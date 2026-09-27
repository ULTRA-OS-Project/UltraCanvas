- **Every application now handles file names in any script on Windows.** The
  UltraFiler fix (a name outside the Windows code page ended the program with
  *filesystem error: in __wide_to_char: Illegal byte sequence*) is now applied
  everywhere. About 500 more conversions between `std::filesystem::path` and
  `std::string` go through UTF-8 instead of the code page. They are in Texter,
  UltraCleaner, UltraMail, EmailCleaner, UltraSocial, UltraPaint, ArtCreator,
  UltraAuthenticator, UltraNetMonitor, the DemoApp, VirtualFS, UltraCloud,
  UltraVault, UltraWin, UltraMessage, SmartHome, the plug-in loaders and the
  document plug-ins (LaTeX, Word, OCR, CDR).
  - `PathToUtf8` / `PathFromUtf8` moved from `UltraCanvasUtils` to the new
    header-only `UltraCanvasPathUtf8.h` (C++17, no link dependency), so
    headless engines and VirtualFS can use them. `UltraCanvasUtils.h`
    includes it, so existing callers are unchanged. On Windows the
    conversion is now done in the header and never throws: invalid UTF-8,
    or an unpaired surrogate in an NTFS name, becomes U+FFFD. The new
    `OpenFileUtf8` replaces `std::fopen` for a UTF-8 path; on Windows it
    goes through `_wfopen`.
  - The plug-in loaders (`UCPluginOpen`, the UltraNet and UltraCloud
    registries) load with `LoadLibraryW` instead of `LoadLibraryA`, so a
    plug-in folder under a non-ASCII user name loads.
    `DescribeFileReadError`, `DescribeFileWriteError` and
    `WriteFileAtomically` (`UltraCanvasFileError.h`) now also open UTF-8
    paths correctly.
  - New rule in `AGENTS.md`: file paths are UTF-8 in every application.
    `scripts/check_path_string.py` enforces it in CI (`path-strings.yml`). It
    flags `.string()`, `.generic_string()` and `fs::path(std::string)`, and a
    site that is correct as it stands (a path built from a wide string) says
    so with `// path-string-ok: <why>`. The baseline is empty.
  - New test `PathUtf8Test` covers Thai, Cyrillic, CJK, emoji and NFD round
    trips, every kind of malformed UTF-8, unpaired surrogates, and a real
    file created, listed and opened under a Thai-plus-emoji name. The
    Windows build of the test passes under Wine.
