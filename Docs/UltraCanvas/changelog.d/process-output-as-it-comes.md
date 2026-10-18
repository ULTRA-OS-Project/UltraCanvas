- **`RunProcessCaptured` can hand over the output as it comes.** A fourth,
  optional argument, `onOutput(data, size, fromStandardError)`, receives each
  chunk as it is read from the child's pipes, while the output is still
  collected into the result as before; existing callers are unchanged. The
  calls come from the calling thread (POSIX) or the reader threads
  (Windows), serialised so that two never run at once. UltraCanvasStart uses
  it to show the MSYS2 set-up, the package install and the build while they
  run. `ProcessRunnerTest` checks that the first chunk arrives before the
  child ends and that the chunks add up to what is collected.
