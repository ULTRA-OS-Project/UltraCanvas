- **VideoFX 0.13.0: voice-overs and sound effects at any time.** The new
  `VideoFXExportSettings::audioClips` holds free audio clips. Each is a sound
  file placed at a time of the export, independent of the cuts, with its own
  range of the file (`start`, `end`), `volume` and fades.
  - New: `VideoFXAudioClip`, with `Sound(path, at, volume)` and
    `VoiceOver(path, at, volume)`.
  - The clips are mixed with the segments' sound before the music. A clip
    with `ducksMusic` (every `VoiceOver`) is heard by the ducker, so the music
    dips under narration as it does under speech. Sound effects are added
    after the ducker and leave the music alone.
  - Clips may overlap each other and the cuts. One running past the end of
    the export is cut off there; the export is not lengthened. A silent
    timeline with a voice-over gets a sound track.
  - Each clip's file is opened when the timeline reaches it and closed when
    it has played.
  - Refused before anything is written: a missing file, one without sound,
    a start past the file's end, or a value out of range.
  - Saved in project files (missing clips are reported like missing
    songs), and carried by slideshows and drafts.
  - `videofx`: `--sound FILE@SECONDS[:VOLUME]` and
    `--voice FILE@SECONDS[:VOLUME]`, repeatable.
