# UltraCanvasClipboardHistory

<!-- doc-check: std::string ActiveApplicationName(); void RefreshList(); void Copy(int64_t id); void Edit(int64_t id); void Delete(int64_t id); -->

The clipboard history of ULTRA OS: every copy, in every format the
framework's clipboard reads, kept on disk so it outlives the program that
made it. UltraDesktop records into it and shows it in its quick panel
(`Super+V`); the [UltraClipboard](../UltraClipboard/README.md) application
shows, searches and edits the same history. Any other application can read it
or record into it.

Headers: `UltraCanvasClipboardHistory.h` (the store, the recorder, capture and
restore) and `UltraCanvasClipboardHistoryView.h` (a list model and a row
painter for `UltraCanvasListView`). Library: `UltraClipboardHistory` (CMake),
which links UltraCrypt; without UltraDatabase (no libsqlite3) it builds and
`IsAvailable()` answers false. Design: `Docs/Research/UltraClipboardDesignProposal.md`.

## Quick start

```cpp
#include "UltraCanvasClipboard.h"
#include "UltraCanvasClipboardHistory.h"
using namespace UltraCanvas;

UltraCanvasClipboardHistory history;
if (!history.Open()) {                       // the per-user default place
    debugOutput << "no clipboard history: " << history.GetLastError() << std::endl;
    return;
}

// What is on the clipboard now, into the history.
ClipboardSnapshot copy;
if (CaptureClipboard(*GetClipboard(), copy)) history.Record(copy);

// Pinned first, then the most recently used.
for (const ClipboardHistoryEntry& entry : history.List()) {
    std::cout << ClipboardEntryKindName(entry.kind) << ": " << entry.title << "\n";
}

// Put the third entry back on the clipboard.
std::vector<ClipboardFormat> formats;
const int64_t id = history.List()[2].id;
if (history.ReadFormats(id, formats) && RestoreToClipboard(*GetClipboard(), formats)) {
    history.MarkUsed(id);                   // it is the newest again
}
```

## What is kept

A `ClipboardSnapshot` is a list of `ClipboardFormat { mime, data }`. Capture
takes, in this order of preference:

| Content | Formats |
|---|---|
| Files | `application/x-ultracanvas-files` (`ClipboardMime::Files`): `copy` or `cut`, then one path per line |
| An image | its own type (`image/png`, `image/jpeg`, `image/bmp` for a Windows `CF_DIB`, …), as copied |
| Text | `text/plain;charset=utf-8` (`ClipboardMime::Text`), plus `text/html` (`ClipboardMime::Html`) when the HTML is markup and differs from the text |

A copy whose source marked it secret (`ClipboardHint::Secret`, KeePassXC's
`x-kde-passwordManagerHint`, Windows' `ExcludeClipboardContentFromMonitorProcessing`)
is never recorded. Bytes that do not start with an image signature are not an
image, whatever the target says: an owner that serves the same bytes for every
target (xclip) would otherwise turn a text into one.

Each entry (`ClipboardHistoryEntry`) is described when it is recorded:

| Field | |
|---|---|
| `kind` | `Text`, `Code`, `RichText`, `Link`, `Colour`, `Image`, `Files` |
| `title` | first line, file names (`Invoice.pdf, Contract.odt and 1 more`), URL, `Image 1920 × 1080` |
| `preview` | the first 4 KB of the text; searched |
| `sourceApplication` | what the recorder's `sourceProvider` answered (the active window's application on the desktop) |
| `copiedAt`, `lastUsedAt`, `useCount` | Unix ms |
| `pinned` | kept whatever the limits say; listed first |
| `sizeBytes`, `width`, `height`, `fileCount`, `cut`, `lineCount` | for the meta line |
| `thumbnailPath` | images: a PNG at most 96 px on its longer side |

The same content copied again is not a second entry: it moves to the top
(`ClipboardRecordResult::MovedToTop`).

## Reading and acting

| Call | |
|---|---|
| `List(query)` | `ClipboardHistoryQuery { text, kinds, pinnedOnly, limit, newestFirst }`; `text` matches title and preview case- and accent-insensitively (`FoldForClipboardSearch`: Latin, Greek, Cyrillic, ß as ss). Pinned entries come first unless `newestFirst` asks for last use alone: `{kinds = {Image}, newestFirst = true, limit = 1}` is "the last image copied" |
| `Get(id)`, `ReadFormats(id, formats)`, `ReadText(id)` | one entry, its content |
| `GetStats()` | entries, pinned, bytes, encrypted |
| `MarkUsed(id)` | it was put back on the clipboard |
| `SetPinned(id, on)` | |
| `Remove(id)` / `Restore(id)` | hidden at once, gone after `kUndoSeconds` (8) unless restored |
| `Replace(id, snapshot, keepOriginal)` | an edited copy: beside the original, or in its place; returns the entry holding it |
| `Clear(includePinned)` | |
| `GetPolicy()` / `SetPolicy(policy)` | limits, pause, thumbnails, excluded programs |
| `Prune()` | applies the limits and finishes removals (`Record` runs it too) |

