- **UltraCloud's dialogs are translated.** The "Attach cloud link" picker
  (`UltraCloudPickerDialog.cpp` 0.4.0) - its title, account and folder
  rows, buttons, column headers and the status lines (now whole sentences
  with the name as `{0}`) - adds 10 standard-set texts.
- **The "Add cloud account" dialog is translated.** Every text a
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
- **A failed cloud call says why in the reader's language.** The service's
  own error text is technical English built in about ninety places (often
  the network library's message passed on), so it cannot be a translation
  key; the new `UltraCloud/ui/UltraCloudResultText.h` gives
  `DescribeResult` - one translated sentence per result code (not found,
  sign-in refused, server unreachable, HTTP error with its status, rate
  limit, ...) - and `ResultDetail`, the technical message. Both dialogs show
  the sentence and keep the detail as the status line's tooltip; the
  applications that show cloud results can call the same pair.
