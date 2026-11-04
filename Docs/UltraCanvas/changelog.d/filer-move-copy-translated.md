- **UltraCanvasFilerWidget: the move / copy dialogs are translated whole.**
  The drop confirmation ("Move 3 items into …?", From / Into / Size, the
  Copy / Move / Cancel buttons), the copy / move problem dialog, the
  paste name-conflict dialog (Existing / Pasted rows with their newer /
  older notes, Merge / Keep both / Replace, the "apply to all remaining
  conflicts" scope) and the rename name-conflict dialog were English in
  every language. Every text in them now goes through `Tr` / `TrF` /
  `TrN` / `TrC` as a whole sentence per verb and kind, names as `{0}`,
  counts in each language's plural forms; all thirty languages carry
  them.
