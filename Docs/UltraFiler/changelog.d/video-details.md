- **A video's details now say what the clip is.** The preview's Details panel
  showed only a video's name, path, size and type, and the status bar its
  name, size and position in the folder. Both now read the clip's own headers
  (MP4 / MOV, MKV / WebM, AVI) through the framework's new
  `ReadVideoFileInfo`: the status bar adds the picture size, frame rate, video
  codec, the sound (codec, sample rate, channels - or *no audio*) and the
  length; the Details panel adds the container, exact duration, bit rates,
  codec profile and level, display size and aspect ratio, frame count, bit
  depth, rotation, the sound's channels and language, the track counts and
  the file's title, encoder and creation time.
- **The file list names a film's picture size.** A video's line under its icon
  (the Length field) and in the status bar now reads
  `1:23 · 1920 × 1080 · H.264` - a phone's portrait clip upright, an
  anamorphic one at its display width - from the same header reader the
  preview's Details use, so the two never disagree; before it said the length
  and the codec only, from a parser of the list's own.