`ClipboardHistoryPolicy` holds `maxEntries` (500 unpinned), `maxAgeDays` (30,
by last use), `maxTotalBytes` (512 MB), `maxEntryBytes` (64 MB; a larger copy
is not recorded), `recordingPaused`, `imageThumbnails` and
`excludedApplications` (`Defaults()`: UltraPassword, UltraAuthenticator,
KeePassXC, Bitwarden, 1Password).

`EditClipboardText(text, edit)` is the edit dialog's text tools:
`ClipboardTextEdit::Trim`, `JoinLines`, `Upper`, `Lower`, `Title`,
`Sentence` (case changes for Latin, Greek and Cyrillic).
`ClipboardImageFile(format, extension)` gives an image entry as a file another
program opens (a bare `CF_DIB` gains its BMP file header).

## What a program takes

`PreferredClipboardKinds(categories, mimeTypes)` reads an application's
desktop entry (`UCDesktopEntry::categories` and `mimeTypes`) and answers the
kinds of entry it takes when something is pasted into it, most wanted first:

| The entry says | Kinds |
|---|---|
| `Graphics`, `RasterGraphics`, `2DGraphics`, `VectorGraphics`, `3DGraphics`, `Photography` (not `Viewer`) | Image, Colour |
| `FileManager`, or it opens `inode/directory` | Files |
| `TextEditor`, `IDE`, `Development`, `TerminalEmulator` | Code, Text, Link |
| `WebBrowser` | Link, Text |
| no such category, and it opens only `image/*` types / only `text/*` types | Image, Colour / Text, Code, Link |
| anything else (mail, chat, office, a mix of types) | nothing: the history is offered as it is |

UltraDesktop's quick panel finds the window Super+V was pressed over, its
entry (`UltraCanvasDesktopShell::MatchApplication`) and these kinds, and lists
the newest such entries first under "For <program>", the first one chosen. The
choice stays the person's: they see it before Enter copies it. Swapping what
the clipboard hands a program while it pastes was considered and left out - a
program that prefers images when one is offered would paste an old picture
instead of the text just copied, unannounced.

## Recording

`UltraCanvasClipboardRecorder` records the clipboard from a UI timer:

```cpp
recorder.sourceProvider = []() { return ActiveApplicationName(); };   // optional
recorder.onRecorded = [this](int64_t, ClipboardRecordResult) { RefreshList(); };
recorder.Attach(&history, GetClipboard(), "desktop", 10);
// every 250 ms on the UI thread:
recorder.Tick();
```

Exactly one process records at a time: the holder of the recorder lease
(`AcquireRecorder(holder, priority)`, renewed every two seconds by `Tick`,
lost after `kLeaseSeconds` of silence). The desktop records with priority 10,
UltraClipboard with 0, so the application records only where no desktop does
— on another desktop, on Windows and macOS — and stops when one starts.

On X11 the clipboard goes with the program that owns it. When that program
quits, the recorder puts the last copy it recorded back on the clipboard, so
a copy outlives the program it came from. On X11 the recorder learns of a new
copy from XFixes (`XFixesSelectSelectionInput`, on a connection and a thread
of its own) instead of polling the clipboard's owner.

## Sharing between processes

Every write raises a generation number. A process that shows the history
reads `GetGeneration()` about once a second and reloads when it moved. The
desktop and UltraClipboard work on one history this way, with no other link
between them.

## On disk

| Path | |
|---|---|
| `<data>/ultraos/clipboard/history.db` | SQLite (UltraDatabase, WAL): entries, formats, settings, the lease |
| `<data>/ultraos/clipboard/blobs/ab/<hash>.bin` | one file per payload, named by a keyed hash (HMAC) of its content |
| `<data>/ultraos/clipboard/thumbs/<hash>.png` | image thumbnails |
| `<config>/ultraos/clipboard.key` | the 256-bit key, owner-only |

`<data>` is `$XDG_DATA_HOME` (`~/.local/share`), `%LOCALAPPDATA%` on Windows,
`~/Library/Application Support` on macOS; `<config>` is the folder the
desktop's settings use. The folders are owner-only.

