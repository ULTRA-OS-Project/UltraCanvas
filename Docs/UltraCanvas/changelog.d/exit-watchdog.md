- **Windows: a program that does not end after its last window closed is
  ended, and says why.** Something in a program's shutdown that waits for
  ever - a joined thread that never finishes, a lock nobody gives back - left
  a process with no window that held its files and its single-instance lock:
  UltraFiler and UltraTexter answered the next start with "already running"
  until the old copy was ended in Task Manager. Once the main loop has ended
  the Windows backend arms an exit watchdog (`ArmWindowsExitWatchdog`, from
  the new `UltraCanvasApplicationBase::RunAfterMainLoop()`); still running ten
  seconds later, the process writes where its main thread stood - the call
  stack, named as in a crash report - to `<app>-exit-<date>-<time>-<pid>.txt`
  beside a minidump in the crash-dump folder (and to `ULTRACANVAS_DEBUG_LOG`),
  and ends with exit code 124. `ULTRACANVAS_EXIT_WATCHDOG_SECONDS` sets the
  wait, `0` turns it off. A single-instance start now keeps trying for fifteen
  seconds instead of ten, so a start made just after closing outlasts such a
  copy and opens.
