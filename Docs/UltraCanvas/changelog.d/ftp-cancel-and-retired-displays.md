- **An FTP call can be called off** (UltraNet FTP 0.7.0).
  `UltraNetFtpOptions::isCancelled`, or `UltraNet_SetThreadFtpCancel` for a
  caller behind a layer that builds the options (UltraCloud), is asked while
  the call runs - as bytes move, and about once a second while none do - and
  ends it as `UltraNetResultCode::Cancelled` ("called off"). Until now a
  download ran to its last byte whoever was waiting for it. UltraCloud's FTP
  provider reports such a call as `ResultCode::Cancelled` rather than as a
  server refusal. Two loopback tests in `test_ftp_log.cpp` call off a stalled
  listing, through the options and through the thread.
- **A folder display can be retired without waiting for its workers**
  (`UltraCanvasFilerWidget::RequestBackgroundStop` / `BackgroundStopped`).
  The destructor joins every worker the widget started, so destroying a
  display whose worker was busy with a slow file held the UI thread for as
  long. A host now tells the workers to stop, keeps the widget, and destroys
  it once `BackgroundStopped()` says every worker thread has returned; every
  thread the widget starts is counted for it. `FilerRetireTest` checks it, on
  Windows too.
