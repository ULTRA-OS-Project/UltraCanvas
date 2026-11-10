- **`UltraCanvasFileLoader::GetSpeechTranscriber`**: the speech transcriber
  installed now, so one installed after it keeps it as its fallback - UltraAI's
  (`RegisterUltraAISpeechTranscription`, UltraAI changelog) hands a recording
  to an application's own engine when no UltraAI provider transcribes.
  FileLoader 1.13.0.
