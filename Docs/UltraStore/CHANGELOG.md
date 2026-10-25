#### 2026-10-25 *0.1.1*
- **UltraInstaller: install, update, remove.** The store now does what it
  planned. `Downloader` streams a package through UltraNet into
  `downloads/<sha256>.<format>`, resumes a partial file with a Range request,
  checks the catalog's SHA-256 and deletes what does not match; `SqliteRegistry`
  remembers every installed version on UltraDatabase in `registry.sqlite`
  (one row per app or component version, the app's current one marked, the
  exact dependencies it was installed with); `ArchiveInstaller` unpacks zip,
  7z and tar.* through VirtualFS - and copies an AppImage - into
  `apps/<id>/<version>` or `components/<id>/<version>`, staged beside the
  destination and renamed into place so a failure leaves nothing half
  installed, makes the entry executable and points `apps/<id>/current` at
  it; its garbage collector removes the app versions and components no
  current app needs. `StoreEngine` ties them together for the command line
  and the window alike. New commands: `ultrastore install <catalog> <app>`,
  `update <catalog> [<app>]`, `remove <app>`, `installed`, `gc`, each with
  `--root <dir>`. Tests run the whole path against a local HTTP server; the
  Linux CI legs install libarchive so they run rather than skip.

#### 2026-10-24 *0.1.0*
- **The store's core and command line.** `Apps/UltraStore` begins with
  `UltraStoreCore`, a static library (`ultrastore_core`) that knows the
  machine (`PlatformInfo`: os, processor, OS version, glibc, distribution,
  Rosetta), reads and writes the signed catalog (`Catalog`: apps and the
  runtime / library components they run on, releases with a package per
  target, exact-version dependencies, the admin backend's settings - minimum
  OS version per package, pricing, update period, visibility, lifecycle and
  flags), checks the catalog's Ed25519 signature and the packages' SHA-256
  (`Verifier`, UltraCrypt with libsodium), picks the package for the machine
  and lays out the install plan with the shared components first and the
  installed ones skipped (`PackageResolver`), and fetches the catalog from the
  first server that answers, verified, refused when older than the one seen,
  cached for offline use (`CatalogClient`, UltraNet). The `ultrastore`
  program drives it from the terminal: `platform`, `list`, `resolve`,
  `verify`, `fetch`, `keygen`, `pubkey`, `sign`, `sha256`. The target
  requires libsodium and SQLite and stops the configure without them
  (`-DBUILD_ULTRASTORE=OFF` leaves it out). Tests under `Tests/UltraStore`
  (`ULTRACANVAS_BUILD_ULTRASTORE_TESTS`), run by CI. The design is
  `Docs/UltraStore/README.md`.
