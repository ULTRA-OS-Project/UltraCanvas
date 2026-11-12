- **Apps can make audio and video calls (element ABI version 4).** A
  WebAssembly app adds audio (Opus) and video (VP8) tracks to a peer
  (`uc_rtc_audio_add`, `uc_rtc_video_add`) and makes the offer
  (`uc_rtc_offer`). It plays what it receives (`uc_rtc_playback`) and shows
  the pictures in a new element kind, `VideoTile`
  (`uc_rtc_video_show`). Pictures go from the engine straight to the tile
  and never pass through the app's memory. `uc_rtc_track_enabled` mutes a
  track or turns its picture off.
- **The camera and the microphone ask first.** An app's first
  `uc_rtc_camera` or `uc_rtc_microphone` shows a bar above it in its tab:
  "x.org wants to use your camera", with **Allow** and **Block**. The app
  hears the answer as `UC_RTC_PERMISSION`. The answer is kept for the
  app's origin (`<settings folder>/UltraWeb/permissions.txt`), so the bar
  asks once. **Ctrl+Shift+Delete** forgets these answers along with the
  cookies.
- Apps built for version 3 run unchanged. An app that asks for version 4
  is refused with a message by an UltraWeb that has only version 3.
  `Docs/UltraWeb/UltraWebRTCIntegration.md` marks step W3 done.
