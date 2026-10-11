- **VideoFX 0.8.0: waveforms.** `VideoFX_ExtractWaveform(path, waveform,
  blocksPerSecond, start, end)` gives the loudness envelope of a file's
  sound - a song, or a video's soundtrack - mixed to mono and cut into blocks
  of equal length, each with its lowest sample, its highest sample and its
  RMS (`VideoFXWaveform`, with `BlockAt` and `Peak`). It is what a timeline
  or a player draws as the waveform, and its member names match
  `UltraCanvasWaveformElement`'s `WaveformChannelData`, so
  `SetFromAudioFX(waveform)` feeds it without an adapter. Fifty blocks a
  second, the default, holds a three-minute song in some 100 KB; a range
  takes only part of the file. A file without sound fails with
  `NoMediaStreams`. `videofx waveform <file> [blocksPerSecond] [start [end]]`
  prints it. The mono decoder behind beat detection now streams at any
  sample rate and is shared by both calls.
