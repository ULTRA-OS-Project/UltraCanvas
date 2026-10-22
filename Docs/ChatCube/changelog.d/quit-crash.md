- **ChatCube crashed as it quit (SIGABRT).** Closing the window ended in an
  abort inside `free()` after ChatCube had shut down: the framework's
  database layer wrote into freed memory while the messaging service closed
  its journal at exit. Fixed in UltraDatabase; ChatCube quits cleanly.
