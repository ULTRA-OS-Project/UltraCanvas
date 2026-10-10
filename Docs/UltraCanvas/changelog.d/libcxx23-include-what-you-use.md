- **The tree includes the standard headers it uses, and the LLVM 23 bridge
  is gone.** The build defined `_LIBCPP_KEEP_TRANSITIVE_INCLUDES_LLVM23` so
  that files relying on libc++ bringing in a header through `<string>` kept
  compiling on MSYS2's LLVM 23 toolchain; libc++ 24 removes that bridge. Every
  C++ file was compiled against the libc++ 23.1.3 headers (clang 22 frontend)
  with the bridge off - the 1318 the Linux build compiles, the Windows-only
  sources against the MinGW headers, the tests and plug-ins this
  configuration skips - once with and once without it. Five relied on it and
  include what they use now: `EmailCleanerTypes.cpp`, `UltraFIBUCli.cpp` and
  `UltraWinSetup/main.cpp` (`<cstdlib>` for `std::atoi`/`atol`/`atoll`),
  AnchorPoint's `RawSocketTransport.cpp` (`<cerrno>`) and
  `UltraCanvasTimeline.h` (`<functional>`); the Windows notification
  listener includes `<cstdio>` for its `std::snprintf`. The definition is
  removed from the build and from the exported library target, so the
  Windows legs reject a file that relies on a transitive include as soon as
  it is written. Not checked here: the macOS, Android and WebAssembly
  platform sources and the opt-in llama.cpp adapter, whose toolchains are
  not libc++ 23.
