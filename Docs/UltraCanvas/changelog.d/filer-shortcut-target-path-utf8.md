- **UltraCanvasFilerWidget: a shortcut's target path is converted with
  `PathFromUtf8` before `std::filesystem` is asked about it.** The two checks
  in `ActivateEntry` / `OpenEntryWithOS` passed the UTF-8 `linkTarget` string
  straight to `fs::is_directory` / `fs::is_regular_file`, an implicit
  `fs::path(utf8)` conversion that on Windows goes through the ANSI code page
  and names a different file for a target outside it (see *File paths are
  UTF-8* in AGENTS.md). The CI check cannot see an implicit conversion, which
  is why it passed.
