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
    the folder being shown) beside a tree-view listing of the folder with
    folder and file icons, and a real `UltraCanvasTextInput` name field and
    `UltraCanvasDropdown` file-type picker whose labels are laid out, not
    placed.
  - The name field has caret, selection, clipboard and IME like every other
    field; Return in it accepts, Return in the path field opens the folder
    typed there, a typed folder name opens that folder, and OK with nothing
    chosen no longer closes the dialog as a silent cancel.
  - The default size is 760 × 520 (was 600 × 450) to make room for the tree.
