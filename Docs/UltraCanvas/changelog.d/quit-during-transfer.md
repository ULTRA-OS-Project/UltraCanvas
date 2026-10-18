- **A running transfer can be called off from outside it.**
  `UltraNet_SetThreadAbortCheck(fn)` (`UltraNetCore.h`) sets a check for the
  calling thread; its synchronous HTTP requests and its FTP uploads and
  downloads ask it from libcurl's progress callback - as the bytes go by,
  and about once a second while the server is silent - and end as
  `UltraNetResultCode::Cancelled` once it says true.
  `UltraNet_ThreadAbortRequested()` asks it directly. UltraFiler's drive
  worker uses it so that quitting stops an upload where it is instead of
  keeping the process alive, windowless, until the last byte has gone.
  `UltraNetTests` (`test_abort.cpp`): an HTTP request and an FTP upload to a
  port that never answers end as Cancelled within a second of the check
  saying so, and a check that never fires changes nothing.
- **`UltraCanvasApplicationBase::ClosingEndsApplication(window)`** says
  whether closing `window` leaves the application without a window - every
  other one already closing, or a child of it (its config `parentWindow`, at
  any depth) that `CloseChildWindows` takes down with it - so that `Run()`
  returns. An application with something to lose by quitting asks in that
  window's `onWindowClosing` when it is true: UltraFiler and UltraMail ask
  before a quit would stop an upload. UltraMail's own copy of the test is
  gone.
