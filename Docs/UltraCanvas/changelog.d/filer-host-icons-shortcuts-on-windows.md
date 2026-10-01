- **The Filer's host-icon and shortcut tests run on Windows.**
  `FilerHostIconsTest` and `FilerShortcutEntryTest` join
  `Tests/FilerTests.cmake`, so the Windows CI rows build and run them with the
  name-encoding and folder-preview tests: the host-icon test asks the real
  Windows shell for its icons, and the shortcut test reads `.lnk` files on
  the system they come from, with links that name real paths. The shortcut
  test's stored paths and the folder it lists are converted with
  `PathToUtf8` instead of `.string()`, and on Windows a desktop entry whose
  `/bin/sh` this machine does not have is expected to leave `linkTarget`
  empty and show its command, as the display does there.
