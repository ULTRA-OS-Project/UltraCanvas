- **UltraCanvasVideoTile: live pictures pushed from any thread.** A new
  element (`UltraCanvasVideoTile.h`) for a call's video, a self view or
  frames a program decodes itself: `PushFrame` copies RGBA or BGRA pixels
  into a one-frame inbox from any thread, the newest frame wins and one
  upload to the UI thread is in flight at a time; `Contain` or `Cover`
  fitting, mirroring for a self view, a caption without a picture, corner
  radius and border. Catalogued under *Text, images and media*; documented
  in `Docs/UltraCanvas/UltraCanvasVideo.md`.
