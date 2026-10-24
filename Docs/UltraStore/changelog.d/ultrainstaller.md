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
