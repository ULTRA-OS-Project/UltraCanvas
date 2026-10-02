- **VideoFX: the ducking thresholds are adjustable** (VideoFX 0.4.1).
  Background music ducked with constants tuned for speech - sound above
  -36.5 dBFS counted as present, 0.12 s down, 0.6 s of quiet before coming
  back, 0.8 s up - so a clip that is loud all the way through (a concert,
  traffic, a waterfall) held the music at `duckingLevel` for its whole length.
  `VideoFXMusic` gains `duckingThresholdDb`, `duckingAttack`, `duckingHold`
  and `duckingRelease`, with those values as defaults (so existing exports
  sound the same), checked by `VideoFX_Export` like the other music settings.
  - `videofx`: `--duck-threshold DB`, `--duck-attack S`, `--duck-hold S`,
    `--duck-release S`.
  - `VideoFXTest`: a steady loud background ducks with the speech defaults
    and not with the threshold raised above it, a louder moment still does,
    a short hold and release bring the music back where the speech hold
    would not, and out-of-range values are refused.
- **CI: EmailCleaner's engine tests run on Windows.** `build.yml` built
  `EmailCleanerEngineTests` on every row but only Linux ran it; the Windows
  rows now run it too (`ctest -R "^EmailCleanerEngine$"`). The one assertion
  that assumed `/` separators (`Attachment_CachePathFollowsUltraMailsLayout`)
  now builds its expected path the way the code does.
