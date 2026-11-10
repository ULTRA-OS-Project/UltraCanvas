- **Speech to text for UltraCanvas applications.**
  `RegisterUltraAISpeechTranscription()` (`UltraAISpeechTranscription.h`, in
  `UltraAI_UI`) makes UltraAI `UltraCanvasFileLoader`'s speech transcriber:
  the words spoken in a sound file or a video come from the first
  speech-to-text provider on the default route (chosen, then local, a cloud
  one only when chosen or allowed, never `mock`), chosen again at every file.
  A sound file of a type the provider takes, within its size limit, goes as
  it is; every video, another type or a file over the limit has its sound
  track taken out with VideoFX first, as a mono 16 kHz MP3 at 64 kbit/s
  (about 50 minutes in 25 MB). Without a provider the transcriber installed
  before it transcribes. `FindSpeechProvider` and
  `LastSpeechTranscriptionProvider` say which. `UltraAI_UI` links VideoFX
  where it is in the build. `UltraAISpeechTranscriptionTest` covers it with a
  generated WAV, the MP3 conversion, and a video where the ffmpeg command
  line is installed.
- **OpenAI speech to text.** `CreateOpenAISpeechToText` (`UltraAIOpenAI.h`)
  transcribes over the audio API: a multipart upload to
  `/v1/audio/transcriptions` (or `/v1/audio/translations` with `whisper-1`),
  `gpt-4o-transcribe` by default, `whisper-1` with segment and word
  timestamps (`verbose_json`), the language as its primary subtag, options as
  form fields, 25 MB refused before upload, errors mapped. Without a key in
  the config it reads the UltraVault entry `ai.openai.api_key`. Against a
  custom `baseUrl` - a self-hosted OpenAI-compatible whisper server - it runs
  keyless with `whisper-1` by default. Live transcription is not supported.
  Registered as `"openai"` for `"speechtotext"`, a cloud provider used once
  chosen (`ULTRAAI_DEFAULT_SPEECHTOTEXT=openai`). `ultraai.openai_speech`
  tests it offline.
