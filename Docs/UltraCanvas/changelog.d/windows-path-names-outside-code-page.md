- **UltraFiler no longer quits on Windows when a folder holds a name outside
  the system code page.** On a Thai Windows 10 machine, opening a folder
  stopped UltraFiler with *Unhandled exception: filesystem error: in
  __wide_to_char: Illegal byte sequence*. With libc++ (the MSYS2 CLANG64 /
  CLANGARM64 toolchain), `std::filesystem::path::string()` converts the
  UTF-16 name to the process's ANSI code page. It throws if a single
  character has no equivalent there, such as an emoji, a CJK name or an
  accented Latin letter on a Thai system. The UTF-8 `activeCodePage` in the
  application manifest would avoid this, but Windows ignores it before 10
  version 1903, so it cannot be relied on. The Filer path now converts
  through `PathToUtf8` / `PathFromUtf8` (`UltraCanvasPathUtf8.h`), which go
  through UTF-16 and never depend on the code page. The code that changed:
  `UltraCanvasFilerWidget` (folder listing, copy / move / delete / rename,
  archives), `UltraCanvasBreadcrumb`, `UltraCanvasVolumeMonitor`,
  `UltraCanvasCloudStorage`, `UltraCanvasHostFileIcons`, the file loader's
  extension and base-folder lookups, and the well-known-folder list in
  `UltraCanvasUtils`. On a system where the manifest's UTF-8 code page is in
  effect, the result is byte-for-byte what it was before.
