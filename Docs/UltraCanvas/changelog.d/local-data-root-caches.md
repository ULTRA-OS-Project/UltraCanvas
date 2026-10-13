- **The machine-local data root is in the header too:
  `UltraCanvasLocalDataFolderRoot()`.** Data that should not roam with a
  Windows profile - the desktop's clipboard history, the UltraMessage journal,
  the OCR language packs - each resolved `%LOCALAPPDATA%` and the other
  platforms' folders with a copy of the rule of its own, two of them through
  the narrow `getenv`. `UltraCanvasDataFolder.h` now has the one rule for it
  as well (`LocalDataFolderRootFor`, pure, and the live root): `%LOCALAPPDATA%`
  on Windows, the same Application Support / `~/.local/share` as the roaming
  root elsewhere, `$XDG_DATA_HOME` first everywhere. All three use it, in the
  folders they had. On Windows `USERPROFILE` stands in for `HOME` when the
  latter is unset, for both roots.
- **The UltraMessage journal's folder is `UltraMessage` on every platform.**
  Linux alone spelt it `ultramessage`; the first broker start moves that
  folder, journal and SQLite side files together, into `UltraMessage`.
- **The spell checker's user dictionary lives in the data folder rule.** On
  macOS it was kept in `~/.local/share/UltraCanvas`, as on Linux, while every
  other per-user file of the framework there is in Application Support; it
  moves there on first use, the words a person added with it. The Hunspell
  backend searches `UltraCanvas/dictionaries` under the same root first (so a
  dictionary dropped there is found on macOS too), reads every variable as
  UTF-8, and still looks in the earlier folders.
