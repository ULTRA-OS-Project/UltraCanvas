- **A program done before the ImageMagick warm-up no longer aborts on its
  way out.** The application starts ImageMagick's warm-up on a thread of its
  own at launch, and only the end of the main loop waited for it; a program
  that never ran the loop - a test, a tool - detached it and exited under it.
  glibc's exit runs each library's finaliser and marks the library
  uninitialised, so the warm-up's dlopen of ImageMagick's coders, landing
  after libgomp's finaliser, ran libgomp's constructor a second time:
  "goacc_register: Assertion `!dispatchers[disp->type]' failed".
  ClipboardTargetsTest aborted so on the Linux x86_64 row once in many runs,
  after all its checks had passed. `UCImageRaster::FinishImageMagickWarmUp`
  waits for the warm-up; the application's destructor calls it, and on POSIX
  so does an exit handler registered with the warm-up (exit handlers run
  before the libraries' finalisers), for a program that calls `exit()`.
  - `ExitDuringWarmUpTest` starts the application and returns at once, run
    with `ExitHoldAudit` as `LD_AUDIT`, which holds the process right after
    libgomp's finaliser: without the fix it aborts in 7 of 10 runs, with it
    none. It runs with the framework's other window tests on a display.
