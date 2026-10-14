- **A second start's files go to the running copy:
  `UltraCanvasSingleInstance::MakeOpenRequest` / `ListenOpen`.** The document
  applications - Texter, UltraFiler, UltraViewer, UltraPaint, ArtCreator,
  UltraVideo and AnchorPoint - now run one copy per user too: opening a file
  from the file manager while one of them runs opens it in that copy, the way
  the application opens a file itself, instead of starting another. The paths
  are made absolute in the starting process, since the running copy was
  started from another folder; a URL passes through unchanged. See
  `Docs/UltraCanvas/UltraCanvasSingleInstance.md`.
- **The running copy answers at once, even while it is busy.** It answered a
  second start from its UI thread, so a window held by a native dialog - an
  application showing "cannot open this file" for its start-up file - left
  the second start waiting ten seconds and then saying *"already running"*.
  The answer now goes out from the message bus's own thread and only the
  request waits for the UI thread; the applications also listen before they
  open their start-up files, so a start made during that time is answered.
