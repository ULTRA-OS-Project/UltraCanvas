#### 2026-10-11 *0.1.3*
- **Rest the pointer on an image's thumbnail and the image opens at full
  size beside it.** The preview covers the right 60 % of the list, in a
  12 px frame, so the thumbnails and the start of every title stay readable
  on the left; a small image is shown at its own size rather than blown up
  to fill the pane, and the wheel zooms in on it. The preview is the
  framework's media viewer, so copied files preview too when their first
  file is something it shows - a picture, a PDF, a drawing, a 3D model, a
  video's first frame (silent). It stays while the pointer is on that
  entry's row or on the preview itself, follows the wheel scrolling the list
  under a still pointer, and closes on Escape or when the pointer goes
  elsewhere.
- **Two styles of preview, in Settings under "Image preview".** *Modern*
  (the default) is the pane above. *Classic* is the picture alone, in its
  12 px frame, floating over the middle of the list at the size it fits in
  60 % of the list's width - no info line, nothing but the image. The choice
  is kept with the history's settings.

#### 2026-10-10 *0.1.2*
- **The More button at the top right shows its three dots on Windows.** It
  was the character "⋯", which the bundled Ubuntu font does not have, so
  it was left to whatever fallback font the system offered - and on Windows
  it drew as a box of hex digits. It is an icon now, drawn like the
  Settings button beside it. The edit dialog's *Case* button had the same
  fault with its "▾" arrow, and now uses the framework's menu chevron.

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
