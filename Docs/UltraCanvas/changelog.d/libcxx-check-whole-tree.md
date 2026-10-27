- **`scripts/check_libcxx_syntax.py` 2.0.0: the whole tree against
  libc++, from the build's compile database.** The shell script of 1.0.0
  checked VideoFX with a flag block of its own; the Python one reads
  `build/compile_commands.json` (adding `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`
  to an existing build directory itself) and compiles every C++ unit the
  Windows build would see - the Linux, macOS and Android OS arms, C sources
  and generated files left out - with `clang++ -stdlib=libc++ -fsyntax-only`
  and the flags the build gave it. A path argument narrows it to a module
  or a file: VideoFX in ten seconds, the 1539 units of the tree in twenty
  minutes on four cores, all of which compile today.
