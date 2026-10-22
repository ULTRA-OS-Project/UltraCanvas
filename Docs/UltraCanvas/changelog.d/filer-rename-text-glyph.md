- **UltraCanvasFilerWidget: the hover icon menu's Rename button shows a "T"
  and its buttons name their keys.** The Rename button drew a pencil, which
  reads as "edit the file"; it renames, so it now draws a serif *T* beside a
  text caret - "type the name". The strip's tooltips say what each button does
  and give its shortcut: *Copy to clipboard (Ctrl+C)*, *Cut to clipboard
  (Ctrl+X)*, *Rename (F2)*, *Delete (Del)*.
- **UltraCanvasFilerWidget: the hover icon menu's Cut is greyed out on a
  drive.** The context menu greys Cut out there and Ctrl+X refuses it, but
  the hover button put the files on the clipboard as a cut anyway, ghosting
  entries whose move off the drive could not be completed. It is now drawn
  dimmed and a press does nothing; its tooltip says to copy, then delete.
