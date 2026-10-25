- **Windows: a program refuses to start against a framework DLL from another
  build, and says which.** A folder that collects programs from several
  packages keeps one `libUltraCanvas.dll`, from whichever was copied last;
  a program built for another version loaded it without a word and crashed at
  its first call whose classes had changed - ChatCube and UltraAI did, with
  only "ChatCube.exe+0x1463D" in the report. Every Windows program linking the
  shared core now compares, before `main()` and before its own static
  objects, the version it was built for with the one the DLL reports
  (`UltraCanvas_LibraryVersion()`, new, with C linkage), and on a difference
  shows the DLL's path and both versions and exits with `0xC0000139`. The
  check is an `INTERFACE` source of the core for executables only; the version
  sits in a three-line wrapper the build writes, so a changelog edit
  recompiles that file in each program and nothing else.
