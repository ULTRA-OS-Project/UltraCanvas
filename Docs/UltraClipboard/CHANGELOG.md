#### 2026-10-07 *0.1.1*
- **One UltraClipboard at a time.** Starting it while it is already open -
  from the desktop's quick panel (F2 or Ctrl+E on an entry, *Open
  UltraClipboard*), the app starter or the command line - brings the open
  window forward instead of opening a second one. The search the panel
  carried over is applied there, and an entry asked to be edited opens in
  that window's edit dialog, shown in the list behind it. A dialog that is
  already open comes forward and keeps what is typed in it. The request
  travels over UltraMessage, the per-user message bus
  (`org.ultraos.ultraclipboard.show`); where there is no bus, every start
  opens its own window as before.

#### 2026-10-06 *0.1.0*
- **First release of UltraClipboard, the clipboard history of ULTRA OS.**
  Every copy the desktop records is listed with a thumbnail by kind - text,
  code, formatted text, link, colour, image, files - its title, a line on
  what it is, where it came from and when, and Copy, Edit and Delete on the
  row. Pinned entries come first, then Today, Yesterday and Earlier.
- **Find anything copied.** The search matches titles and texts without
  regard to case or accents; All / Text / Images / Files / Links / Pinned
  narrow the list.
- **Edit a copy before using it again.** Text kinds open in an edit dialog
  with Plain text (drops the formatting), Trim, a case menu (UPPER, lower,
  Title, Sentence) and Join lines. *Save* replaces the entry or, with
  "Keep the original entry as well", adds the edited one beside it; *Save
  and copy* also puts it on the clipboard. Images open in UltraPaint.
- **Delete with Undo**, pin to keep an entry whatever the limits say, and
  *Clear history* for everything not pinned.
- **Settings**: how many entries, for how long and how much the history
  keeps, the largest copy it records, image thumbnails, and the programs
  whose copies are never recorded (the password managers to begin with).
- **The keyboard reaches everything**: Up / Down choose, Enter copies, F2 or
  Ctrl+E edits, Delete deletes, Ctrl+P pins, Ctrl+Z undoes a deletion,
  Ctrl+F finds and Escape clears the search.
- The history is shared with UltraDesktop, whose quick panel (Super+V)
  opens UltraClipboard with its search or an entry's edit dialog. Where no
  desktop records - another desktop, Windows, macOS - UltraClipboard
  records while it is open. Titles, texts and copies are encrypted on disk.
