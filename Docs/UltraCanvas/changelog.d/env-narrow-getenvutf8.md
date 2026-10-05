- **Environment variables are read as UTF-8 on Windows, and the path check
  reports a narrow read.** Windows keeps the profile folders and the user's
  name in the environment (`APPDATA`, `LOCALAPPDATA`, `USERPROFILE`, `TEMP`,
  `USERNAME` ...), and the narrow `getenv` / `_dupenv_s` answer in the ANSI
  code page. A profile named in Thai or Cyrillic under code page 1252 came
  back with `?` in it, so a settings folder, a cache or a dictionary was
  looked for in a folder that does not exist. Wrapping those bytes in
  `PathFromUtf8` does not make them UTF-8, which is why `check_path_string`
  never saw it.
  - **`GetEnvUtf8(name)`** in `UltraCanvasPathUtf8.h` (1.2.0): the value as
    UTF-8, from `GetEnvironmentVariableW` on Windows and `getenv` elsewhere,
    empty when unset. It reads the live environment block, not the C
    runtime's copy, so it also sees a variable set later with
    `SetEnvironmentVariableW`. Still header-only: it declares
    `GetEnvironmentVariableW` itself rather than including `<windows.h>`.
    `Tests/PathUtf8Test.cpp` checks a Thai-and-emoji value, one longer than
    the first 260-character buffer, an empty and an unset one; Windows CI
    runs it under code page 1252.
  - **`env-narrow`** in `scripts/check_path_string.py` reports a narrow read
    of one of those names, any narrow read in Windows-only code, and a call
    with one of those names to a helper of the same file that reads narrowly
    (`EnvOrEmpty("LOCALAPPDATA")`). `--self-test` covers it.
  - **The 46 sites it found are fixed**, and the baseline stays empty: the
    framework's spell checker (the user dictionary and the Hunspell
    dictionary folders), the generated fontconfig folder, the cloud-storage
    folder detection, the file dialogs' remembered folder and home folder,
    Windows shortcut targets (`%VAR%` expansion), the desktop shell's home
    and `PATH` search, the UltraMessage journal, the "Open with" icon cache
    and Applications folder, and the DemoApp image benchmark's temporary
    folder; and the apps listed in their own changelogs (Texter, UltraMail,
    UltraPassword, UltraAuthenticator, UltraDesktop, UltraNetMonitor,
    UltraCanvasStart, UltraCleaner, UltraFiler and the UltraAI dashboard). Each consumer was checked first: every one opens the folder
    through `PathFromUtf8` or `OpenFileUtf8` (or hands it to SQLite, which
    takes UTF-8), so the code-page bytes never worked there for a name
    beyond ASCII. The one consumer further down that reads names narrowly,
    Hunspell, is handled next.
  - **A dictionary Hunspell cannot open is no longer listed.** Hunspell
    opens its files with the narrow CRT, so on Windows a path beyond ASCII
    only reaches it where the process code page is UTF-8 (the manifest's
    `activeCodePage`, honoured from Windows 10 1903). With `%APPDATA%` now
    read correctly, a dictionary there would otherwise have been found ahead
    of the bundled copy of the same language and then failed to open; it is
    passed over instead, and the next one in the search order is used.
  - The crash-report switches (`ULTRACANVAS_NO_ERROR_DIALOG`,
    `ULTRACANVAS_NO_CRASH_DUMP`) stay a narrow read: they hold an ASCII
    `0`/`1`, and are read on the way to a crash report, where nothing should
    allocate. The site says so.
- **`check_path_string` reads one line at a time when it strips literals.**
  Stripping the whole file at once let an apostrophe in a comment ("don't")
  pair with one many lines further down, and every declaration in between
  disappeared - so the header-aware rules missed strings declared there. Ten
  sites came to light and are fixed: five `fs::is_directory(currentPath)`
  calls and a template path in the Filer widget, the audio recorder's stream
  file, the rich document's image folder, and UltraFiler's rename of a
  remote preview.
- **VirtualFS's Windows RAM disc reads volume labels wide.** Every drive's
  label is read to find the discs this module made, and
  `GetVolumeInformationA` handed a stick labelled `ultravfs-Ελένη` back as
  `ultravfs-?????` - which then counted as one of ours. It uses
  `GetVolumeInformationW` now, and a label only counts when what follows the
  prefix is a name the module accepts. The `imdisk` and `icacls` command
  lines already went through `_wpopen` as UTF-16.
