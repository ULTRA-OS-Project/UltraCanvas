- **The Linux release binaries carried full debug info — about 800 MB of the
  portable bundle.** The top-level `CMakeLists.txt` added `-gdwarf-4` to work
  around binutils ld < 2.40 misreading Clang's DWARF5, on the belief that it
  was "a no-op without -g". On Clang every `-gdwarf-N` also *turns on*
  debug info, so every Release build on the Linux leg was a debug build in
  size: the packaged executables and `libUltraCanvas.so` came to 907 MB,
  100 MB once stripped. It is now `-fdebug-default-version=4`, which only
  picks the format for a build that asks for `-g`. The version test beside
  it read `${CMAKE_MATCH_1}` in the same `if()` as the `MATCHES` that sets
  it, so it took the branch on any ld (it reported "GNU ld 2.42 < 2.40");
  it is nested now.
- `package-linux.sh` strips the executables and the UltraCanvas libraries it
  copies from the build tree (`--strip-unneeded`, so `.dynsym` stays and
  plug-ins still bind to the executables' exported core symbols). A Debug or
  RelWithDebInfo tree packaged by hand no longer ships its DWARF either.
