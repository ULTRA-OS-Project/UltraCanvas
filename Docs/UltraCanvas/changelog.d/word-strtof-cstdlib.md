- **The Word document module builds with the current MSYS2 toolchain
  again.** `UltraCanvasWordFormatInternal.h` used `std::strtof` without
  including `<cstdlib>`, and the libc++ MSYS2's CLANG64 ships since
  2026-10-09 no longer brings it in through `<string>`: a Windows leg with a
  cold compiler cache stopped in `UltraCanvasOdtFormat.cpp` with "no member
  named 'strtof' in namespace 'std'". The header includes `<cstdlib>` now.
