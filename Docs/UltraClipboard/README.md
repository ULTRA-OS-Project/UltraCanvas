# UltraClipboard

The clipboard history of ULTRA OS: everything you copied, with a thumbnail and
Copy, Edit, Save as and Delete on every entry. Find a copy from last week,
use it again, change it first, save it as a file, or pin it so it stays.

Source: [`Apps/UltraClipboard/`](../../Apps/UltraClipboard/). Changelog:
[`CHANGELOG.md`](CHANGELOG.md). The history itself:
[UltraCanvasClipboardHistory](../UltraCanvas/UltraCanvasClipboardHistory.md).
Design and mock-ups: `Docs/Research/UltraClipboardDesignProposal.md`.

## Where the history comes from

UltraDesktop records every copy — texts, links, colours, code, formatted
text, images and files — into a history on disk, and its quick panel
(`Super+V`, or the clipboard button on the right bar) shows the pinned and
the latest entries. UltraClipboard shows the whole history. Both work on the
same history, so a change in one appears in the other within a second.

Where no UltraDesktop records — another desktop, Windows, macOS —
UltraClipboard records while it is open. When the desktop starts it takes
over.

Copies a password manager marks as secret are never recorded (UltraPassword,
KeePassXC and the others that mark theirs), and copies made in the programs
listed in the settings are left out.

## Using it

**The list.** Pinned entries come first, then Today, Yesterday and Earlier.
Every row has a thumbnail by kind (the picture itself for an image, the
colour for a colour, a sheet with a `+n` badge for several files), the title,
a line on what it is, which program it came from and when, and four buttons:

| Button | Key | |
|---|---|---|
| Copy | Enter, double-click | puts it back on the clipboard; it moves to the top |
| Edit | F2, Ctrl+E | the edit dialog; an image opens in UltraPaint |
| Save as | Ctrl+S | writes the entry to a file, then asks *Delete the item from the clipboard?* |
| Delete | Delete | with *Undo* in the status bar for 8 seconds (Ctrl+Z) |

The buttons stand right of the entry's text, or left of it between the
thumbnail and the text: Settings, *Display → Icons*. The desktop's quick
panel follows the same setting.

Right-click a row for the same and *Pin* / *Unpin* (Ctrl+P).

**Saving as a file.** *Save as* writes the entry where you choose, through
the framework's Save dialog: a text, code, link or colour as a text file;
formatted text as HTML (as it was copied), ODT or DOCX, or as its plain
text; an image as any picture type this build writes (in the type it was
copied in, at its own size, it is written as it came, nothing re-encoded;
the type's Settings can give another size); copied files are copied into the
folder you choose, and files that were *cut* are moved there, as pasting them
would. A file of the same name already in that folder is replaced only when
you say so. Once saved, UltraClipboard asks *Delete the item from the
clipboard?* - Yes deletes it, with *Undo* as for any deletion; No keeps it.
The next *Save as* starts in the folder the last one saved to. Up and Down move
through the list, skipping the section headers.

**Finding.** Type in the search field (Ctrl+F) to search titles and texts,
whatever their case and accents. *All*, *Text*, *Images*, *Files*, *Links* and
*Pinned* narrow the list. Escape clears the search.

**Editing.** The edit dialog holds the text with four tools: *Plain text*
(formatted text only: keep the words, drop the formatting), *Trim* (spaces at
the ends of lines, empty lines at the ends), *Case* (UPPER, lower, Title,
Sentence) and *Join lines*. *Keep the original entry as well* (on) adds the
edited text as a new entry; off, it replaces the entry. *Save and copy* also
puts it on the clipboard. An image goes to UltraPaint; copy the result there
and it arrives as a new entry. Files are edited where they are.

**Recording.** The *Recording* switch pauses the history; while it is off
nothing copied is kept (the desktop's clipboard button shows it crossed out).

**Settings** (the sliders button): how many entries the history keeps (100 to
2,000, pinned ones not counted), for how long (a day to a year, by last use),
how much in all and the largest single copy, whether images get thumbnails,
how an image previews, *Display → Icons* (the row's buttons left or right of
the item text), and the programs whose copies are never recorded. The dialog also says where
the key is kept and what the encryption protects.

**More** (`⋯`): *Find*, *Settings* and *Clear history*, which removes every
entry that is not pinned.

## From the desktop

The desktop's quick panel starts UltraClipboard for what does not fit in it:

| In the panel | Starts |
|---|---|
| *Open UltraClipboard* | `UltraClipboard`, or `UltraClipboard --search TEXT` with the panel's search |
| F2 or Ctrl+E on an entry | `UltraClipboard --edit ID`: that entry's edit dialog at once |

The desktop looks for `UltraClipboard` next to its own executable first, then
on `PATH`.

There is one UltraClipboard at a time. A start while it is open - from the
panel, the app starter or the command line - hands its search or its entry
to the open window, which comes forward, and quits. A dialog already open
there comes forward instead and keeps what is typed in it. The hand-off goes
over UltraMessage, the per-user message bus: a request on the topic
`org.ultraos.ultraclipboard.show` (`search`, `editId`) to the running
instance, which answers before it acts; the first instance hosts the bus when
nothing else does. Who runs is decided by an operating-system lock
(`UltraCanvasSingleInstance`, `UltraCanvasInstanceLock`), so two starts at the
same moment cannot both stay; without the bus a second start says that
UltraClipboard is already running instead of opening a window of its own.

## Command line

```
UltraClipboard [--search TEXT] [--edit ID]
UltraClipboard --version
```

## Privacy

The history lives in `~/.local/share/ultraos/clipboard/` (`%LOCALAPPDATA%` on
Windows, `~/Library/Application Support` on macOS, or under `$XDG_DATA_HOME`
when that is set, on every platform), in folders only you can open. Titles, texts and copies are encrypted with a key kept apart from them
in `~/.config/ultraos/clipboard.key` (`%APPDATA%\ultraos` on Windows,
`~/Library/Application Support/ultraos` on macOS, beside the desktop's
settings), so a copy of the history folder or a backup of it cannot be read
without the key. Image thumbnails are not
encrypted. A program running as you can read the clipboard itself, so the
encryption does not protect against that; leaving such programs' copies out,
or pausing the recording, does.
