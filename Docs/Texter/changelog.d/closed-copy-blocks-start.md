- **Starting UltraTexter after closing it could say "UltraTexter is already
  running"** on Windows, with the old copy left in Task Manager without a
  window. The framework fixes behind it: the message bus's clean-up no longer
  waits inside Windows' process shutdown, and an exit watchdog ends a copy
  still running ten seconds after its window closed, leaving
  `UltraTexter-exit-*.txt` in `%LOCALAPPDATA%\UltraCanvas\CrashDumps` that
  names where it was stuck. A start made meanwhile waits for it and opens.
