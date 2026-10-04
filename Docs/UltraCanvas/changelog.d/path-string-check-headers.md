- **`check_path_string` sees what a header declares, a call that returns a
  string, and a call over several lines.** The device-key vault's path
  conversions went through the Windows code page unseen, because the check
  read only the file's own declarations and never treated a call as a string:
  `fs::exists(DeviceKeyPath(), ec)` (a function declared in the header as
  returning `std::string`), `fs::create_directories(dir_, ec)` (a member
  declared there) and a three-line `fs::permissions(DeviceKeyPath(), ...)`
  all passed. The check now also reads the repository headers a file includes
  directly - their declarations, with inline function bodies left out so a
  header's locals lend their types to nothing - counts a call to a function
  declared as returning `std::string` as a string wherever it is handed to a
  path, and reads a call that is left open at the end of a line together with
  the lines that close it. `--self-test` checks those rules against examples
  of their own (and fails when either is switched off); `path-strings.yml`
  runs it before the scan. Run on the pre-fix vault source, the check now
  reports every one of its sites; the old one reported it clean.
- **The 37 sites it found are fixed, and the baseline stays empty.** Among
  them, real failures on a Windows profile named outside the code page:
  - UltraFiler's and the UltraAI dashboard's configuration folders were read
    with the narrow `getenv("APPDATA")`, which answers in the ANSI code page,
    and handed on as UTF-8 to code that opens them as UTF-8 (SQLite, the
    vault, the JSON file helpers). They are read wide now (`_wgetenv`) and
    converted with `PathToUtf8`, and every file in them is opened through
    `PathFromUtf8`.
  - VirtualFS's Windows RAM-disc fallback took its folder from
    `GetTempPathA`; it uses `GetTempPathW` now, and runs `icacls` through
    `_wpopen`, so a UTF-8 path on the command line is not read in the code
    page.
  - UltraWin's associations file and environment manifests, the Filer
    widget's shortcut-target check and its chunked copy's `fs::permissions`,
    a `.git` file's `gitdir:` pointer, the LaTeX reader's `\graphicspath`,
    the safe-save temporary name (`UltraCanvasFileError.h`, whose extension
    is the target's own) and the CorelDRAW converter's temporary name, the
    UltraCloud plug-in folder, a RAM disc's mount check, the Z-Wave
    controller path, the DemoApp's Up button, UltraAuthenticator's
    `fs::permissions` on its temporary files, and the vault's file name join.
- **`UltraVaultTests` runs on Windows.** The new `ULTRACANVAS_BUILD_VAULT_TESTS`
  builds it without the full suite (the target lives in
  `Tests/VaultTests.cmake`, shared with `BUILD_TESTS`), and the Windows CI row
  runs it - the one platform, on its code page 1252 runner, where the
  vault-in-a-Thai-and-emoji-folder test can fail. The test file itself goes
  through `PathToUtf8` / `PathFromUtf8` now and is scanned by the check.
