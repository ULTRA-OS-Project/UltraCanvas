- **The "New RAM disc" dialog shows its whole note.** The explanation under
  the size ran to more lines than its fixed box held, so its last line was
  cut off just above the buttons. The note is now measured and as tall as
  its text, the window grows to fit it, and Create and Cancel sit at the
  bottom right (`UltraFilerRamDiskDialog.cpp` 1.1.2).
- **The rest of the "New RAM disc" dialog is translated.** The notes on how
  the memory is used, how much is free, and that everything is lost on
  eject, and the messages for a missing, too long or wrongly spelt name,
  were English in every language; they now go through `Tr` (German
  included, the other languages follow with the next catalogue draft).
