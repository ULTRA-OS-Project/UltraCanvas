- **UltraPassword joins the applications that keep native file dialogs off.**
  `KnownFileDialogApplications()` lists it beside UltraAuthenticator and
  UltraMail, so the file-dialog settings page offers it. The new app itself
  versions from `Docs/UltraPassword/CHANGELOG.md` (declared in
  `cmake/UltraCanvasVersion.cmake` as `ULTRAPASSWORD_VERSION`), and
  `Tests/UltraPasswordTests.cpp` joins the headless suites under
  `BUILD_TESTS`.
