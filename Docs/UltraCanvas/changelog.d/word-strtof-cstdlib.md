- **The tree builds with the current MSYS2 toolchain again.** MSYS2's
  CLANG64 and CLANGARM64 moved to LLVM 23 on 2026-10-09, and its libc++
  dropped most of its transitive includes: `<string>` no longer brings in
  `<algorithm>`, `<cstdlib>`, `<iterator>` or `<optional>`. A Windows leg with
  a cold compiler cache stopped on the first file that relied on that
  (`UltraCanvasWordFormatInternal.h`, "no member named 'strtof' in namespace
  'std'"), then on the vendored MicroTeX's `string_utils.h`. Those two and
  `UltraFIBUCli.cpp` (`std::sort`) include what they use now, and the build
  defines libc++'s own bridge `_LIBCPP_KEEP_TRANSITIVE_INCLUDES_LLVM23`,
  exported to SDK consumers through the CMake package, until every file does;
  libc++ 24 removes the bridge.
