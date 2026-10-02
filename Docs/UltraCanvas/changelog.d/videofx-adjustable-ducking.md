- **VideoFX: the ducking thresholds are adjustable** (VideoFX 0.4.1).
  Background music ducked with constants tuned for speech - sound above
  -36.5 dBFS counted as present, 0.12 s down, 0.6 s of quiet before coming
  back, 0.8 s up - so a clip that is loud all the way through (a concert,
  traffic, a waterfall) held the music at `duckingLevel` for its whole length.
  `VideoFXMusic` gains `duckingThresholdDb`, `duckingAttack`, `duckingHold`
  and `duckingRelease`, with those values as defaults (so existing exports
  sound the same), checked by `VideoFX_Export` like the other music settings.
  - `videofx`: `--duck-threshold DB`, `--duck-attack S`, `--duck-hold S`,
    `--duck-release S`.
  - `VideoFXTest`: a steady loud background ducks with the speech defaults
    and not with the threshold raised above it, a louder moment still does,
    a short hold and release bring the music back where the speech hold
    would not, and out-of-range values are refused.
- **CI: EmailCleaner's engine tests run on Windows.** `build.yml` built
  `EmailCleanerEngineTests` on every row but only Linux ran it; the Windows
  rows now run it too (`ctest -R "^EmailCleanerEngine$"`). The one assertion
  that assumed `/` separators (`Attachment_CachePathFollowsUltraMailsLayout`)
  now builds its expected path the way the code does.
- **CI: UltraMail's engine tests run on Windows**, in the same step as
  EmailCleaner's (`ctest -R "^(EmailCleanerEngine|UltraMailEngine|...)$"`):
  they were built on the Windows rows and never run. The sync-service test
  wrote its cache to `"/tmp/ultramail_syncsvc_test"`; it now uses the system
  temp folder.
- **The plug-in import check covers Windows DLLs.**
  `scripts/check_ultranet_plugin_imports.py` read only `.so` / `.dylib`
  undefined symbols, so nothing checked that a Windows plug-in avoids the
  core. It now reads each `.dll`'s import table (`llvm-objdump -p`, or GNU
  `objdump -p`): a plug-in must import nothing from `libUltraCanvas*.dll`,
  `UltraNet*.dll` or another plug-in, and no core function from any DLL. A
  DLL it cannot read is a failure, not a pass. `UltraNetPluginHostImports`
  is registered on Windows too (MSYS2's Python is added to the Windows rows)
  and run there; it passes `--require` when the build makes plug-ins, so an
  empty output folder fails. New `UltraNetPluginHostImportsSelfTest` checks
  the parser against both objdump layouts; the check was tried on DLLs
  built with clang/lld importing from `libUltraCanvas.dll` (caught), a core
  C++ function from a renamed DLL (caught) and only `KERNEL32.dll` (clean).
- **The path check catches a string joined onto a path with `/`.**
  `scripts/check_path_string.py` (`path-implicit`) looked at strings handed
  to `fs::` calls, stream constructors, `open()` and `fs::path p = str;`, but
  not at `PathFromUtf8(dir) / accountId` - the form of the two real Windows
  bugs this branch fixed in UltraMail and EmailCleaner (a mail folder with a
  non-English name cached under a mangled path). A `/` chain that holds a
  path (`PathFromUtf8(...)`, `fs::path(...)`, a path variable or an
  `auto` set from one, a path accessor such as `.parent_path()`) now has
  each operand checked: a `std::string` variable, `.c_str()` or a call to a
  function the file declares as returning `std::string`
  (`/ SanitizeFolder(folder)`) or a parenthesised sum with such a term
  (`/ (baseName + " (2)")`) is reported; `p /= str;`, `pathVar = str;` and a
  `cond ? s1 : s2` with a string branch likewise. The check also reads
  range-for variables (`for (const std::string& d : dirs)`), C strings and
  other `auto`s, so the nearest declaration of a name decides its type
  (a `for (const char* name : {...})` is no longer taken for an older
  `std::string name`).
  - 49 sites across the tree were wrapped in `PathFromUtf8`: joins in the
    Filer widget (copy / extract / rename-on-collision), the Git repository
    reader, rich-document image export, shell-link resolution, desktop
    entries, file-error suggestions, the GutenPrint PPD cache, UltraWin,
    the OCR language files, UltraCloud's secrets, UltraMail's sender-icon
    cache, UltraCleaner's trash and the macOS bundle reader; `std::string`
    range-for variables handed to `fs::` calls in the Hunspell backend, the
    CDR converter, the Filer widget and UltraFiler; and the Filer's extract
    destination, built with `fs::path dir(cond ? a : b)` and reassigned
    `dir = currentPath`. Against the pre-fix UltraMail / EmailCleaner
    sources the check reports all five lines that were wrong.
