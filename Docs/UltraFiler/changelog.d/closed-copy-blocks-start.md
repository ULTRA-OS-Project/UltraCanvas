- **Starting UltraFiler after closing it said "UltraFiler is already
  running"** on Windows, and the old copy - no window, still in Task Manager -
  had to be ended by hand. UltraFiler hosts the per-user message bus when it is
  started first, and the bus's clean-up at exit could wait for ever inside
  Windows' process shutdown, holding the lock that lets only one UltraFiler
  run; that clean-up no longer runs there. Whatever else keeps a closed
  UltraFiler from ending, the framework's exit watchdog now ends it ten
  seconds after its window closed and leaves `UltraFiler-exit-*.txt` in
  `%LOCALAPPDATA%\UltraCanvas\CrashDumps` naming the step it was stuck in, and
  a start made meanwhile waits for it and opens.
