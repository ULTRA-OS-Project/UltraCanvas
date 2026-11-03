- **UltraCanvasFilerWidget: the delete-problem titles are translated whole.**
  "Delete: a {0} is write-protected", "... could not be deleted" and "...
  could not be moved to the {1}" took the English "file" / "folder" and
  `TrashDisplayName()` ("Trash", "Recycle Bin") as arguments, so every
  language showed those words in English mid-sentence. They are now eight
  whole sentences, per kind and per platform's trash name, translated in
  all thirty languages with each language's own noun, gender and case.