With libsodium, titles, previews and payloads are sealed with
XChaCha20-Poly1305 under the key file, kept apart from the data: a copy of the
history folder, or a backup of it, reads as noise. That does not protect
against a program running as the same user, which can read the clipboard
itself. Thumbnails are not encrypted. A key that no longer opens the history
(deleted, replaced) leaves it unreadable, so `Open` clears it and starts
again. Without libsodium the history is stored as it was copied, and
`GetStats().encrypted` says so.

The key is a file of its own rather than an UltraVault entry: UltraVault
allows one vault per process, and the applications that show the history
already open theirs.

## Showing it in a list

`ClipboardHistoryListModel` and `ClipboardHistoryRowDelegate` put a history
into an `UltraCanvasListView`, in the light style UltraClipboard uses or the
dark compact one of the desktop's panel
(`Docs/Research/UltraClipboard/UltraClipboard-RowAnatomy.svg`):

```cpp
auto listView = std::make_shared<UltraCanvasListView>("history");
auto model = std::make_shared<ClipboardHistoryListModel>();
auto rows  = std::make_shared<ClipboardHistoryRowDelegate>(ClipboardRowStyle::Light());
listView->SetModel(model);
listView->SetDelegate(rows);
listView->SetVariableRowHeights(true);      // headers are lower than rows
model->SetEntries(history.List(), ClipboardHistoryListModel::Sections::ByDay);

// Raw pointers: a lambda stored in the list must not own the list.
listView->onCellHovered = [rows = rows.get(), view = listView.get()](int row, int, const Point2Di& at) {
    if (rows->SetHover(row, at)) view->RequestRedraw();
};
listView->onCellClicked = [rows = rows.get(), model = model.get()](int row, int, const Point2Di& at) {
    const ClipboardHistoryEntry* entry = model->GetEntry(row);
    if (!entry) return;                     // a section header
    switch (rows->ActionAt(row, at)) {
        case ClipboardRowAction::Copy:   Copy(entry->id); break;
        case ClipboardRowAction::Edit:   Edit(entry->id); break;
        case ClipboardRowAction::Delete: Delete(entry->id); break;
        default: break;
    }
};
listView->tooltipProvider = [rows = rows.get()](int row, int) {
    return ClipboardHistoryRowDelegate::ActionTooltip(rows->GetHoverAction(row));
};
```

- **Sections**: `Flat`; `ByDay` (Pinned, Today, Yesterday, Earlier, with
  counts); `PinnedAndRecent` (the panel's Pinned and Recent). A
  `LeadSection { title, count }` puts the first `count` entries under a title
  of the caller's above them (the panel's "For UltraPaint").
  `GetEntry(row)` is `nullptr` on a header; `EntryRowFrom(row, step)` skips
  them for Up / Down; `FindRow(id)` keeps a selection across a reload.
- **The row**: a thumbnail drawn by kind (an image's own thumbnail, a colour
  swatch, a code tile, a link tile, file sheets with a `+n` badge), the
  title, a meta line (`DescribeClipboardEntry`: "Text · 2 lines · 29
  characters · UltraMail · 5 min ago") and Copy, Edit and Delete. The light
  style shows the actions on every row, the compact one on the hovered and
  selected rows only. `ShowCopied(id)` turns Copy into a check with a
  "Copied" pill for a moment; redraw while `IsShowingCopied()`.
- The action glyphs are `media/icons/clipboard/*.svg`, drawn as masks;
  `iconsDir` points elsewhere.
- Helpers: `FormatClipboardAge`, `FormatClipboardSize`,
  `ParseClipboardColour`.

The actions are painted into the row and hit-tested, as UltraMail's contact
list paints its bin, so the keyboard reaches them through the window's keys:
Enter copies, F2 edits, Delete deletes.

## Tests

`Tests/ClipboardHistoryStoreTest.cpp` (headless): kinds and titles, the
encryption on disk, search, pin, remove and undo, edit, the policy's limits,
two instances on one history, the recorder lease, clear, a wrong key, a 16-bit
image's thumbnail, the text tools, and the recorder against a fake backend
(secret copies, putting the last copy back, text that is not an image).

## See also

- [UltraClipboard](../UltraClipboard/README.md) and
  [UltraDesktop](../UltraDesktop/README.md) — the two programs built on it.
- [UltraCanvasDesktopShell](UltraCanvasDesktopShell.md) — `UltraCanvasGlobalShortcut`, the desktop's `Super+V`.
- `UltraCanvasClipboard.h` — the live clipboard, `ClipboardHint::Secret`.
