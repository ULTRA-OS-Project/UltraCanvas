- **UltraCanvasFilerWidget: the delete-problem dialog is translated whole.**
  Its titles took the English "file" / "folder" and `TrashDisplayName()`
  ("Trash", "Recycle Bin") as arguments, so every language showed those
  words in English mid-sentence, and the rest of the dialog - the
  question, the facts' labels, the note, the "apply to all" label and the
  buttons - was not translated at all. Every text in it now goes through
  `Tr` / `TrF` as a whole sentence, per kind and per platform's trash name,
  with the item's name as `{0}` so a translation can place it; all thirty
  languages carry them.
