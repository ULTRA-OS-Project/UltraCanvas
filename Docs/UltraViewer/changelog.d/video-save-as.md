- **A video can be saved in another type.** UltraViewer registers VideoFX's
  converter at start-up (`VideoFX_RegisterWithFileLoader`, where VideoFX is
  built), so the toolbar's *Save as* is there for a film the player opened
  and offers every container this build's FFmpeg writes - MP4, MOV,
  Matroska, WebM, AVI. The conversion runs behind a progress ring with
  Cancel; see the framework changelog.
