- **Claude reads text: the Anthropic vision adapter.**
  `CreateAnthropicVisionAnalyzer` (`UltraAIAnthropicVisionAnalyzer.h`, in
  `UltraAI_AdapterAnthropic`) is an `IVisionAnalyzer` for `VisionTask::Ocr`
  over the Messages API, built on the Anthropic text adapter (its
  serialization, key resolution, error mapping and transport). The picture
  goes as a base64 image block with an instruction to transcribe every word
  in reading order - lines, paragraphs, table rows by tabs, nothing
  translated or corrected - and the transcription comes back as structured
  output (`{"text": ...}`), so no commentary is mixed in. Defaults:
  `claude-opus-5-5`, effort `low`, server-side refusal fallbacks
  (`"fallbacks": "default"`, beta `server-side-fallback-2026-07-01`); a
  configured `output_config` or `fallbacks` replaces them. A refusal is
  `ContentFiltered`, a cut-off transcription an error, other tasks
  `InvalidRequest`. Without a key or a vault reference in the config it
  reads the UltraVault entry `ai.anthropic.api_key`, so the default route can
  build it. Registered as `"anthropic"` for `"visionanalyzer"`: a cloud
  provider, used only once chosen. With `RegisterUltraAITextRecognition` it
  reads pictures and scanned pages for `UltraCanvasFileLoader::ExtractText`
  (UltraClipboard's *Extract text*); pictures over 3.75 MB of PNG are halved
  there until they fit the API's image limit. `ultraai.anthropic_vision`
  tests it offline.
