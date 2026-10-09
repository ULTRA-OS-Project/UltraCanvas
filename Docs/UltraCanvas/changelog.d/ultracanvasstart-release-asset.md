- **UltraCanvasStart is a release asset of its own, for each platform.** The
  setup application sets a computer up for UltraCanvas development, so it
  has to reach a computer that has neither the toolchain nor a clone - and
  until now it was only in the suite packages, one of twenty applications in
  a download of several hundred megabytes, and not in the Linux and macOS
  suites at all. Every CI leg now cuts it out of the suite package it just
  made with exactly the libraries it loads: `scripts/package-ultracanvasstart.sh`
  on Linux (the closure `ldd` resolves inside the bundle's `lib/`) and Windows
  (the DLLs its import table reaches, with `cacert.pem` for the SDK download),
  `package-macos.sh --start-app` on macOS (a bundle with its own `Frameworks/`,
  signed and notarized in a submission of its own). Each script runs the
  packaged application before it is done. The release build of `main`
  attaches the six `UltraCanvasStart-<OS>-<version>-<arch>` archives to the
  release beside the six SDKs; `Docs/GettingStarted.md` opens with the
  download. The Linux and macOS suites carry UltraCanvasStart too now.
