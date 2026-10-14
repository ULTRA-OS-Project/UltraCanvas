- **One rule for the per-user application data folder:
  `UltraCanvasDataFolder.h`.** Every application worked out for itself where
  its data goes, and they disagreed: UltraMail, UltraPassword and
  UltraAuthenticator treated macOS like Linux and kept the mailbox and the
  vaults in `~/.local/share`, UltraAuthenticator preferred `HOME` to
  `%APPDATA%` on Windows, EmailCleaner knew neither and looked for UltraMail's
  mail in the working directory there, while UltraSocial and the desktop's
  clipboard history used `~/Library/Application Support`. The new header-only
  helper (usable from the headless engines, like `UltraCanvasPathUtf8.h`)
  names the one rule - `%APPDATA%` on Windows, Application Support on macOS,
  `$XDG_DATA_HOME` or `~/.local/share` elsewhere, and `$XDG_DATA_HOME` wins
  everywhere - as a pure, testable `DataFolderRootFor(platform, ...)`,
  `UltraCanvasDataFolder(appName)` for the live environment,
  `LegacyUnixDataFolders` for where the rule moved away from, and
  `AdoptLegacyDataFolder`, which moves such a folder into place once, whole,
  never over data already there. UltraMail, UltraPassword, UltraAuthenticator,
  UltraSocial and EmailCleaner all resolve their folder through it now; the
  clipboard history keeps `%LOCALAPPDATA%` on purpose (a machine-local store,
  not roaming data). `Tests/DataFolderTest.cpp` covers the rule and the move.
