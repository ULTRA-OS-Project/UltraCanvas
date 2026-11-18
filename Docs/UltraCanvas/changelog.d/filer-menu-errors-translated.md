- **The Filer's context menu, default names and error messages were English on
  every desktop.** `UltraCanvasFilerWidget` now translates them all:
  - **Context menu:** every entry (Extract and Run, Copy, Cut, Paste, Delete,
    Delete Permanently, Duplicate, Rename, New > Folder, Extract, Print,
    Settings, "(no applications)") and the Display submenu's sort fields,
    view types, thumbnail / detail-view file kinds, dataset fields,
    extension tags, file-icon styles and switches. The public
    `PreviewTypeLabel` / `ExtensionBadgeLabel` / `FileIconStyleLabel` stay
    English: tests and UltraFiler use them as names and identifiers.
  - **Default names** a person reads and keeps on disk - "New folder",
    "Pasted image", "Pasted text" - are translated as file and folder names
    (`TrC("folder name", ...)`, `TrC("file name", ...)`).
  - **Errors:** every `ReportError` message is a whole translated sentence,
    with paths and system errors as `{0}` / `{1}`; the extraction summary's
    title, caption and "skipped" / "cancelled" reasons too.
    `RefuseWriteHere` takes the whole refusal for its command
    (`Tr("Cannot rename on a drive: ...")`) instead of an English verb
    spliced into an English sentence.
  - Standard set: the new texts arrive drafted in every language under
    `media/locale/` (machine draft, reviewed by -); "List", "Treemap",
    "Audio", "3D", "Docs", "Spreadsheets", "Bar size" and "Hidden files"
    move up from the UltraFiler / UltraDesktop / ChatCube / demo catalogues
    and the Computer base set, keeping their translations.
  - **`UltraCanvasFilerWidget::PreviewTypeText(kind)`** (new): a preview kind's
    name in the reader's language, shared by the Display submenus and an
    application's settings; `PreviewTypeLabel` stays the English name.
  - The extract-conflict dialog (captions, note, "Apply to all remaining
    archives", Keep both / Extract into existing / Skip / Stop) and the
    progress window of a pack, an unpack, Extract and Run and an
    administrator delete (titles, "Unpacking \"{0}\" (1 of 3)" captions,
    the Windows permission line) are translated too.
