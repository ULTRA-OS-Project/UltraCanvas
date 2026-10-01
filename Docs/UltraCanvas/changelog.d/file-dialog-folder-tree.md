- **The framework's file dialog had no folder tree, and its labels sat on
  top of the fields.** `UltraCanvasFileDialog` — what every app with native
  dialogs off gets from `UltraCanvasFileLoader` (UltraMail's *Attach file*,
  UltraAI, UltraAuthenticator, UltraCleaner, the UltraCloud pickers) — painted
  a flat `[D]` list, a path bar, a name field and a type selector by hand at
  fixed pixel offsets. With Windows font scaling the path text dropped out of
  its bar and "File name:" / "Files of type:" ran into the fields.
  - It is now built from elements: an editable path field with an *up*
    button, an `UltraCanvasTreeView` of folders (Home, Desktop, Documents,
    Downloads and every mounted drive, read as they are expanded and kept on
    the folder being shown) beside the folder's listing, which is
    `UltraCanvasFilerWidget` — the same display as UltraFiler, with its
    icons, Details columns, sorting and keyboard — and a real `UltraCanvasTextInput` name field and
    `UltraCanvasDropdown` file-type picker whose labels are laid out, not
    placed.
  - The name field has caret, selection, clipboard and IME like every other
    field; Return in it accepts, Return in the path field opens the folder
    typed there, a typed folder name opens that folder, and OK with nothing
    chosen no longer closes the dialog as a silent cancel.
  - A row of view buttons beside the path field switches the listing
    between Details, List and small / medium / large / extra-large icons,
    with the current one marked (an `UltraCanvasSegmentedControl`).
  - The dialog window can be resized; the tree and the listing take up the
    space.
  - The dialog remembers the view chosen and the size it was left at, per
    user and for every application, in `FileDialog.conf` in the UltraCanvas
    settings folder (`%APPDATA%\UltraCanvas`, `~/Library/Application
    Support/UltraCanvas`, `$XDG_CONFIG_HOME/UltraCanvas` or
    `~/.config/UltraCanvas`); it is written whenever the dialog closes.
  - Details shows Name, Size, Type and Modified, so the name column gets
    the width (it was squeezed to its 120 px minimum by seven columns).
  - The default size is 900 × 560 (was 600 × 450) to make room for the tree.
- **`UltraCanvasFilerWidget::SetEntryFilter(predicate)`** limits the listing
  to the entries a host accepts — the file dialog's *Files of type* filter,
  and folders only in a folder picker. Such entries are not "hidden" and are
  never counted into the hidden-items notice.
- **`UltraCanvasFilerWidget::SetDetailsColumnVisible(column, visible)`**
  leaves a Details column out of the table (Name always stays), so a compact
  display gives the name the width the others would take.
