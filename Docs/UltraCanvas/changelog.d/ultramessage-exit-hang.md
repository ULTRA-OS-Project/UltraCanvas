- **A Windows program hosting the UltraMessage bus could stay running with no
  window after it was closed.** In the shared core the module's at-exit
  clean-up is a handler of `libUltraCanvas.dll`, which Windows runs at
  `DLL_PROCESS_DETACH`, after it has killed every other thread wherever it
  was. Stopping the broker there locked mutexes those threads may have held,
  joined them, and let the toast presenter leave a COM apartment under the
  loader lock - for the toasts, from a thread that had never
  entered it. A process that hung there kept its single-instance lock, so
  the next start said "*<App>* is already running" until the old one was
  ended in Task Manager. The handler now does nothing while Windows ends
  the process (`RtlDllShutdownInProgress`); the system closes the pipe and the
  journal with it. Elsewhere, and in a static core, it shuts down as before.
  Reproduced under Wine: a DLL's at-exit handler that locks a mutex a killed
  thread held keeps the process alive; with the check it ends at once.
- **A start waiting on a copy that is closing takes over once it has gone.**
  `UltraCanvasSingleInstance::Claim` tries the lock again between its
  hand-off attempts, so a copy whose window has closed but whose process is
  still ending no longer makes the next start give up with "already running".
- The toast presenter leaves its COM apartment only on the thread that
  entered it.
