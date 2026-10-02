- **VideoFX: ducking presets** (VideoFX 0.4.2). `VideoFXDuckingPreset` -
  `Speech` (the defaults), `Outdoor` (-28 dBFS) and `LoudEvent` (-15 dBFS,
  back up after 0.2 s of calm) - and `VideoFXMusic::SetDuckingPreset` fill
  in the ducking threshold and times for the kind of footage, so an app can
  offer one choice instead of four numbers; the depth (`duckingLevel`) is
  left alone and single fields set afterwards refine it.
  - `videofx`: `--duck-preset speech|outdoor|loud`, applied before the single
    `--duck-*` values whatever the order on the command line.
