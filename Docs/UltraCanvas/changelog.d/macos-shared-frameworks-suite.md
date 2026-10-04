- **macOS: the apps share one copy of their libraries.** `package-macos.sh`
  gave every `.app` its own `Contents/Frameworks/` with the ~90 Homebrew
  dylibs (95-131 MB each). With eight apps that was ~830 MB of the same
  libraries, and adding UltraAuthenticator and UltraPassword took the macOS
  DMG from 431 MB to 556 MB (arm64). The apps are now packaged as one suite
  folder, `UltraCanvas/`, with a single shared `Frameworks/` that every app
  loads from (`@executable_path/../../../Frameworks/`); `ultramsg` sits in
  the same folder. The suite is notarized in one submission and each app's
  ticket stapled, instead of one round trip to Apple per app.
  - Install by copying the whole `UltraCanvas` folder to Applications: an app
    moved out of it on its own does not start.
  - `verify_suite` fails the packaging run when an app carries its own
    `Contents/Frameworks/`, when a binary needs a dylib missing from the
    shared folder, or when one still loads from Homebrew - so a new app can
    no longer bring its own copy of the libraries. The rule is written down
    in `AGENTS.md` ("Packaging a new app for macOS").
  - `package_and_notarize-macos.sh` zips the suite folder instead of the
    separate `.app` folders.
