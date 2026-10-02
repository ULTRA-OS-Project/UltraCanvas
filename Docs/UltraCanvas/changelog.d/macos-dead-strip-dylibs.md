- **macOS apps load and ship only the libraries they use.** The core is
  linked statically on macOS, so every application's link line carried its
  whole dependency list (tesseract, leptonica, MuPDF, zbar, libcdr with boost
  and ICU, the audio codecs), and Apple's linker recorded each one whether the
  program used it or not. `package-macos.sh` then copied all of them, with
  their own dependencies, into every one of the six `.app` bundles. Apps now
  link with `-dead_strip_dylibs`, which drops only the libraries an app takes
  no symbol from. So far that is little: DeviceExplorer loses zbar and c-ares,
  while tesseract, MuPDF and the rest stay, because core code every app pulls
  in still references them. The packaging
  script prints, and adds to the CI job summary, each bundle's size and how
  much of it is bundled libraries.
- **UltraNetMonitor and DeviceExplorer on macOS no longer carry the LaTeX
  module.** `package-macos.sh` copied `libUltraCanvasLaTeX.dylib`, the
  libraries it loads and the MicroTeX fonts (`media/microtex`) into every
  `.app`. Neither app typesets LaTeX: the only Markdown either shows is a
  dialog message, which without the module displays `$...$` as plain text.
  Their bundles now leave all three out (`NO_LATEX_APPS`); every other app
  keeps them.
- **The macOS CI download is one disk image.** The macOS artifact was the
  `dist-macos/` folder zipped by `upload-artifact`, which does not keep file
  permissions, so an app unpacked from it had lost its executable bit. CI now
  packages with `--dmg` and uploads only `UCDemo-MacOS-<version>-<arch>.dmg`.
  The image is LZMA-compressed (`ULMO`, the tightest format `hdiutil` has), holds
  the six apps, `ultramsg` and an Applications link for drag-and-drop install,
  is signed like the apps, and on `main` is notarized and stapled as well.
  `hdiutil create` gets three tries against the runners' occasional "Resource
  busy".
- **`package_and_notarize-macos.sh` no longer carries notarization
  credentials.** It exported the Apple ID, team ID and an app-specific
  password written into the script, so every reader of the repository had
  them. It now takes `APPLE_ID`, `APPLE_TEAM_ID` and `APPLE_APP_PASSWORD` from
  the environment and stops with a message when one is missing. The old
  password is still in the git history and has to be revoked at Apple.
