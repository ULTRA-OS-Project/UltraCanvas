- **VideoFX 0.15.1: no experimental encoder is chosen, listed or reported.**
  Built on an FFmpeg without libvorbis, VideoFX took FFmpeg's own Vorbis
  encoder for Ogg (and Vorbis in WebM and MKV) and reported Ogg as written,
  but that encoder is marked experimental and takes stereo only: any other
  export failed with "Cannot open the vorbis encoder: Operation not
  permitted". Every encoder VideoFX picks, lists (`VideoFX_ListVideoEncoders`)
  or counts (`VideoFX_IsAudioEncoderAvailable`, `VideoFX_GetFileFormats`) now
  comes through `FindEncoderByName` (`VideoFXBackend.h`), which leaves out
  the ones FFmpeg marks experimental - its own Vorbis and Opus encoders. Ogg
  then takes Opus or FLAC, or is not reported as written; the two places
  that opened an experimental encoder in experimental mode are gone.
  Reproduced on FFmpeg 9.0.2 without libvorbis, where `VideoFXTest`'s "every
  type it writes is written, and read back" failed on Ogg and now passes.
- **The Windows CI rows run `UltraVideoDocumentTest` too**, beside
  `VideoFXTest` (`ULTRACANVAS_BUILD_VIDEOFX_TESTS`, now in
  `Tests/VideoFXTests.cmake`): UltraVideo's document on the engine, and the
  `.ultravideo` file's paths written with `/` on Windows (UltraVideo 0.6.1).
