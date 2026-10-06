- **A clipboard history that outlives the program that showed it.**
  `UltraCanvasClipboardHistory` (library `UltraClipboardHistory`) keeps every
  copy - files with the cut flag, images as copied, text with its HTML - in
  an SQLite database (UltraDatabase) with one content-addressed file per
  payload, under `<data>/ultraos/clipboard/`. Titles, texts and payloads are
  sealed with XChaCha20-Poly1305 (UltraCrypt) under an owner-only key file
  kept apart from them; images get 96 px thumbnails. Each entry is described
  when it is recorded (text, code, formatted text, link, colour, image,
  files; title, preview, source program, sizes), the same content copied
  again moves to the top, and a copy marked secret is never recorded.
  `List` searches case- and accent-insensitively and filters by kind;
  `Remove` / `Restore` give an undo, `Replace` an edited copy beside or in
  place of the original, `SetPinned`, `Clear`, and a policy of limits,
  pause, thumbnails and excluded programs. Two processes share one history:
  a generation number tells the other side to reload, and a lease lets
  exactly one of them record. See `Docs/UltraCanvas/UltraCanvasClipboardHistory.md`.
  - `UltraCanvasClipboardRecorder` records from a UI timer, and puts the last
    copy back on the clipboard when the program that owned it quits (X11).
  - `CaptureClipboard` / `RestoreToClipboard` move a `ClipboardSnapshot`
    between the live clipboard and the history; `EditClipboardText` (trim,
    join lines, upper / lower / title / sentence case for Latin, Greek and
    Cyrillic) and `ClipboardImageFile` serve an editor.
  - `ClipboardHistoryListModel` and `ClipboardHistoryRowDelegate`
    (`UltraCanvasClipboardHistoryView.h`) show a history in an
    `UltraCanvasListView`: section headers, a thumbnail by kind, a meta line
    and painted Copy / Edit / Delete, in a light style and the desktop's dark
    compact one.
  - The X11 clipboard learns of a new copy from XFixes
    (`XFixesSelectSelectionInput` on a connection and thread of its own) when
    libXfixes is found. Before, `HasClipboardChanged` fetched the clipboard's
    text every 100 ms and compared it, which also missed every copied image
    and file list; that stays the fallback without XFixes.
    `HasClipboardOwner()` is new on the backend.
  - `Tests/ClipboardHistoryStoreTest.cpp` (headless).
