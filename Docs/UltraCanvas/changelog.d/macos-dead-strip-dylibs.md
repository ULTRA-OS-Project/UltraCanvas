- **macOS apps load and ship only the libraries they use.** The core is
  linked statically on macOS, so every application's link line carried its
  whole dependency list (tesseract, leptonica, MuPDF, zbar, libcdr with boost
  and ICU, the audio codecs), and Apple's linker recorded each one whether the
  program used it or not. `package-macos.sh` then copied all of them, with
  their own dependencies, into every one of the six `.app` bundles. Apps now
  link with `-dead_strip_dylibs`, which drops only the libraries an app takes
  no symbol from, so an app such as UltraNetMonitor or DeviceExplorer that
  never reaches the OCR or PDF code no longer carries them. The packaging
  script prints, and adds to the CI job summary, each bundle's size and how
  much of it is bundled libraries.
