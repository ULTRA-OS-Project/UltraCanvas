- **Android plays video and records from the camera.** Until now Android
  builds had no video backend, so `UltraCanvasVideoPlayer` could not open
  anything and there was no camera. The new backend uses only the NDK's own
  libraries (`mediandk`, `camera2ndk`, `aaudio`), so it is on by default and
  does not wait for the cross-compiled dependency sysroot:
  - **Playback** decodes with MediaCodec and plays the sound through AAudio,
    with the picture kept in step with what is heard. Phone recordings come
    out upright. Seeking is frame-accurate, and looping, volume, mute and
    playback rate work. Sound plays at 1× only, because AAudio cannot change
    speed.
  - **Thumbnails** come from one decoded frame, without opening an audio
    device.
  - **Cameras** are listed with their sizes. The preview is upright, and
    mirrored for the front camera. Recording writes MP4 with H.264 or H.265
    video and AAC sound, and pausing cuts the pause out of the file.
  - **Permissions:** `UltraCanvasVideoDevices::RequestCameraPermission` shows
    Android's prompt. The template manifest says which permissions to
    declare.
  - Like the rest of the Android backend, this is compiled but has not yet
    run on a device.
