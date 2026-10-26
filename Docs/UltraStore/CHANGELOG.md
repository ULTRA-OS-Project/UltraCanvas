#### 2026-10-26 *0.1.2*
- **The window.** `ultrastore` started plainly opens it: a sidebar with
  Discover, Categories, Installed, Updates and Settings, the download queue
  in a collapsible panel along the bottom, a status line. Discover searches
  the catalog and shows every app as a card with its icon, summary and state
  (installed version, update available, not for this system, price) and the
  action that fits: Install (or Try for a paid app with a trial, and Buy…
  to the publisher's page until payment lands), Open, Update, Remove,
  Details. Categories filters the same cards with a chip per category.
  Installed lists the apps, the runtimes and libraries under them, and the
  clean-up. Updates lists what has a newer release, one by one or all at
  once, with the count on the sidebar. Details shows one app whole: the
  badges (price, age rating, open source, needs an account, update period,
  deprecated or withdrawn, translated automatically), the description, the
  screenshots, the release's notes and what it needs, the links. Settings
  holds the channel, the texts' language, the servers, the keys trusted
  beside the compiled-in ones, fetch-at-start, and what the store knows
  about the machine, its folder and the catalog. Every operation runs
  through the core on one worker thread at a time; each is a row of the
  Downloads panel with a progress bar, Pause / Resume and Cancel.
- **Shown or hidden as the admin backend says** (`StoreQuery`): a listed app
  always, an unlisted one by its exact id, a hidden or withdrawn one only
  when installed; an installed app always, for its update and removal.
- **Settings in the framework's place** (`StoreSettings`,
  `UltraCanvasAppSettings("UltraStore")`): channel, language, servers,
  additional trusted keys, the window's size and the panel's fold; the
  command line and the window read the same file.
- **Compiled-in keys and servers.** Every `Apps/UltraStore/keys/*.pub` and
  the CMake variable `ULTRASTORE_SERVERS` reach the program through the
  generated `UltraStoreBuiltin.h`; both are empty until the admin backend
  publishes the first catalog, and Discover says so.
- **Pictures from the catalog** (`AssetCache`): icons and screenshots,
  relative to the catalog's URL, fetched once into `cache/assets/` and shown
  from there offline.
- **`--catalog <file>`, `--root <dir>`, `--channel beta`** open the window
  on a local catalog (unverified), a store folder of one's own, a channel.
- One copy per user (a second start brings the window forward); a GUI
  program on Windows with `ultrastore.com` beside it for the prompt; the
  freedesktop entry and the icon on Linux. `cli/main.cpp` became
  `cli/Commands.cpp` (`UltraStore::Cli::Run`), `ui/main.cpp` decides between
  the window and a command. `FormatBytes` moved to the core.

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
