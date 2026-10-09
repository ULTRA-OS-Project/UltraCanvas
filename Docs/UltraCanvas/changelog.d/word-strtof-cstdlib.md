- **The tree builds with the current MSYS2 toolchain again.** The libc++
  MSYS2's CLANG64 ships since 2026-10-09 no longer brings `<cstdlib>` in
  through `<string>`, and a Windows leg with a cold compiler cache stopped
  on the first file that relied on it: `UltraCanvasWordFormatInternal.h`
  ("no member named 'strtof' in namespace 'std'"), then the vendored
  MicroTeX's `string_utils.h` (`strtod`, `strtol`). Both include
  `<cstdlib>` now.
