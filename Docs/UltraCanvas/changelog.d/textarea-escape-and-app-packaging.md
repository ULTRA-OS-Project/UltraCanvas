- **Escape cancels a dialog from a multi-line field too.**
  `UltraCanvasTextArea` took the Escape key and did nothing with it, so a
  custom dialog (`DialogType::Custom`, the kind UltraMail's and
  UltraPassword's forms are) stayed open on Escape while the caret was in a
  text area, and closed on it from every other field. The text area now
  declines the key, so it reaches the dialog's own Escape-to-Cancel, and it
  returns before the text-insertion path, so the key's `"\x1b"` is never
  typed. Texter's editor gains the same: Escape there now reaches the
  window, which closes the search bar. `Tests/DialogEscapeTest.cpp` routes
  the key the way the application does and fails on the old behaviour.
- **UltraAuthenticator and UltraPassword are packaged.** `package-linux.sh`
  lists both. `package-macos.sh` builds an `.app` bundle for each, and its
  `build_app_bundle` now finds an executable in `build/bin/` as well as the
  build root - both apps set `RUNTIME_OUTPUT_DIRECTORY` to `bin/`, which the
  Linux and Windows scripts already searched. A missing one is reported and
  skipped instead of ending the run. `package-win.sh` needed no change: it
  copies every `.exe` in `build/` and `build/bin/`.
