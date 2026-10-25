- **Starting UltraFiler after closing it said "UltraFiler is already
  running"** on Windows, and the old copy - no window, still in Task Manager -
  had to be ended by hand. UltraFiler hosts the per-user message bus when it is
  started first, and the bus's clean-up at exit could wait for ever inside
  Windows' process shutdown, holding the lock that lets only one UltraFiler
  run. The clean-up no longer runs there, and a start that finds a copy
  still closing waits for it to go and then opens.
