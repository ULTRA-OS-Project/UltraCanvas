- **The delete confirmation lists what is about to go, with icons.** Deleting
  a folder showed a wrapping grid of 64-pixel tiles for its first ten entries.
  Only image files got a picture there: a folder, a DLL or a certificate was
  an empty square over a name cut at eleven bytes, and several selected items
  were not shown at all. The dialog now has an `UltraCanvasListView` in the
  Details view's form:
  - Columns: icon and name, size, modified. The icon is the display's own
    (`DrawEntryIcon`, via a small list delegate), so every row gets the glyph
    or host icon the file display gives that entry. Sizes and dates are
    formatted as in the Details view.
  - Several items selected: the list is those items, with a caption that
    counts folders and files and adds up the files' size. One folder: the
    list is its contents, folders first and then by name (only the rows
    shown are stat-ed), with *Folder "X" contains N items (first 40 shown)*.
    A single file gets no list. At most 40 rows, ten visible, with a
    scrollbar; each row's tooltip is the full path.
