- **A `\` in the bundled fonts' folder name no longer breaks the generated
  fontconfig file on Linux and Android.** When no system `fonts.conf` is
  found, `SetupBundledFontconfig` writes one naming the folder of the bundled
  fonts. `ToFontconfigPath` turned every `\` in that path into `/` on every
  system, so an application whose folder has a `\` in its name pointed
  fontconfig at a folder that does not exist and found none of its bundled
  fonts. The swap is now made only on Windows, where `\` separates folders.
- **UltraWeb 0.8.3**: a local file with a `\` in its name opens on Linux and
  macOS (`ToPageUrl`); `UltraWebTabTest` opens one.
