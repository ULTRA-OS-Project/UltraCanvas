- **A program could crash on exit after using file associations.** The
  association service's worker thread resolves applications and icons in the
  background; the service's destructor joins it, but only once the current
  lookup ends. The statics that lookup used — the desktop icon cache on Linux
  (`FindDesktopIconFile`), the icon cache directory on Windows and macOS, the
  sweep's extension list — were function-local statics first built on that
  thread, so they were destroyed *before* the service and freed under the
  running lookup. `FilerNameEncodingTest` printed `ALL PASSED` and then
  crashed in CI. They are now allocated once and never destroyed. The Linux
  backend's MIME/application index is a namespace-scope global, built before
  the service, and so already outlived it.
