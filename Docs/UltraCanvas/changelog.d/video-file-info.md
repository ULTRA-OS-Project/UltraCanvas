- **A video's Details panel and status bar now say what the clip is.** The
  media viewer (UltraFiler's preview, UltraViewer) showed only a video's file
  name, path, size and type: the playback backends fill `VideoStreamInfo`
  unevenly, and Media Foundation reports nothing but the size and the length.
  New `UltraCanvasVideoFileInfo.h` - `ReadVideoFileInfo` / `VideoFileInfo` -
  reads MP4 / MOV / 3GP (fragmented too), Matroska / WebM and AVI headers
  directly, with no decoder: container and brand, duration, overall and
  per-track bit rates, video codec with profile and level, coded and display
  size, aspect ratio, frame rate, frame count, bit depth, rotation, audio
  codec (AAC LC / HE-AAC told apart), sample rate, channels, language, track
  counts, title, encoder and creation time. The status bar now reads
  `clip.mp4 · VIDEO · 1920 x 1080 · 29.97 fps · H.264 · AAC LC 48 kHz Stereo ·
  1:23 · 17.5 MB · 2 / 2`; the Details panel gets *Video*, *Audio* and
  *Tracks* sections. A container the reader does not know (WMV, MPEG-TS) falls
  back on the backend's stream info. `VideoFileInfoTest` checks it against
  MP4, fragmented MP4, MOV, Matroska and AVI headers built in memory.
