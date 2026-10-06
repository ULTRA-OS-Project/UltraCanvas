- **A copied password no longer ends up in a clipboard history.**
  `SetText(text, ClipboardHint::Secret)` (and `SetClipboardText(text, hint)`)
  puts the text on the clipboard together with each platform's "leave this out
  of the history" marker: `x-kde-passwordManagerHint` = `secret` on X11 (the
  marker KDE's Klipper and KeePassXC use);
  `ExcludeClipboardContentFromMonitorProcessing`,
  `CanIncludeInClipboardHistory` = 0 and `CanUploadToCloudClipboard` = 0 on
  Windows, which also keep it out of `Win+V` and the cloud clipboard; and
  `org.nspasteboard.ConcealedType` on macOS. The clipboard's own history
  honours the same markers, from this process or any other:
  `ProcessNewClipboardContent` and the entry recorded at start-up skip content
  whose owner marked it, through the new backend call
  `IsClipboardMarkedSecret()`. Until now UltraDesktop's monitor recorded
  every text it saw, so a password copied from UltraPassword stayed in its
  clipboard menu after UltraPassword had cleared the clipboard.
  - `UltraCanvasClipboardBackend` gains `SetClipboardSecretText` and
    `IsClipboardMarkedSecret`; a backend without markers (Android,
    WebAssembly) puts plain text and reports nothing marked.
  - `UltraCanvasClipboard::InitializeWithBackend` takes a backend from the
    caller, for a platform the framework has none for, and for tests.
  - `GetEntries()` is documented as newest first, which it always was.
  - `Tests/ClipboardHistoryTest.cpp`: newest-first order, a secret copy from
    another program, from this process, and one already on the clipboard at
    start-up, against a fake backend (headless).
