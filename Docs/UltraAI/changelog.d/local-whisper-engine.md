- **A local whisper engine in the gallery: speech to text on this
  computer.** The built-in gallery publishes whisper.cpp's `whisper-server`
  v1.9.0 - the upstream CPU builds for Linux x64, Linux arm64 and Windows
  x64, pinned by SHA-256 - and the catalogue offers Whisper Small (the
  default), Whisper Base and Whisper Large v3 Turbo (Q5_0) under a new
  `transcription` topic. The runtime starts it on loopback with
  `--inference-path /v1/audio/transcriptions --language auto`, so the
  `local` provider now serves `ISpeechToText` (`CreateLocalSpeechToText`)
  through the OpenAI adapter: WAV, MP3, FLAC and Ogg Vorbis as they are, no
  size limit, translation through whisper's own `translate`, segments and
  (when asked) word timings, 30 minutes' timeout by default. `local` is
  first on the speech-to-text route, so UltraClipboard's *Extract text* on a
  recording or video runs on this computer once the model is there; with it
  not downloaded under the *Ask* policy the transcriber steps aside for the
  next provider, and the error says what to install.
- **Models whose engine has no build here are not offered.** `ListModels`
  and `Recommend` leave out a catalogue model whose engine has no gallery
  build for this platform, no installed bundle and no override - the whisper
  models on macOS and Windows on ARM, where whisper.cpp publishes no server
  (`ULTRAAI_LOCAL_ENGINE_WHISPER_SERVER` brings one's own). A model on an
  engine with CPU builds only here (whisper-server; llama-server on an Intel
  Mac) fits as *runs on CPU*, whatever the GPU, unless the engine is an
  override.
- **User whisper models.** `AddModelFile` and `AddHuggingFaceModel` take
  `"speechtotext"` (a whisper.cpp ggml `.bin`, served by `whisper-server`);
  the dashboard's *Local models* page adds a `.bin` as a speech-to-text
  model and labels it so, and the *Get a local model* dialog names a
  speech-to-text model. The OpenAI adapter reads the word timings inside
  each segment, as whisper.cpp's server returns them.
- The model server's seed carries the new engine, models and topic.
