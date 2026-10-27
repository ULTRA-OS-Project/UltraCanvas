- **The window.** `ultrastoreadmin` started plainly (or with `--store <dir>`
  alone) opens the admin window: the store's pages and every app, runtime
  and library in a sidebar; the form of one with every setting the catalog
  carries - identity, visibility and lifecycle, age rating, the monetary
  model with price, currency and trial, the free update period, the texts
  per language with *Translate the missing languages* through UltraAI and
  the machine ones marked, the icon and screenshots copied into the media
  folder, the releases newest first with their packages and inline editors
  for a new release and a new package (the build into the package store,
  the entry suggested from the archive); Publish with *Check* listing the
  warnings and the report; the keys with export and retire; the settings
  and the publishers. The commands moved to `cli/Commands.cpp`
  (`Cli::Run`), the store's folders and the media import to the core
  (`AdminStore`, `ImportMedia`, `DefaultAdminStoreDir`). One copy per
  user; the app icon and a desktop file.
