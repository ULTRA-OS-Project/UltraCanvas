- **UltraCanvasFilerWidget: the delete dialogs are translated whole.** The
  problem dialog's titles took the English "file" / "folder" and
  `TrashDisplayName()` ("Trash", "Recycle Bin") as arguments, so every
  language showed those words in English mid-sentence, and the rest of it
  - the question, the facts' labels, the note, the "apply to all" label
  and the buttons - was not translated at all; nor was any of the delete
  confirmation (its question, From / Size facts, the folder's list caption,
  the notes, Move to Trash / Delete permanently). Every text in both now
  goes through `Tr` / `TrF` / `TrN` as a whole sentence, per kind and per
  platform's trash name, with names as `{0}` so a translation can place
  them and counts in each language's plural forms; the size facts
  (`DescribeEntries`, `CountedSizeFact`) count files and folders the same
  way. All thirty languages carry them.
