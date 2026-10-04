- **`UltraCanvasTextArea::onBeforeKeyDown`: a host can give a key a meaning of
  its own.** The callback sees every KeyDown before the area handles it, in any
  editing mode and when read-only, and consumes the key by returning true. A
  chat box can now send on Enter and keep Shift+Enter for a line break without
  reimplementing the editor - the text area's `OnEvent` never reached the
  generic `eventCallback`, so there was no way in before. UltraClaude's message
  box is the first user. Documented in `UltraCanvasTextAreaExamples.md`.
