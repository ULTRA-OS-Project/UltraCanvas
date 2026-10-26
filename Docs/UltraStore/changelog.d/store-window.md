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
