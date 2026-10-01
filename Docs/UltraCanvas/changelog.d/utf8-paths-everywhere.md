- **Every file name is UTF-8 on Windows - the implicit conversions too.**
  `PathToUtf8` / `PathFromUtf8` had replaced `.string()` and `fs::path(str)`,
  but a UTF-8 `std::string` handed *straight* to something that takes a path
  converts the same way, through the ANSI code page: `fs::exists(str)`,
  `fs::remove(str, ec)`, `fs::directory_iterator(str)`, `std::ifstream f(str)`,
  `f.open(str)`, `fs::path p = str;` - and `fopen(name, mode)` reads the name
  in it too. On a Windows whose code page lacks a character of the name, each
  of these named a different file: an image, document, model, font, PDF, mail
  attachment, cloud cache or setting under a Thai, CJK or emoji folder name
  was "not found". 774 such sites in 243 files - framework, plugins,
  VirtualFS, UltraCloud, UltraNet, SmartHome, UltraAI, every application and
  the tests - now go through `PathFromUtf8` / `OpenFileUtf8`.
  - `PathFromUtf8` also takes a `const char*`, a `std::string_view` and a
    `std::filesystem::path` (returned unchanged), so wrapping a name in it is
    correct whichever of these it is; a C string or view used to pick the
    code-page `path` constructor instead.
  - `scripts/check_path_string.py` reports the implicit forms as
    `path-implicit` and `fopen-narrow` (537 findings before this change, none
    after), and now also scans `UltraNet/` and `VideoFX/`. Linux, macOS,
    Android, WASM and ULTRA OS platform code is exempt from the two new kinds:
    a path's native string is the UTF-8 bytes there.
  - `Tests/PathUtf8Test.cpp`, which Windows CI runs under code page 1252,
    exercises each wrapped call - create, query, size, time, read, write,
    open, copy, rename, iterate, remove - on a Thai-and-emoji folder and file.
