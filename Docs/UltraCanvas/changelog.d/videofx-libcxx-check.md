- **`scripts/check_libcxx_syntax.sh`: VideoFX type-checked against libc++
  before a push.** The Windows builds compile the module with MSYS2's
  CLANG64 toolchain and its libc++, which refuses what Linux's libstdc++
  lets through: #973's first Windows build failed on `std::to_string` of a
  file time's tick count, a 128-bit integer there, fifteen minutes into the
  job. The script runs `clang++ -stdlib=libc++ -fsyntax-only` over the
  engine's sources, the tool and the test with the module's own flags, in
  parallel, and prints only the errors; with the old key put back it
  reports exactly that ambiguity. AGENTS.md and the VideoFX README name it.
