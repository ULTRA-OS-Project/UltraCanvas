- **VideoFX: beat detection; slideshows on the beat** (VideoFX 0.5.0).
  `VideoFX_DetectBeats` gives a file's tempo (BPM), a confidence and its beat
  times - VideoFX's own analysis (spectral-flux onsets, an autocorrelation
  tempo weighted towards 120 BPM against half / double readings, and
  dynamic-programming beat tracking), reporting no beat for speech, noise or
  a held tone. `VideoFXSlideshowOptions::beatSync` puts every change (a cut,
  or a transition's middle) on the beat nearest `secondsPerImage`;
  `beatsPerImage` gives each image exactly that many beats. With a song list
  the beats come from each song where it plays.
  - `videofx beats FILE`; `videofx slideshow ... --beat-sync`,
    `--beats-per-image N`.
