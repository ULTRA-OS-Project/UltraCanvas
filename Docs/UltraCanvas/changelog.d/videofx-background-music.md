- **VideoFX: background music** (VideoFX 0.4.0).
  `VideoFXExportSettings::music` (`VideoFXMusic`) lays a song - any file with
  sound, a video's soundtrack included - under a whole export, slideshow or
  not: volume, start offset, looping (or silence after the end), fade in and
  out over the export, and **ducking**: where the segments have sound of
  their own the music drops to `duckingLevel` within about 0.1 s and returns
  after a pause, without pumping between words. It is mixed in as the sound
  is encoded, so it runs straight through joins, transitions and padding;
  the level detection and gain ramps are VideoFX's own, identical on FFmpeg
  4.4 to 8.x.
  - `VideoFXSlideshowOptions::music` and `matchMusicLength`: a slideshow
    whose seconds per photo are chosen to end with the song.
  - Music makes a timeline of silent pictures produce sound, so photos plus
    music can be written to MP3 / WAV.
  - Fix: a sound-only export of photos with transitions came out too short -
    each photo after the first ended where the previous transition's held
    sound did, instead of after its own length.
  - `videofx`: `--music`, `--music-volume`, `--music-start`, `--duck`,
    `--no-loop`, `--fit-music`. `VideoFXTest` grows to 298 checks.
