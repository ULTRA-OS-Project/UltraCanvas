- **Live audio: the recorder delivers frames as they arrive, and the player
  plays frames as they are pushed.** Both engines were buffer and file based
  — `UltraCanvasAudioRecorder` accumulated into a `UCAudio` for `TakeBuffer()`,
  and `UltraCanvasAudioPlayer` played a file or a buffer loaded up front — so
  a call, a speech recogniser or a streaming encoder had no path through the
  framework. Now `AudioCaptureConfig::mode = AudioCaptureMode::Live` keeps
  nothing and calls `onLiveFrame` with interleaved float PCM (gain and mute
  applied, any backend sample type converted) in frames of exactly
  `liveFrameMs` (10 for Opus / WebRTC, 20 for speech engines; 0 passes the
  backend's chunks through), with `firstFrameIndex` as a timestamp and the
  partial last frame flushed, zero-padded, on `Stop()`. And
  `UltraCanvasAudioPlayer::OpenSink(AudioSinkConfig)` opens the device at a
  given rate and channel count and plays whatever `PushSinkFrames` queues,
  from a bounded ring whose `bufferMs` is the latency ceiling: frames beyond
  it are dropped and counted, an underrun plays silence and fires
  `onSinkUnderrun` once per episode, and `GetSinkQueuedSeconds()` lets the
  producer pace itself. The building blocks — `AudioLiveFrame`, the
  wait-free SPSC `AudioFrameRing`, `AudioFramePacketizer` — are header-only
  in `UltraCanvasAudioStreaming.h`, backend-free, and covered by
  `Tests/AudioStreamingTest.cpp`. Also fixed in passing: the player's device
  callback dereferenced a null source when asked to fill with nothing loaded.
  `Docs/UltraCanvas/UltraCanvasAudio.md` documents both modes.
