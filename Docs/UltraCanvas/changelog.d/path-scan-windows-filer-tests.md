- **The Filer tests that run on Windows are checked for code-page path
  conversions.** `scripts/check_path_string.py` scans only the tests that run
  on Windows, and of the five Filer tests the Windows job runs it covered
  none until `FilerHistoryTest` - so a `.string()` or `fs::path(std::string)`
  added to `FilerFolderPreviewTest`, `FilerHostIconsTest`,
  `FilerNameEncodingTest` or `FilerShortcutEntryTest` would have passed the
  check and failed only on a Windows machine with a name its code page cannot
  hold. All five are scanned now, and `path-strings.yml` runs when one of
  them changes - before, a change to a test file alone never started it. The
  two sites in `FilerNameEncodingTest` that write a raw Latin-1 file name on
  POSIX on purpose say so with `// path-string-ok`.
