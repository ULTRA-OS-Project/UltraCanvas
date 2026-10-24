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
