- **Extract text on recordings and videos.** A copied sound file or video is
  transcribed by an UltraAI speech-to-text provider: OpenAI's, once chosen
  (`ULTRAAI_DEFAULT_SPEECHTOTEXT=openai`, the key in UltraVault under
  `ai.openai.api_key`), or a whisper server of your own. A video's sound is
  taken out first. The words arrive as a new text entry, on the clipboard, and
  the status bar says which provider transcribed them ("read by openai").
  Without a provider *Extract text* says that speech to text is not available
  (UltraAI changelog: `speech-to-text`).
