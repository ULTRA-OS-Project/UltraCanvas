- **The RAM disc button opens UltraFiler's RAM disc.** A click has UltraFiler
  show the RAM disc, or open the *New RAM disc* dialog when there is none
  (`UltraFiler --ram-disc`), instead of opening `/dev/shm` itself, where
  UltraFiler's discs are only folders. `ramDiscPath` in `desktop.json` is now
  `"ultrafiler"` by default, on Windows and macOS too, so the button is there
  on every platform; a settings file holding the earlier default `/dev/shm`
  is read as `"ultrafiler"`. Another folder set there still opens that
  folder, and an empty value still leaves the button out. With a file
  manager other than UltraFiler the button opens it without the option.
