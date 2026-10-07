- **VideoFX: song lists with crossfades** (VideoFX 0.5.0). Background music
  can be several songs: `VideoFXMusic::playlist` (or
  `VideoFXMusic::FromFiles`) plays after `path`, in order, each song blending
  into the next over `crossfade` seconds (3 by default, 0..30) with
  equal-power gains, so the loudness holds steady and there is neither a hard
  cut nor a gap. A song shorter than twice the crossfade gets a shorter one,
  at most half of either song. A looping list crossfades from its last song
  into its first; a single looping song now crossfades into its own start
  (`crossfade = 0` keeps the 0.4 back-to-back loop).
  - `VideoFXSlideshowOptions::matchMusicLength` fits a slideshow to the whole
    list: the songs' lengths minus the crossfades.
  - `videofx`: repeat `--music FILE` for a list; `--music-crossfade S`.
