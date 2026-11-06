- **UltraCloud's "Add cloud account" dialog is translated.** Every text a
  person reads in it - the title, the row captions, the placeholders, the
  default-account checkbox, the buttons, the per-provider hint and the
  status messages - goes through `Tr` / `TrF`
  (`UltraCloudAccountDialog.cpp` 0.8.0), and the OAuth hint is one sentence
  with the provider's name as `{0}` instead of an English possessive. The
  dialog is shown by UltraFiler and UltraPress, so its 25 texts are in the
  standard set, drafted in all thirty languages: `extract_ui_strings.py`
  now scans `UltraCloud/ui` with the framework. The default upload folder
  (`/Shared from ULTRA OS`) stays the same in every language - it is a
  folder name on the server.
