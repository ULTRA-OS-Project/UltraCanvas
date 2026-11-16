- **The video player refuses a file it cannot open, at once.**
  `UltraCanvasVideoPlayer::LoadFromFile` (and the player element's) returned
  true for an empty file, a file that did not exist and a folder: the
  backends - GStreamer, Media Foundation, AVFoundation, Android - accept any
  path and report what they cannot play only later, on their own threads, so
  the caller showed a player that never played. It now returns false with
  the reason in `GetLastError()` and `onError` (state `Error`, nothing loaded)
  for no path, a missing file, a folder, an empty or unreadable file, before
  any backend or registered codec sees it. A damaged file is still accepted
  and reported later, as before. The media viewer's video *Save as* check
  leaves an empty file to the player. Test: `VideoCodecPluginTest`
  (`TestPlayerRefusesNoFile`). Docs: `UltraCanvasVideo.md`.
