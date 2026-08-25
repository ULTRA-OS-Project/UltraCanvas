#### 2026-08-25 *0.3.66*
- **Spell checking, and text areas that use it.** The framework had no spell
  checker at all. `UltraCanvasSpellChecker` adds one: a service owning a
  backend, a user dictionary, a session ignore list and a worker thread, so a
  dictionary lookup never happens on the render thread. Backends sit behind the
  UltraCanvas-owned `ISpellCheckBackend` and are picked at runtime — enchant-2
  on Linux, ISpellChecker on Windows 8+, NSSpellChecker on macOS, Hunspell
  everywhere else and as the fallback. All of it is optional: with nothing
  installed the module compiles, reports zero dictionaries and does nothing,
  rather than failing the build.
  `UltraCanvasTextArea::SetSpellCheckEnabled(true)` is the whole integration for
  an application — misspellings get a squiggle, right-click offers suggestions
  plus Add to Dictionary and Ignore, and a chosen suggestion is applied as an
  ordinary undoable edit. One line puts the language menu in a menu bar:
  `UltraCanvasSpellChecker::BuildSpellCheckMenu()` lists only the dictionaries
  actually installed, as a radio group that shows which one is active, and
  rebuilds itself each time it opens so the host never has to.
- **Text areas can say where a character range is on screen.** `TextArea::
  GetCharacterRangeBounds(startByte, byteLength)` maps a byte range of the
  document to the rectangles covering it, accounting for soft wrap, both scroll
  offsets, the line-number gutter, sharded long lines and markdown mode (where
  rendered runs do not match source bytes one-to-one). One rectangle per visual
  line. Nothing equivalent existed, and it is what search-result highlighting,
  inline diff marks, comment anchors and collaborative cursors all need.
  `ReplaceTextRange(startByte, byteLength, text)` is its counterpart, replacing
  a range through the selection and undo machinery so the edit behaves like a
  typed one.

#### 2026-08-24 *0.3.65*
- **The numeric keypad's Enter finishes a text entry.** `UltraCanvasTextInput`
  only ever looked for `UCKeys::Return`, so the keypad's Enter — reported as a
  key code of its own (`UCKeys::NumPadEnter`) by the X11, macOS and WASM
  backends; only Windows folds both onto one code — fell through
  unhandled: the Filer's inline rename accepted the typed name and then did
  nothing on Enter, leaving the file under its old name, and every other field
  that ends on Enter behaved the same way. Both keys now commit (and both
  insert a newline in a multi-line input). The Filer widget itself follows:
  the keypad Enter opens the selected entry and commits the compress dialog,
  like the main one.
- **Selected text is no longer washed out.** The selection band was filled
  *over* the glyphs, so a translucent `selectionColor` faded the text under it
  — most visibly in the Filer's rename editor, which opens with the base name
  selected and showed it in a pale gray. The band is now painted behind the
  text.
- **The Filer's rename editor writes in a dark gray** (`FilerStyle::renameTextColor`,
  `Color(60, 60, 66)`), a step lighter than the near-black of a displayed name,
  so an entry being edited reads as being edited. The caret follows the same
  color.

#### 2026-08-24 *0.3.64*
- **Renaming and deleting a previewed file no longer fail the way moving one
  did.** The move path was taught to let go of its sources first (0.3.63); the
  rename and delete paths were not, and they need the file just as much - a
  rename is refused while another program holds it open, on Windows outright,
  and so is a delete. Rename is the most exposed of the three: the entry stays
  selected for as long as the editor is open, so a host preview pane is
  certainly showing it. Both now drop the entries out of the selection first,
  which closes that preview synchronously, and both put the selection back
  afterwards - rename onto the new name, delete onto the neighbour when
  `SetSelectNextAfterDelete` is on - so the commands that need a single selected
  entry (F2 and the Rename button above all) keep finding one. Only a sole
  selection can be previewed, which is also the only shape the rename path
  produces, so the release is scoped to exactly that case and a multi-selection
  delete is unaffected.
- **"Open with" proposes real applications on Windows and macOS.** The submenu
  knew only what the Linux backend could enumerate; everywhere else it offered
  nothing but "Other application…". Both remaining desktop backends of
  `UltraCanvasFileAssociations` are now implemented, so the menu lists what the
  system actually registers for the selected files — with the default
  application first and each entry's own icon.
  - **Windows:** `SHAssocEnumHandlers(".ext", ASSOC_FILTER_RECOMMENDED)` — the
    very list Explorer shows — with names from `IAssocHandler::GetUIName` and
    the default marked from `AssocQueryString`. Picking one launches it through
    `IAssocHandler::CreateInvoker`/`Invoke` on an `IDataObject` built from the
    whole selection, the same path Explorer takes, so multi-select opens one
    window and per-application quirks stay the shell's problem; a handler that
    refuses the selection gets the files one at a time, and a plain executable
    falls back to a detached `app.exe file…` launch.
  - **macOS:** `-[NSWorkspace URLsForApplicationsToOpenContentType:]` with the
    default from `URLForApplicationToOpenContentType:`, bundle display names,
    and launching via `openURLs:withApplicationAtURL:` (macOS 12+; older
    systems keep the previous default-open-only behaviour).
  - **Application icons** are extracted once and cached as PNG files
    (`%LOCALAPPDATA%\UltraCanvas\openwith-icons`,
    `~/Library/Caches/UltraCanvas/openwith-icons`), keyed by icon source, so a
    menu open stays a cache read and the extraction survives restarts. The
    Windows extraction is the shell icon path the filer already uses for
    `.exe`/`.dll`/`.ico` files, now shared through `OS/MSWindows/UltraCanvasWindowsIcons.h`.
  - Both platforms resolve per file extension and expire their entries after a
    minute, so changing a default association shows up without a restart.
- **The Filer's context menu opens with "Open with", and clicking it opens the
  file.** The submenu moved to the top of the menu — opening a file is what the
  menu is opened for most often — and the entry itself is now an action: it
  opens the whole selection with the OS default application, exactly like a
  double-click, while hovering still opens the application list. "Display"
  moved the other way, down next to "Settings", where the view options belong.
- **A submenu item can carry its own action.** `MenuItemData::onClick` on a
  `Submenu` item was ignored — activating such an entry only opened its child
  list. It now runs the action and closes the menu, and hovering opens the
  child list as before, which is what makes the Filer's "Open with" clickable.
  Submenu items without an `onClick` are unaffected.

#### 2026-08-23 *0.3.63*
- **The Filer notices changes made behind it.** The shown folder was only
  rescanned when the widget itself changed something, so a file another program
  saved into it, a finished download or a deleted file simply did not appear.
  A background worker now re-fingerprints the folder every 1.5 s (its own
  modification time folded together with every entry's name, size and
  modification time) and the widget rescans when the number moves — never on the
  UI thread, and never in the middle of something: an open rename editor, a
  drag, a marquee, a context menu or a file operation waiting on its dialog all
  hold the refresh back until they end. `SetFolderWatchEnabled()` /
  `SetFolderWatchIntervalMs()` configure it.
- **Compress and extract run in the background, with a progress window.** They
  ran on the UI thread, so packing a few hundred megabytes froze the window with
  no sign of what was happening. They now run on a worker behind the new
  `UltraCanvasProgressDialog` — a circular ring (`UltraCanvasCircularProgressChart`
  in SingleRing style) with the percentage in its centre, the file being handled,
  and Cancel. Cancelling a pack deletes the half-written archive; cancelling an
  unpack keeps what it already wrote and stops the rest of a multi-archive run.
- **VirtualFS reports bytes while creating an archive.** `CreateArchive` fired
  its progress callback once per top-level source and never filled in any byte
  count, and `AddDirectory` ignored the callback it was handed — so packing one
  folder produced exactly one progress report with nothing in it. The manager
  now measures the sources up front for `grandTotalBytes` and forwards a
  callback into `AddDirectory`, which reports every file before adding it. That
  is what makes an honest percentage possible; extraction already reported bytes.
- **UltraCanvasTreeView: `SetRootVisible(false)`** hides the root row and draws
  its children as the top level, so a tree can show a forest of sections instead
  of one node with everything under it. The root still owns the nodes and is
  kept expanded; it is never drawn, hit-tested or counted as a row.
- **UltraFiler: "Pinned" sits above "Computer"** in the folder tree, opens
  expanded, and is hidden entirely while nothing is pinned (an empty section is
  a header over nothing).
- **UltraFiler: the sort-direction button shows the direction.** It carried a
  fixed `sort-alpha-down` icon that never changed, so the only way to tell which
  way the list was sorted was the caret in the Details header. It now paints
  `sort-up.svg` while ascending and `sort-down.svg` while descending, repainted
  from the filer's own state - which also covers the direction being changed
  from the context menu, from a column header, or by a folder's stored view.
  The Sort dropdown reads "Date modified" / "Date created" instead of the
  ambiguous "Modified" / "Created".
- **UltraFiler: per-folder display state.** The view type and sort order are
  remembered per folder and restored on entry, so a picture folder can stay on
  large thumbnails by date while a source folder stays on details by name; a
  folder with no stored state keeps whatever the previous one used, which is
  what makes browsing feel continuous. Stored as `folderviews.txt` (the 400 most
  recently entered folders, least recent evicted) and cleared from
  Settings > History & Favorites.

#### 2026-08-23 *0.3.62*
- **The PDF view no longer holds the file it is showing open.** MuPDF opens a
  document as a *stream* (`fz_open_file`) and reads pages from it on demand, so
  `UltraCanvasPDFView` kept an operating system handle on the PDF for as long as
  it was displayed — which is what made moving the previewed file fail on
  Windows, where an open handle refuses a rename. Nothing else in the media
  viewer does this: images are rasterized into a pixmap, and text, spreadsheets,
  3D models and e-books are parsed out of a buffer, all with the file closed
  again. `LoadFromPath()` now reads documents up to `SetMaxInMemoryBytes()`
  (256 MiB by default) into memory through the new
  `IPDFDocument::OpenInMemory()`, so no handle survives the call; a file past
  the limit still streams, because holding hundreds of megabytes of PDF in RAM
  is the worse trade. Measured with `/proc/<pid>/fd` while previewing: the file
  descriptor on the PDF is present when streaming and gone when loaded into
  memory.
- `OpenInMemory()` keeps the real path as the document's source, so `Save()` and
  `GetInfo()` are unaffected; only `SaveIncremental()` is unavailable on a
  memory-opened document (appending to a file needs a document backed by it) and
  now returns `false` instead of writing a broken file. `OpenFromBytes()` no
  longer copies its buffer twice.

#### 2026-08-23 *0.3.61*
- **UltraFiler: dropping a file on a folder moves it, even while it is being
  previewed.** A move is a rename, and a rename is refused while another
  program still holds the file open — on Windows outright. The previewed file
  was exactly that: the media preview keeps the document open (MuPDF for a PDF),
  so dragging the previewed file onto a subfolder failed instead of moving it.
  The Filer now drops the sources out of the selection before it starts a move,
  which fires `onSelectionChanged` synchronously, and
  `UltraCanvasMediaViewer::CloseFile()` makes the preview release the document
  rather than merely stop playback — the file is free by the time the rename
  runs. A move whose rename fails also no longer leaves the entry in two places:
  when the copy + delete fallback cannot remove the original the copy is undone
  and the rename's own error is what gets reported.
- **The "cannot move / copy" dialog shows the whole failure.** It was sized for
  its message alone, so the switches added below it squeezed the text down to a
  sliver and the reason was unreadable — the very thing the dialog exists to
  say. `UltraCanvasModalDialog::AutoSizeToContent()` now counts the elements
  `AddDialogElement()` put in the message column, so every dialog with extra
  controls (the paste conflict dialog too) grows to fit both. The Filer's
  dialog is titled "Cannot Move" / "Cannot Copy" and spells out the operating
  system's reason, the source path, the destination folder and the usual cause.
  Footer buttons are now as wide as their label needs, never narrower than the
  configured width — "Continue" used to reach the user as "Conti…".
- **UltraFiler: Settings > Handling > Drag & Drop.** A new settings page with
  **Drop on folder: Move files / Copy files**, persisted as
  `handling.dragdrop.drop.on.folder` and applied live to every open tab. Ctrl
  at the drop always copies and Shift always moves, whichever way it is set.
  New widget API: `UltraCanvasFilerWidget::SetDropOnFolderCopies()`.
- **UltraFiler: New > Folder, on Ctrl+F.** The file display's *New >* submenu
  opens with **Folder**, above the document kinds and separated from them, and
  Ctrl+F does the same from the keyboard. Both go through the new
  `UltraCanvasFilerWidget::CreateNewFolder()`, which the command bar's "New
  folder" button now uses as well, so all three create the folder, record it in
  the History and open the inline rename editor identically.

#### 2026-08-23 *0.3.60*
- **UCImageRaster::GetMetadataString** reads one embedded metadata field
  (EXIF capture time, camera make/model) by its libvips name, stripping
  the trailing annotation and returning "" when absent. Nothing in
  UltraCanvas exposed EXIF before. Callers must treat "" as *unknown*:
  every photograph in the reference album carried either no EXIF or a
  zeroed timestamp, so UltraCleaner's time gate only applies where both
  pictures actually know when they were taken.
- UltraCleaner now carries its own version, read from
  `Docs/UltraCleaner/CHANGELOG.md`; its entries move there and no longer
  bump the framework.

#### 2026-08-23 *0.3.59*
- **UltraFiler: the folder tree's colours are settings.** The drives in the
  tree — the drive roots on Windows, "File System" and every mounted volume
  under `/media` and `/mnt` elsewhere — now carry a background colour of
  their own, so they read as the section headings they are rather than as
  four more folders. The colour, and the highlight of the selected folder,
  are configured under **Display > Treeview** in the settings window: each
  is a colour box showing the current value that opens the
  `UltraCanvasColorPicker` in a popup window, with the colour previewed live
  in the tree while it is being picked, kept by "Use colour" and put back by
  Cancel. "Restore default colours" returns both. They persist as
  `tree.drive.background.color` / `tree.selected.folder.color` in
  `config.ini`. Drive rows take white text when the chosen background is
  dark, so a deep colour stays readable.
- **Cairo/Pango backend: plain text no longer disappears from a markup
  layout.** `UCTextLayout::SetMarkup` handed the string straight to
  `pango_layout_set_markup`, which rejects the whole thing when it is not
  well-formed markup and leaves the layout EMPTY. Widgets that render user
  text through the markup path — tree node labels among them — therefore
  showed nothing at all for a label containing a bare `&` or `<`; UltraFiler's
  own settings tree had an invisible "History & Favorites" row. The markup is
  now parsed first (`pango_parse_markup`) and a string that is not markup is
  laid out as literal text instead of vanishing.

#### 2026-08-22 *0.3.58*
- **TabbedContainer: the overflow tab list showed only part of the tabs, and
  picking one did nothing.** With more tabs than fit the bar (UltraTexter with 47
  open files), the "▼ 47" dropdown opened a list that ran off the bottom of the
  window: `UltraCanvasAutoComplete` sized its popup as `itemCount * itemHeight`
  with no regard for the window, so everything below the window edge was clipped
  away — unseen, unscrollable and unclickable — and no scrollbar appeared,
  because the ListView believed it was tall enough for all its rows. The popup is
  now clamped to the room actually available (below the field, or above it when
  there is more space there, and kept inside the window horizontally, as the
  Dropdown already did), so the surplus rows are simply scrolled to and every tab
  is reachable. `AutoCompleteStyle::maxVisibleItems` is now an upper bound rather
  than a promise of height.
- Clicking an entry in that list also did nothing at all. The tabbed container
  refills the search list from `Arrange()`, and opening the popup invalidates the
  layout — so on the very next frame `SetItems()` cleared the AutoComplete's
  filtered vector while the ListView kept rendering its 47 rows. Every click then
  resolved against an empty vector and was dropped on the floor by
  `SelectItem()`'s bounds check, which is why the popup just closed and the tab
  bar never moved. `SetItems()` now re-filters instead of clearing when the popup
  is open, so the visible list can never come adrift from the data behind it, and
  the tabbed container leaves the list alone while it is on screen instead of
  rebuilding it (and discarding the user's filter) on every layout pass.
- **FilerWidget: resizing the folder display no longer loses the file you were
  looking at.** Every view reflows when the display area changes size — a
  thumbnail grid re-wraps into a different number of columns, the List view
  re-columns, the treemap is rebuilt — so keeping the pixel scroll offset
  through a resize left the viewport on a completely unrelated part of a big
  folder: dragging the tree | folder split pane, or the UltraFiler's preview
  pane opening or closing next to it, made the selected (previewed) file jump
  off screen. The scroll offset is now re-derived from a reference entry
  instead of kept: the entry the viewport is anchored to is noted before the
  relayout — the selected entry while it is on screen, otherwise the first
  visible one — and put back at the same place in the viewport afterwards,
  with the usual reveal in case the reflow changed its size. This runs inside
  the layout pass for every view type, so any host that resizes the widget
  gets it without calling anything; a relayout at an unchanged size (a rescan,
  a view switch) keeps its own scroll position as before.

#### 2026-08-22 *0.3.57*
- **PDF view: the page inventory is laid out from its width, and the stray
  page badge is gone.** Every thumbnail slot in the strip used to be a fixed
  `thumbHeight` (180 px) tall whatever the page was, so a page drawn to fit the
  strip's width sat in a slot far taller than itself — bands of empty space
  above and below each page, and only a couple of thumbs visible at a time in a
  narrow view (the media viewer / UltraFiler preview). The strip now derives
  everything from its width: the effective strip width (still capped at 1/4 of
  the view) minus `PDFViewStyle::thumbMargin` on both sides is the thumbnail
  width, and each thumbnail is as tall as *its own* page's aspect ratio
  requires, so pages fill their slots exactly and a document mixing page sizes
  gets a correctly-sized thumb for each. `thumbMaxHeight` caps extreme formats
  by narrowing them rather than stretching them. Page numbers — the translucent
  overlay and the caption alike — are sized from the thumbnail they belong to
  (`thumbOverlayNumberHeight`, and the new `thumbLabelHeight` for captions), so
  they shrink with it instead of dwarfing a small page. Thumbnails render at the
  size the layout asks for and re-render when the strip is resized; the page
  aspect ratios are cached per document, so a resize is arithmetic rather than a
  round trip to the engine for every page. The black "N / M" pill floating over
  the top-right of the page is removed: it was not interactive and only repeated
  what the host's status bar (media viewer, UltraFiler) already shows.
  `PDFViewStyle::thumbHeight` is gone, replaced by `thumbMargin` /
  `thumbMaxHeight`. The page's own margin to the edges of the display area
  (`pageMargin`) is halved, 24 px to 12 px, so a fitted page uses the space it
  is given instead of floating in it — most visible on a single-page document,
  where there is no thumbnail strip beside it.
#### 2026-08-22 *0.3.56*
- **New application: UltraCleaner.** Finds and removes the files macOS,
  Windows and Linux leave behind — temporary files, application and browser
  caches, logs, crash reports, thumbnail databases, package-manager
  downloads (npm/Yarn/pip/Gradle/Cargo/Go/Composer/Maven/NuGet), developer
  leftovers (Xcode derived data, simulator and IDE caches) and the trash —
  and shows exactly which paths it proposes to remove before touching
  anything. `Apps/UltraCleaner` builds two targets: `UltraCleanerEngine`, a
  headless static library, and `UltraCleaner`, the GUI on top of it.
  - **The rule table is the whole surface.** Every location the application
    can examine comes from a `CleanRule` in `engine/UltraCleanerRules.cpp`,
    written with tokens (`{HOME}`, `{CACHE}`, `{LOCALAPPDATA}`, `{WINDIR}`,
    …) rather than literal paths, so one row covers all three platforms and
    a root that means nothing on the running system is skipped rather than
    mis-resolved. Reviewing that one file is enough to know what the
    application can touch per OS.
  - **Two checks, not one.** `PathGuard` refuses anything that is not
    strictly inside a resolved rule root, is or contains a protected
    location (the home directory, Documents/Desktop/Downloads/Pictures,
    `.ssh`, `.gnupg`, `.config`, `.local`, `Library`, `AppData`, the
    cloud-sync folders, the OS roots), sits fewer than two levels below the
    filesystem root, resolves through a symlink that escapes its root, or is
    a socket, fifo or device node. It runs during the scan and again during
    the removal, rebuilt from the report's own allowed roots — so a report
    whose items changed in between still cannot reach outside them.
  - **Nothing goes by accident.** Removal defaults to Simulate; the other
    modes are move-to-trash (XDG `.trashinfo` records on Linux, `~/.Trash` on
    macOS, `FOF_ALLOWUNDO` through the shell on Windows) and permanent
    delete, which the GUI confirms and the CLI gates behind `--yes`.
    Categories whose removal costs something unexpected — emptying the
    trash, a Maven repository, old installers in Downloads — arrive
    unticked.
  - **UI.** Toolbar, a category panel of `UltraCanvasCheckbox` (three-state:
    a category is indeterminate when only some of its paths are ticked) plus
    `UltraCanvasBadge` sizes, and an `UltraCanvasTableView` naming every path
    with its size, age and originating rule; double-clicking a row keeps or
    drops that one path. Scanning and cleaning run on a worker thread and
    marshal back through a UI-timer queue, so the window stays responsive and
    Stop works.
  - **Headless too:** `--scan`, `--rules`, `--clean [--trash|--delete --yes]
    [--all]` run the same engine without a display.
  - Engine test suite in `Tests/UltraCleaner` (target
    `UltraCleanerEngineTests`, `-DULTRACANVAS_BUILD_ULTRACLEANER_TESTS=ON`):
    49 tests over the path helpers, the guard, the rule table's structure,
    the scanner and the remover, all driven across temporary trees.
  - Documentation: `Docs/UltraCleaner/README.md`.

#### 2026-08-22 *0.3.55*
- **FilerWidget / UltraFiler: a folder of videos no longer makes a sound
  (Windows).** Opening a folder with video files in it could play a burst of
  a clip's audio, and the preview pane's "5 s clip" video mode ran with sound
  instead of muted. Both came from the same hole in the Media Foundation
  backend: `VideoDecodeOptions::disableAudio` and `SetMute` were realised
  through the renderer's stream volume service, which only exists once the
  Media Session has resolved its topology — so the mute a poster-frame grab
  (the Filer's video thumbnails) or a muted preview applies at open time was
  silently dropped and the streaming audio renderer played at full level.
  `disableAudio` now keeps the audio stream out of the topology altogether —
  no renderer to be heard, and the output device is never opened — matching
  what GStreamer already did, and any volume/mute requested before the
  renderer exists is replayed once it does (`MF_TOPOSTATUS_READY` /
  `MESessionStarted`) instead of being lost. The media viewer also decides the
  preview mute *before* opening the source, so the engine builds a muted
  session rather than muting one already wired for sound, and a finished
  preview clip now stays muted while it sits paused — un-muting at the pause
  could let the sound out when a backend was still deferring that pause behind
  an in-flight seek. The sound comes back on the user's own resume from the
  transport bar. A Media Foundation session now also reads its duration when it
  opens rather than when its topology resolves, so a poster grab asking for the
  frame "10% in" gets it instead of settling for the black first frame.

#### 2026-08-21 *0.3.54*
- **Filer widget: double-click runs applications on POSIX platforms.**
  Activating an executable used to go through the MIME machinery, which
  opens files but never runs them — so native binaries and AppImages did
  nothing on Linux (Windows always worked: ShellExecute's "open" verb
  runs executables). Now `FileAssociations::ClassifyExecutable` sniffs an
  execute-permission file's content — ELF (AppImages included) and
  Mach-O run directly via `LaunchExecutable` (detached, double-fork +
  setsid, the file's folder as working directory); a `#!` script asks
  Run / Open / Cancel first; a file whose execute bit lies (FAT mounts)
  still just opens with its default application. The widget's new
  `OpenEntryWithOS(entry)` bundles these Explorer semantics for hosts
  with their own `onFileActivated`; UltraFiler routes through it.
- **Filer widget: embedded application icons on Windows.** `.exe`, `.dll`
  and `.ico` entries now show the icon embedded in the file — what
  Explorer shows — instead of the generic EXE/DLL glyph, in every view
  from the Details icon column to the largest thumbnail tiles. Extraction
  goes through the shell (`SHDefExtractIconW`, nearest embedded size up
  to 256 px, alpha-masked legacy icons handled) on the existing
  background thumbnail workers, so folders of executables stay smooth;
  files without an icon resource keep their glyph, and the Display >
  Preview switches are not involved — this is an icon, not a content
  preview. New `UltraCanvasNativeFileIcons.h` platform API
  (`NativeFileIconAvailable` / `LoadNativeFileIconPixmap`) with the
  Windows extractor in `OS/MSWindows/UltraCanvasWindowsFileIcons.cpp`
  and no-op stubs elsewhere, so other platforms are unchanged.
- **Filer widget: the remaining "ask the user" gaps.** Every operation that
  silently invented " (2)" names or only logged an error now asks, in the
  same exclusive-switch dialog style. Drag & drop — inside the widget and
  drops arriving from other applications — runs through the paste
  machinery, so taken names raise the paste conflict dialog. An entry that
  *fails* to paste (locked, in use) asks **Try again** / **Skip** with the
  same one-silent-retry-then-ask "for all" semantics as delete. Renaming
  onto an existing name asks **Replace** / **Cancel** instead of refusing
  into the status bar. `ExtractSelection()` with a taken destination
  folder name asks **Keep both** (renamed folder) / **Extract into the
  existing folder** (merge) / **Skip this archive**, with a
  "do this for all remaining archives" switch. The exclusive-switch group
  and the two-choice problem dialog are factored into shared helpers, and
  a cut is now consumed by its own clipboard paste only — a drag-move no
  longer clears an unrelated pending cut.
- **Filer widget: delete problem dialog.** A delete that runs into trouble
  no longer just logs to `onError`: a write-protected (locked) entry asks
  *before* the attempt — **Delete it anyway** (lifting the protection
  first, so it also works on Windows) / **Skip this file**, Skip
  preselected — and a failed delete asks *afterwards* with the failure
  reason ("may be locked or in use by another program") — **Try again** /
  **Skip this file**, Try again preselected. Both flavors carry a
  "Do this for all remaining …" scope switch and Continue / Cancel
  buttons in the same exclusive-switch style as the paste conflict
  dialog; Cancel keeps what was already deleted. A stored
  try-again-for-all grants each later failing entry one silent retry
  before asking again, so nothing can loop forever. Archive-batch
  deletions and the no-dialogs fallback keep the previous behavior, and
  `onFolderModified` now reports only folders that really lost an entry.
- **Filer widget: paste conflict dialog.** Pasting an entry whose name is
  already taken in the target folder no longer silently invents a " (2)"
  name: the paste pauses on a conflict dialog whose choice is set by three
  exclusive switches — **Keep both** (the pasted entry takes the next free
  " (2)" style name; the default), **Replace the existing file**, **Skip
  this file** — plus a **"Do this for all remaining conflicts"** switch that
  decides whether the next conflict asks again (off, the default) or reuses
  the choice. **Continue** proceeds, **Cancel** keeps what was already
  pasted and drops the rest. Copy-pasting a file alongside its original
  never asks (the copy takes the next free name, like Duplicate), and with
  dialogs unavailable every conflict falls back to keep-both — the previous
  fixed behavior. The machinery is public as
  `PasteFilesInto(folder, paths, cut, onDone)` so hosts can aim a paste at
  any folder (UltraFiler's tree context menu now routes through it, gaining
  the dialog too); with `onDone` set the caller owns the post-paste work and
  learns whether anything changed. A folder can no longer be pasted into
  itself from the widget either — previously only app-side paste guarded
  against that.

#### 2026-08-20 *0.3.53*
- **Chart engine: themes and palettes.** The engine grew the theming home the
  proposal reserved (`Engine/UltraCanvasChartTheme.h`): a `ChartTheme` bundles
  the furniture colours (background, plot area, grid, axes, title, legend)
  with a `ChartPalette` of series colours, applied with
  `SetTheme(theme)` / `SetTheme("name")` / `SetPalette(palette)` and — for
  by-name creators — `SetProperty("theme", "Dark")`. Fourteen built-in
  themes, their palettes lifted from the definitions the pre-engine charts
  each carried privately (Light/Bright, Dark, Corporate, Vibrant, Pastel,
  Colorblind, Material, Classic, Tableau, and the Ocean/Sunset/Forest/Slate/
  Monochrome ramps). Palettes answer `ColorAt(index)` (cycling, with wrapped
  cycles re-tinted so long runs never repeat exactly) and
  `ColorAt(index, count)` (ramps spread across their run when the element
  count is known); `ChartPalette::FromColormap(Viridis, n)` samples any
  `UltraCanvasColormap` map into a palette of the requested size. A theme
  change is repaint-only — no layout, no label re-solve — with an
  `OnThemeChanged()` hook for content that caches theme colours (legend
  entries). Pastel carries soft warm-grey furniture of its own, and is the
  Chart Engine demo's default look; the demo gained a Theme row and its bar
  edges follow the bar colour (darkened) instead of a fixed near-black.
  Model-layer tests cover the registry, cycling, count-aware selection and
  colormap sampling.
- **Chart engine: the legend is the shared ChartLegend component.** The
  engine's private right-side-only legend is gone; `SetShowLegend` /
  `SetLegendEntries` now drive the shared component
  (`UltraCanvasChartLegend`), which brings the full option set:
  `SetLegendPosition` with 12 outside placements (Top/Bottom/Left/Right ×
  Start/Center/End, each reserving its edge in the layout negotiation) plus
  4 inset corners that float over the plot (reserving nothing, but riding
  the label plan as an obstacle), `SetLegendOrientation`
  (Auto/Horizontal/Vertical with row wrapping), `SetLegendTitle`, and
  `Legend()` for value text, interval entries, overflow capping and label
  formatting. The shared component gained the engine's paint-mirroring
  swatches (Outline, Hatched, Image, and a real Gradient ramp), so its
  swatch vocabulary now spans Square, Circle, Ring, Line, DashedLine,
  Marker, Glyph, Gradient, Outline, Hatched and Image — and the engine
  legend follows the active chart theme. This also removes the duplicate
  `ChartLegendEntry` type the engine declared in parallel with the shared
  one. New `SetCustomArea(size, draw)` reserves a host-drawn panel below
  the entries for keys richer than any swatch (an annotated
  confidence-ellipse diagram, a bubble-size scale); the demo's Inset mode
  keys the chart's limiter reference lines — a key describes marks the
  chart actually draws. The engine legend is interactive: hover highlights
  an entry, a click toggles it (dimmed) and notifies the chart via
  `OnLegendEntryToggled` — the demo hides the series, stacks re-solving
  without it. Tall vertical legends now wrap into further columns instead
  of silently clipping (a `ChartLegend` fix that also benefits the
  diagrams already using the component). The component grew the engine
  proposal's continuous modes — `SetMode(ColorBar)` draws a colormap ramp
  over a value range with tick labels (optionally quantized into bands),
  `SetMode(SizeLegend)` draws sample circles keying a bubble-size scale —
  and the legacy charts' private legends (polar, circular progress,
  funnel, pyramid, Mekko, dumbbell, cumulative flow, population, nested
  area, arc diagram, and the contour surfaces' colour bars) were migrated
  onto the shared component, retiring their incompatible position enums'
  private implementations while keeping every public API working.
- **Chart engine: the highlight layer (proposal §8.2).** `AddHighlight` /
  `ClearHighlights` bring group washes to slot 200 (under the grid) and
  overlays to slot 700: explicit rectangles/ellipses in value space, value
  bands, and the computed shapes — **confidence ellipses** (covariance eigen
  decomposition at 50%/95% with a mean marker, the group-of-dots scatter
  convention), padded convex hulls, Chaikin-smoothed blobs and point halos.
  Highlight captions ride the label plan as `HighlightLabel`. The geometry
  (`ComputeConfidenceEllipse`, hull, expand, smooth) is UI-free in
  `Engine/UltraCanvasChartHighlights` and unit-tested. The demo gained a
  Highlight row (Band / Ellipse / Blob); the ellipse style brings the
  legend's 50%/95% ellipse key with it, since a key describes marks the
  chart actually draws.
- **Chart engine: value labels survive the axis maximum.** The label solver
  was clamped to the plot area, so a bar reaching the axis limit had its
  value label pushed down onto the bar. The solver's bounds now also include
  the margins the chart content reserved for itself in `MeasureContent`
  (`SolveLabelBounds`) - the spill band above the bars (or right of them
  under the horizontal projection) - so the label sits just above the plot
  edge instead. Axis bands, the title band and the legend margin remain out
  of bounds.

- **FilerWidget / UltraFiler: the context menu's Extract got the same dialog
  as Compress.** Extract used to unpack immediately with no way to pick the
  destination; it now opens the compress dialog's panel in extract mode: the
  archive's file-type icon with its name (or "N archives") beneath, an
  editable destination **folder** name — suggested from the archive's name
  without its suffix, so `sources.tar.gz` offers `sources` — and the
  location line. The icon can be dragged onto any folder in the view to
  retarget the destination, Enter / Extract unpacks, Esc / Cancel dismisses,
  and an existing folder name gets the usual " (2)" suffix instead of being
  written into. Several selected archives each unpack into their own
  subfolder of the named folder so their contents cannot collide. Unpacking
  still runs through the VirtualFS bridge (`UCVFSBridge::ExtractArchive`);
  the dialog-free `ExtractSelection()` remains for programmatic use, and the
  new `OpenExtractDialog()` is public for hosts.
- **Audio player: a finished track no longer restarts itself.** When a
  non-looping source played to its end, the output device kept pulling frames
  and the playback cursor had been reset to 0 — so the track audibly started
  over from the beginning while the player reported *Stopped* and the
  transport showed the Play icon. This is what made the UltraFiler preview
  with auto-play look like "it plays but never shows the Pause icon": the
  first pass played with the correct Pause icon, then looped forever in the
  stopped state. `UltraCanvasAudioPlayer` now feeds silence after end of
  stream until the next transport call, `onEnded` fires exactly once, and
  `UltraCanvasAudioPlayerElement` stops the device when it learns the track
  ended. `Play()` after the end still restarts from 0:00.
- **Audio player element: UI work moved off the audio thread.** The backend
  delivers position updates, end-of-stream and the resulting state change on
  its audio thread, and the element used to update its label, sliders and
  icons directly from those callbacks — racing the UI thread's layout,
  text-measurement and dirty-rectangle bookkeeping (reproducibly crashing in
  pango under load, and losing repaints such as the play/pause icon refresh).
  All player callbacks are now marshalled to the UI thread
  (`PostToUIThread`), the transport icons also re-sync from the UI-thread
  position timer, and the element disconnects its callbacks on destruction.
- **Audio player element: narrow hosts no longer clip the volume slider.**
  All controls had fixed widths, so in a narrow host (the UltraFiler preview
  pane goes down to ~260px) the flex row overflowed: the seek bar collapsed
  to zero and the volume slider ran off the pane's right edge (the bug
  report's screenshot). The row is now responsive — when the width cannot
  fit everything beside a usable seek bar it hides the volume slider first,
  then the time label; the mute button stays so the sound can still be
  silenced, and everything returns as soon as the element is wide enough.

#### 2026-08-17 *0.3.52*
- **FilerWidget: balanced line breaks for wrapped tile captions.** A name that
  needs two or three caption lines was broken greedily — the first line took
  everything that fit and the rest became a stub, "CoderBox compiler" /
  ".png". A name that fits its lines completely is now re-broken at the
  smallest line width that still needs no extra line, so the lines come out
  near equal: "CoderBox" / "compiler.png", "Diagram" / "Wordcloud.png". The
  line count — and with it the caption band and tile height — never changes,
  and names too long even for `captionMaxLines` keep the greedy break with the
  leading-"…" last line, whose every line is full anyway. Applies to the
  thumbnail grids and the treemap in the FilerWidget and everything built on
  it (UltraFiler, file dialogs).
- **UltraFiler: Pin / Unpin with state flags in the context menus, and
  "Open prompt" is back.** The filer context menus' Extras submenu ends with
  an app-provided block (the FilerWidget's new `extrasMenuProvider` hook):
  **Open prompt** — which had lost its home when the menu bar was dropped —
  then **Pin** and **Unpin** submenus, each with "To Treeview" /
  "To Favorites" entries acting on the current selection (or the shown folder
  while nothing is selected). The entries are check items whose flag shows
  whether the selection is pinned there right now — Pin is enabled while
  something is still unpinned, Unpin while something is pinned. The folder
  tree's context menu gets the same **Pin** submenu between the file commands
  and Unpin; there the "To Treeview" / "To Favorites" flags directly toggle
  the folder's pin in the tree's Pinned section / the Favorites view's
  Folders tab. All menus build their items when they open, so the flags
  always reflect the current pin state.

#### 2026-08-17 *0.3.51*
- **FilerWidget / UltraFiler: empty displays say so.** A folder with no
  content used to show only a small "(empty folder)" line; it now draws a
  vertically centered notice — an attention icon (a vector-drawn warning
  triangle, no icon assets involved) with **"Folder is empty!"** beneath it.
  An empty file-list display shows **"No entries"** the same way, which is
  what the UltraFiler's History and Favorites tabs show before anything was
  recorded or pinned (and a search without matches). A widget that never had
  a folder set keeps the plain "(no folder)" text.

#### 2026-08-13 *0.3.50*
- **PDFView / UltraFiler preview: five fixes to the PDF page view and its
  thumbnail strip ("page inventory").** **Stale thumbnails** — opening another
  PDF kept showing the previous file's thumbnails, because the thumbnail cache
  deliberately survives zoom changes and the document switch reused that same
  invalidation. `SetDocument` and every page-mutating operation
  (delete/move/insert/merge/replace-text/redact) now drop the thumbnail cache
  too (`InvalidateAllCaches()`), while zoom/resize keep it as before.
  **Single-page documents show no strip** — a one-page PDF needs no page
  inventory, so the strip only appears for documents with more than one page.
  **Wheel-scrolling reads through the document, with hard limits** — before,
  the view scrolled the one page endlessly into empty space and never
  advanced. Scrolling now stops once the page edge sits a page-margin inside
  the viewport; from that resting point the next wheel step continues at the
  top of the next page (and up past the top edge, at the bottom of the
  previous page), while on the last/first page the margin is the end of the
  line. Pages open at their top instead of vertically centered, the strip
  auto-scrolls so the current page's thumbnail stays visible, and its own
  scrolling is clamped to its content. **Page area at least 3× the strip** —
  the strip's effective width is capped at 1/4 of the view, so a narrow
  preview pane can no longer end up mostly inventory with a tiny page.
  **"Over the page" numbering in the viewer** — the MediaViewer (UltraFiler's
  preview pane) now uses `ThumbnailNumberStyle::Overlay`, the large translucent
  number over the thumbnail page, instead of the caption beneath; slot heights
  no longer reserve the caption row in that style.

#### 2026-08-12 *0.3.49*
- **FilerWidget / UltraFiler: hidden files are now the platform's notion, not
  just dot names.** "Hidden" was tested as `name[0] == '.'` everywhere, a test
  that never fires on Windows — so a profile folder listed the `NTUSER.DAT`
  registry hives, `AppData` and the localized hidden compatibility junctions
  (`Anwendungsdaten`, `Lokale Einstellungen`, `Startmenü`, …) that Explorer
  never shows, and on macOS `~/Library` was visible. The widget's scans now
  read the Windows `FILE_ATTRIBUTE_HIDDEN` attribute and the macOS `UF_HIDDEN`
  file flag inside the one metadata call each entry already paid for (on
  Windows via `GetFileAttributesExW`, which returns attributes, size and times
  together — replacing `::stat`, which cannot see attribute bits), so scan
  cost is unchanged. The UltraFiler's folder tree and recursive search use the
  same test through the new `UltraCanvas::IsHiddenFileSystemEntry(path)`
  (`UltraCanvasUtils.h`).
- **UltraFiler: the Home tree section is curated like the Explorer / Finder
  sidebars.** Expanding Home now leads with the user's well-known folders —
  Desktop, Documents, Downloads, Music, Pictures, Videos (plus Public /
  Templates where the OS defines them) — each with its own icon, resolved
  through the new `UltraCanvas::GetWellKnownUserFolders()`:
  `SHGetKnownFolderPath` on Windows (follows folder redirection, e.g. a
  Documents folder moved into OneDrive), the fixed home subfolders on macOS,
  and `xdg-user-dirs` on Linux (localized folder names; entries pointing at
  `$HOME` are disabled per the spec). The remaining visible home folders
  follow alphabetically, with a well-known folder that physically sits in the
  home folder not listed twice; the Home node itself now wears the home icon.
- **Changelog: resolved the duplicate *0.3.44* version number** left by the
  Breadcrumb merge — its entry (the newer of the two) is now *0.3.48*, so the
  first-line version the build derives moves forward again instead of
  regressing below the *0.3.47* beneath it.

#### 2026-08-11 *0.3.48*
- **Breadcrumb**: four fixes to the per-item dropdowns, all visible in the
  Filer's and the Media Viewer's path strip. **An empty list gets no
  control** — a folder with no sub-folders now shows no dropdown chevron
  and reserves no click area instead of opening a menu that only says
  "(no sub-folders)". Lazily filled dropdowns answer the question through
  the new `BreadcrumbItem::dropdownAvailableProvider`, a cheap "is there a
  first entry?" probe cached per item
  (`UltraCanvasBreadcrumb::RefreshDropdownAvailability()` clears it);
  `hasDropdown` with nothing behind it no longer draws a chevron either.
  **Hover no longer hides the label**: the current item keeps its emphasis
  text colour through hover and press, and its feedback background is
  derived from `currentItemBackgroundColor` (tinted towards the other end
  of the luminance scale) instead of the generic hover colour, which turned
  the Filer's blue current segment pale while its label stayed white. New
  `currentItemHoverBackgroundColor` / `currentItemPressedBackgroundColor`
  override the derived colours, and `minTextContrastRatio` (2.2 by default,
  0 disables) redraws a label black or white when it cannot be read against
  its own opaque background. The `Steps` preset's hover/press label colours
  and the `Parallelogram` preset's current-item label were unreadable on
  their own backgrounds and were fixed at the source. **Dropdown entries
  sort alphabetically** (case-insensitive, by the displayed name — sorting
  full paths put every capitalised folder ahead of every lower-case one),
  opt-in per item via `BreadcrumbItem::sortDropdownItems`. **The dropdown
  click area is a full-height zone**, at least
  `BreadcrumbStyle::dropdownHitAreaMinWidth` (24px) wide, covering the
  chevron, the gap in front of it and the item's trailing padding — and, in
  the `Arrow` / `Parallelogram` styles, extending over the tip drawn past
  the segment's right edge, so the whole arrow head opens the menu. It
  never takes more than the trailing half of an item, so the label keeps a
  clickable area of its own.
#### 2026-08-11 *0.3.47*
- **macOS: a classic USB mouse wheel is responsive again.** `UCEvent::wheelDelta`
  is an integer notch count — the X11 backend emits ±1 per button-4/5 press, the
  Win32 one divides `WM_MOUSEWHEEL` by `WHEEL_DELTA` and guards the result
  against rounding to zero — but the macOS backend assigned
  `NSEvent.scrollingDeltaY` straight into it. macOS applies scroll acceleration
  to a classic wheel and reports it in *lines*, so a single slow notch arrives
  as a fraction (~0.1) and truncated to 0, while a trackpad or Magic Mouse
  reports *points*, tens per gesture, and always survived the truncation. Worse
  than losing the notch: a zero delta is not "no scroll" to widgets, most of
  which read `wheelDelta > 0 ? up : down`, so it landed in the down branch — in
  the 3D scatter / contour charts the wheel zoomed *out* whichever way it was
  turned. Wheel events now round to a notch, never report a real notch as zero,
  and are not delivered at all when there is no vertical movement (which also
  stops AppKit's zero-delta gesture / momentum phase events, and horizontal
  swipes, from registering as scrolls down). Trackpad scroll distances are
  unchanged.

#### 2026-08-11 *0.3.46*
- **macOS: double-click now works at all.** The Cocoa event conversion only ever
  produced `MouseDown` / `MouseUp`, so `UCEventType::MouseDoubleClick` was never
  raised on macOS and every handler waiting for it was dead code — double-clicking
  a folder or file in the Filer did nothing, and the same held for each of the
  ~37 double-click handlers across the framework. A mouse-down now consults
  AppKit's `NSEvent.clickCount` (which already honours the double-click interval
  from System Settings) and the doubled press is delivered as `MouseDoubleClick`
  *instead of* `MouseDown`, matching the X11 and Win32 backends — the first click
  selects, the second activates — with pairs counted (2, 4, 6 …) so a triple
  click's third press is an ordinary `MouseDown` there too. The unused
  hand-rolled click-tracking state (`MouseClickInfo`, `IsDoubleClick`,
  `UpdateLastClick` — declared, never defined or called) is gone with it.

#### 2026-08-11 *0.3.45*
- **macOS: frames rendered without a Cocoa event now reach the screen.** The
  content view is layer-backed, so `setNeedsDisplay:` only queued a layer
  display: `drawRect:` (the blit of the Cairo surface) and the CoreAnimation
  commit that puts it on screen both happened at the end of *AppKit's* event
  cycle, which this framework does not run — its loop blocks in
  `CFRunLoopRunInMode(..., returnAfterSourceHandled: true)` and returns as soon
  as the cross-thread wake-up source is handled, before the run-loop observers
  AppKit relies on fire. Any repaint not provoked by an input event therefore
  stayed invisible until the next mouse move: opening a folder in one of the
  Filer's thumbnail views showed no thumbnails at all until the cursor was
  moved, and the same applied to every other `PostToUIThread` result (video
  poster frames, network completions) and to timer-driven repaints.
  `InvalidateWindowNative()` now draws the view immediately and
  `UltraCanvasMacOSApplication::RunInEventLoop()` flushes the CoreAnimation
  transaction once per main-loop iteration, outside any AppKit display
  callback. Linux and Windows were unaffected — their surfaces present on
  flush.

#### 2026-08-11 *0.3.44*
- **UltraCanvasAlbum** *(1.7.0)*: video tiles now make their own covers.
  A `Video` item whose `thumbnailPath` is empty — or points at an image that
  does not decode — has one representative frame extracted from its clip on a
  background worker (`AlbumConfig::videoPosterFrames`, on by default, with
  `videoPosterMaxSize` / `videoPosterTimeSec`), cached in memory by media path
  and repainted in place, reflowing the aspect-driven layouts around the real
  frame. Nothing is written to disk, which is what fixes macOS: the previous
  approach cached poster files next to the clips, impossible inside a
  code-signed `.app` bundle (and equally in an AppImage or any read-only
  install), so every video tile in the demo's album fell back to the
  play-triangle placeholder. An explicit cover that decodes still wins, work is
  queued only by tiles that actually draw, and with no video backend (or an
  undecodable clip) the slot fails once and the placeholder stays. The DemoApp
  album example (2.18.0) dropped its `SaveVideoThumbnail` pre-pass, which also
  removes a synchronous decode per clip from building the page.

#### 2026-08-09 *0.3.43*
- **UltraSocial** *(Phase 3)*: the Tier-3 networks and media for Tier 2.
  **LinkedIn connector** — OAuth2 code flow for the user's own
  confidential-client app (secret in the form body, no PKCE; redirect
  port 17997), member id via OpenID `userinfo`, text posts through the
  versioned `POST /rest/posts` (post URN read from the `x-restli-id`
  response header), token refresh when the app has it granted.
  **Facebook Pages connector** — pasted Page id + long-lived Page access
  token (personal profiles have no posting API); text to `/{page}/feed`,
  one photo + caption to `/{page}/photos` as multipart (no public URL
  needed); Meta's `{"error":{...}}` shape added to the shared error
  surface. **X media** — images upload via the v2 media endpoint and
  attach as `media_ids` (4 × ≤5 MB), inside the same refresh-retry as
  text. **Telegram albums** — 2–10 photos via `sendMediaGroup`
  (`attach://` multipart, caption on the first). Wizard forms for both
  new networks. 10 new engine tests (47 total).

#### 2026-08-09 *0.3.42*
- **UltraSocial** *(Phase 2)*: the "automatically" part plus the Tier-2
  networks. **Scheduling outbox** — "Post later…" opens a date + time
  dialog and queues one outbox row per selected account (UltraDatabase,
  raw draft stored so adaptation happens at send time); a scheduler timer
  flushes due entries through the same `UltraSocialPublisher` path as
  "Post now", with bounded retries on linear backoff (5 attempts,
  +5 min × attempt) before a failed history row; queued posts show as
  closable chips (with retry count) and go out at next launch when they
  came due while the app was closed. **Reddit connector** — OAuth2
  "installed app" code flow built from the UltraNetOAuth2 blocks (Reddit
  has no PKCE; token exchange authenticates HTTP Basic `clientid:` with
  an empty password), self posts via `/api/submit` with the draft's first
  line as the title, hourly-token refresh on 401. **X connector** — OAuth2
  code + PKCE public client via `UltraNet_OAuth2AuthorizeInteractive`,
  text tweets via `POST /2/tweets`, rotating refresh tokens persisted
  back to the vault. Both are bring-your-own-client-id (fixed loopback
  redirect ports 17995/17996); wizard forms added. Capabilities now
  express "media not supported" (`maxImages == 0`) — the composer drops
  attachments for such networks with a warning. 11 new engine tests
  (37 total).

#### 2026-08-09 *0.3.41*
- **UltraSocial** *(Phase 1 UI)*: the GUI application (target `UltraSocial`)
  on top of the engine — compose window with per-account target checkboxes
  and live character counters (per network's limit, switching to the caption
  limit when media is attached; amber badge + "will be shortened" warning
  when over), media chips through the file picker, add-account wizard
  (network picker with per-network fields and hints; Mastodon's browser
  OAuth or pasted token, Bluesky app password, Telegram bot token), post
  reporting per target, and the recent-history strip. Sign-in and publishing
  run on worker threads; results marshal to the UI through a main-thread
  timer queue, so the window stays live during the OAuth browser consent
  and slow uploads.

#### 2026-08-09 *0.3.40*
- **UltraSocial** *(new, Phase 1 engine)*: the cross-posting app's headless
  engine (`Apps/UltraSocial/engine/`, target `UltraSocialEngine`) —
  compose-once → adapt-per-network composer (code-point counting,
  word-boundary truncation with ellipsis, media trimming, caption limits),
  per-account credential vault (UltraMail's file-backend pattern), account +
  post-history store on UltraDatabase, and three connectors behind
  `ISocialConnector`: **Mastodon** (dynamic OAuth client registration +
  UltraNetOAuth2 interactive flow or pasted token; multipart media upload
  with 202-processing poll; statuses with `Idempotency-Key`), **Bluesky**
  (app-password session, `uploadBlob` + `app.bsky.feed.post` records,
  transparent `ExpiredToken` refresh that hands the rewritten credential
  blob back for the vault), **Telegram** (Bot API; `sendMessage` /
  `sendPhoto` with caption; `t.me` permalinks). 26 engine tests against
  scripted loopback HTTP fakes (`ULTRACANVAS_BUILD_ULTRASOCIAL_TESTS`).
  Design: `Docs/UltraSocial/Concept.md`.

#### 2026-08-09 *0.3.39*
- **UltraNet**: new OAuth 2.0 helper (`UltraNet/UltraNetOAuth2.h`) — the
  authorization-code flow with PKCE (RFC 6749 + 7636) for native apps:
  `UltraNet_OAuth2GeneratePkce` / `UltraNet_OAuth2ChallengeFromVerifier`
  (S256, verified against the RFC 7636 test vector),
  `UltraNet_OAuth2BuildAuthUrl`, a loopback redirect listener
  (`UltraNet_OAuth2WaitForCallback`, RFC 8252 style — binds 127.0.0.1/::1
  only, answers stray requests with 404 and keeps waiting),
  `UltraNet_OAuth2ExchangeCode` / `UltraNet_OAuth2Refresh` (client secret via
  HTTP Basic or form body; server `error`/`error_description` surfaced in the
  result), `UltraNet_OAuth2ParseTokenResponse`, and the one-call blocking
  orchestrator `UltraNet_OAuth2AuthorizeInteractive`, which also resolves a
  port-0 redirect URI to the ephemeral port actually bound. SHA-256 is
  self-contained in the module, so no TLS-library crypto dependency.
- **UltraNet** sockets: `UltraNetSocketOptions.bindAddress` restricts
  listeners / UDP binds to one interface (e.g. loopback),
  `UltraNet_TcpAccept` takes an optional timeout, and the new
  `UltraNet_SocketLocalEndpoint` reports the bound address/port — together
  they let a port-0 listener discover its ephemeral port.
#### 2026-08-10 *0.3.39*
- **UltraCanvasAlbum** *(1.6.1)*: a hover video preview no longer plays
  alongside the full video opened from its tile. Clicking a video tile (or its
  Play action) opens the player in its own window while the cursor still rests
  on the tile, so the album never received a MouseLeave and the muted inline
  preview kept decoding behind the player — two videos at once. Activating a
  tile (click, double-click, action icon, context-menu action) now stops the
  running preview before the app callback fires, and hover previews (video and
  GIF/WebP animation alike) only run while the album's window is the
  application's focused window — a preview that is mid-playback when another
  window takes the focus stops on its next frame tick, and a mouse move over
  the now-background album no longer re-arms one.
- **UltraCanvasFilerWidget**: flexible tile widths in the thumbnail grid
  views. The column count still comes from the selected tile edge, but the
  leftover strip on the right — too narrow for one more column — is now
  distributed across the row's cells (Explorer-style), so the grid always
  fills the widget's width: resizing the window stretches the cells smoothly
  until the next column fits instead of growing an empty gap. Only the cell
  widens — captions get the extra room, so long names wrap later — while the
  image box keeps the square Small / Medium / Big / Maximized edge, centered,
  so thumbnails keep their size during a resize and the async decode cache is
  not churned. Controlled by `SetFlexibleTileWidths(bool)` (default on;
  off restores the fixed-width grid).

#### 2026-08-10 *0.3.38*
- **UltraFiler**: Favorites (pinning). A new heart button next to the History
  clock shows the Favorites view — the same Files / Folders / Apps tabbed
  layout, but listing deliberately pinned paths (`UltraFilerFavorites`,
  persisted as `favorites.txt` next to the settings) instead of recently used
  ones. The new menu bar **Pin** menu pins the visible view's selection (or
  the shown folder when nothing is selected): **Pin ▸ Favorites** into the
  tab the entry's kind belongs to, **Pin ▸ Treeview** — enabled only while
  the selection is a folder — into the folder tree's new **Pinned** section,
  whose entries navigate like bookmarks. The folder tree gained a context
  menu: **Copy / Delete / Paste** act on the folder under the cursor (Paste
  only when a folder is under the cursor and the clipboard holds files,
  Delete with confirmation and never on the top-level roots), **Unpin**
  (pinned entries only) removes the bookmark without touching the folder.
  *Settings ▸ Clear Favorites* empties the pins; Esc leaves the Favorites
  view like it leaves the History view.
- **UltraCanvasTreeView**: `onNodeRightClicked` now fires only for the right
  mouse button (it used to fire on every mouse-up over a node) and passes the
  `UCEvent` along so handlers can place a context menu at the pointer; a
  right press no longer moves the selection to the node under the cursor.

#### 2026-08-09 *0.3.37*
- **UltraNet**: new `UltraNetApiStatus` tool (`Tests/UltraNet/ApiStatus/`,
  target `UltraNetApiStatus`, enabled by `ULTRACANVAS_BUILD_NET_TESTS`) walks
  the whole public UltraNet surface and reports each entry as **WORKING**
  (the probe drove the real code path and the result matched the contract),
  **IMPLEMENTED** (present and reached, but unverifiable in this
  environment), **NOT IMPLEMENTED** (documented stub / no-op / absent
  backend) or **BROKEN** (ran and contradicted the API). 108 entries across
  Core, URL, HTTP, Session, SSE, WebSocket, DNS, Socket, TLS, FTP, MIME and
  Plugins; `--format=text|markdown|json`, `--area=`, `--output=`,
  `--network`, `--strict`, and a `--serve` diagnostic that just holds the
  probe origin open. Registered with CTest; exits non-zero only on BROKEN
  (or, with `--strict`, on anything short of WORKING).
- **UltraNet**: the status tool verifies offline by bringing its own peers —
  an in-process HTTP/1.1 + RFC 6455 WebSocket origin written on UltraNet's
  own TCP API (keep-alive, chunked bodies, `Expect: 100-continue`,
  redirects, cookies, Basic-auth challenges, `text/event-stream`, a slow
  route for cancellation, and a masked-frame echo endpoint with its own
  SHA-1 for `Sec-WebSocket-Accept`), loopback TCP/UDP peers, and an
  `openssl s_server` TLS peer whose throwaway certificate makes
  `UltraNet_TlsSetCABundle` / `UltraNet_TlsAddTrustedCert` checkable in both
  directions. No Python, no external service and no internet access
  required.
- Docs: `Docs/Modules/UltraNet/ApiStatus.md` documents the statuses, the
  options, how each area is verified and how to add a probe; both UltraNet
  READMEs point at it from their Status sections.
- **UltraNet**: the macOS TLS backend now honours custom trust anchors —
  found by the status tool's first CI run, whose trust-store probes came back
  BROKEN on macOS. `OS/MacOS/UltraNetTlsImpl.mm` stored the global CA bundle
  and `UltraNet_TlsAddTrustedCert` PEMs but `VerifyPeer` evaluated the peer
  against the system keychain only, so `UltraNet_TlsSetCABundle` /
  `UltraNet_TlsAddTrustedCert` (and the per-wrap
  `UltraNetTlsOptions::caBundlePath`, equally unread) were silently ignored.
  Wrap now parses the resolved PEMs into `SecCertificateRef` anchors and
  `VerifyPeer` applies them via `SecTrustSetAnchorCertificates`; a CA bundle
  replaces the system roots (matching the OpenSSL backend's
  `SSL_CTX_load_verify_locations` semantics) while added PEMs alone extend
  them (`SecTrustSetAnchorCertificatesOnly(false)`).

#### 2026-08-09 *0.3.36*
- **UltraCanvasFilerWidget** *(1.13.0)*: the compress dialog keeps the whole
  name and stays editable. The suggested archive name was `stem()` of the
  entry, which strips everything after the last dot — for a folder named
  `UCDemo-Windows-0.3.27-x86_64` that left `UCDemo-Windows-0.3`, because
  `.27-x86_64` looks like an extension to `std::filesystem`. A folder now keeps
  its full name (a folder has no extension, so every dot in it belongs to the
  name), and a file only loses a tail that is a plausible file type: short,
  alphanumeric and not a pure number, with `.tar` dropped along with the
  `.gz` / `.bz2` / `.xz` / `.zst` of a compound suffix. `CompressSelection()`
  picks the same name.
  The name field itself was a hand-rolled buffer fed by the dialog's own key
  handler: it could only append and backspace at the end (no caret, no
  selection, no clipboard — nothing could be corrected in the middle), and
  because it read the keyboard through the widget's own focus it went silent
  the moment anything else in the window claimed the focus, and stayed silent
  for every dialog opened afterwards. It is now a real `UltraCanvasTextInput`
  child, the same component the inline rename editor uses, opened with the
  suggestion selected so typing replaces it; the dialog additionally installs a
  window `KeyDown` filter for as long as it is up, so a keystroke reaches the
  editor whoever the window currently considers focused. Closing the dialog
  removes the filter and hands the keyboard back to the folder display. The
  committed name is stripped of path separators and trimmed before it becomes
  a file name.
- **UltraCanvasFilerWidget** *(1.13.0)*: the compress dialog's Compress /
  Cancel are `UltraCanvasButton` children now, replacing a private
  `DrawDialogButton` painter and its own `okHover` / `cancelHover` flags and
  hit-testing. They carry the framework's hover, press and disabled painting,
  and a click reaches them as elements instead of being pattern-matched
  against rectangles in the dialog's mouse handler.
- **UltraCanvasTimePicker** *(1.2.0)*: the editable field is a real
  `UltraCanvasTextInput` child (the picker is an `UltraCanvasContainer` now,
  like every other composite widget), replacing a private `editBuffer` /
  `caretPos` / `editing` triple and ~110 lines of hand-written key handling.
  Typing a time gains selection, clipboard, undo, double-click word select and
  multi-byte input, all of which the hand-rolled editor lacked; the popup
  spinners, the clock dial, the wheel-over-field nudge, Up/Down to open, Enter
  to commit and Escape to revert keep working as before. Two behaviours had to
  be re-pointed at the new field: the spinner and dial paths wrote `value`
  directly and so left the text stale, and the wheel guard tested
  `IsHovered()`, which the editor now absorbs by being the element under the
  pointer.
- **UltraCanvasSpreadsheet** *(1.2.0)*: the cell / formula-bar editor is a real
  `UltraCanvasTextInput` child (the widget is an `UltraCanvasContainer` now),
  replacing an `editBuffer_` / `editCursorPos_` pair and its own key handling.
  One editor moves between the active cell and the formula bar depending on the
  edit mode. Cell text gains selection, clipboard, undo and multi-byte input;
  typing to start an edit, Enter to commit and step down, Tab to commit and step
  right, Escape to discard and formula entry all behave as before. `editBuffer_`
  survives as a mirror of the editor's text, so the live formula preview and the
  formula bar read it unchanged.
  With this the UI-reuse baseline is **empty**: every control in the tree is
  built from an UltraCanvas element.
- **UltraCanvasDatePicker**, **UltraCanvasColorPicker** *(1.3.0)*: their typed
  fields are real `UltraCanvasTextInput` children too, on the same pattern as
  the time picker — both widgets derive from `UltraCanvasContainer` now. The
  date field keeps its calendar popup, arrow-key month navigation (the popup
  takes the keyboard off the field so the arrows drive dates, and hands it back
  on close) and is read-only in the range / week / multiple modes, whose text
  is a computed summary. The colour picker moves one editor between its hex box
  and the channel boxes, keeps the per-field character filter — now applied to
  paste as well as typing — and gains selection, clipboard and undo it never
  had. Clicking a value box selects its contents (type to replace) instead of
  placing a caret mid-value; a second click inside the editor places the caret
  as usual. `DragTarget::TextDrag` is gone: dragging out a selection is the
  editor's own gesture.
- **UltraCanvasContainer** *(4.2.0)*: new `PlaceChildAt(child, rect)` for a
  self-rendered widget that positions a child itself (an inline editor over a
  field or a cell). `SetBounds()` alone is not enough — it writes only
  `finalBounds`, which the next layout pass overwrites, and
  `UltraCanvasTextInput::Arrange()` re-clamps its horizontal scroll whenever
  its width changes, so an editor placed that way ended up showing the tail of
  its own value ("F" instead of "#85FFFBFF"). `PlaceChildAt` writes the CSS
  position and size the engine resolves from, so the rectangle survives
  Arrange. The Filer's compress dialog uses it too.
- **UltraCanvasFilerWidget**: fixed a build break in `ScanFolder()` — a merge
  kept the rename-reveal block that reads `renamedTo` but dropped the lines
  that declare it (and map the selection from the old path to the new one), so
  the file did not compile and the renamed entry lost its selection.
- **Docs**: new `Docs/UltraCanvas/UltraCanvasUIElements.md` — a catalogue of
  every UI element the framework ships, grouped by what you are trying to
  build, with each element's defining header. The corpus had ~150 per-component
  documents and no index, so an element could only be found by someone who
  already knew its name; that is why controls kept being painted by hand
  instead of instantiated. AGENTS.md now carries the rule ("if it takes input,
  shows a picture or presents a value, it is an element") and points at the
  catalogue.
- **Tooling**: new `scripts/check_ui_reuse.py` plus a `UI element reuse` CI
  workflow. It reports the two shapes that are almost always a reinvented
  element — a private edit buffer and caret fed from a `KeyDown` handler with
  no `UltraCanvasTextInput` in the file, and a
  `Draw*Button(IRenderContext*, …, bool hovered)` painter. The six controls
  that already existed (`UltraCanvasColorPicker`, `UltraCanvasDatePicker`,
  `UltraCanvasTimePicker` and `UltraCanvasSpreadsheet` edit fields, the Filer's
  `DrawDialogButton`) were recorded in `scripts/ui_reuse_baseline.txt` so they
  would not fail the build while only new ones did — and were then all ported
  in this same release, leaving the baseline empty. Self-rendered views that legitimately
  paint their own content opt out with a `// ui-reuse-exempt: <reason>` marker.
- **UltraCanvasFilerWidget** *(1.13.0)*: content previews are now **selectable
  per file kind**. The context menu grew a `Display > Preview` submenu with a
  checkbox for each of Bitmaps, Vector graphics, 3D, PDF, Text, Docs,
  Spreadsheets and Videos — all enabled by default — mirrored in code by
  `SetPreviewType()` / `SetPreviewTypes(mask)` / `IsPreviewTypeEnabled()` /
  `GetPreviewTypes()` over the new `FilerPreviewType` bitmask. Switching a kind
  off drops its entries back to the plain type glyph immediately *and* stops
  the widget from opening those files at all, which is what makes a folder of
  huge photos, videos or PDFs on a slow volume browsable.
  Three kinds gained a real preview producer, all running on the existing
  viewport-driven thumbnail workers so no preview ever blocks a frame:
  **PDF** files render their first page through the PDF plugin (outlined as a
  sheet of paper, since a page is white on a white widget), **STL** models are
  rasterized in software as a shaded three-quarter view (the GL viewer needs a
  window and a current context, which a background decode has neither of), and
  **text, documents and spreadsheets** preview as a miniature page of their own
  content — plain text and source code read directly, HTML stripped of its
  tags, RTF of its control words, ODT / DOC / DOCX through the shared
  rich-document reader, and ODS / XLSX / CSV / TSV laid out as a cell grid.
  Page-shaped previews are only drawn from roughly a 40 px box up, so the icon
  column of a Details or List row keeps its glyph and a folder listing does not
  read every document in it. `FilerFileCategory` gained `Model3D` and the type
  map learned the common 3D extensions (stl, obj, ply, 3ds, 3mf, gltf, glb,
  dae, fbx) plus `tsv`, so those files sort and colour as models / text instead
  of "File".

#### 2026-08-08 *0.3.35*
- Change Linux packager script, drop .appimage
- Change GitHub build to produce package with all dependent libs

#### 2026-08-08 *0.3.34*
- **UltraCanvasFilerWidget** *(1.12.0)*: a dragged file can leave the widget
  again. The drag was handed to the native OS drag the moment the cursor
  crossed the widget's border, and that is where it visibly died: the badge is
  drawn by the widget and therefore clipped to it, the OS drag draws nothing of
  its own while the cursor is still over the application's own window (XDND
  refuses a drop back onto the window that started it), and on Windows and
  macOS `StartNativeFileDrag()` had no implementation at all, so the gesture
  was dropped on the floor. Crossing the border now keeps the drag running: the
  badge travels over the whole window on the new window drag overlay, a release
  over another element hands it the files as a `Drop` event (a second Filer
  pane, a folder tree, any drop-aware widget), and only leaving the *window*
  turns the set into the native OS drag. A platform without one keeps the
  window-wide drag alive instead of losing the gesture.
- **UltraCanvasWindowBase** *(2.2.0)*: new `SetDragOverlay(owner, windowRect,
  renderer)` / `ClearDragOverlay(owner)` — window-level content painted above
  every element, for widgets that drag something across the whole window and
  cannot paint outside their own bounds. Moving it repaints the rectangle it
  leaves and the one it enters; the first owner keeps it until it clears it.
- **Windows backend**: native file drags out of a window are implemented
  (`UltraCanvasWindowsWindow::StartNativeFileDrag`) with a CF_HDROP
  `IDataObject` plus an `IDropSource` driven by `DoDragDrop`, the counterpart of
  the `IDropTarget` that was already there. Files can now be dragged from a
  Filer widget into Explorer or any other application, and the accepted effect
  (copy / move) is reported back so a move rescans the source folder. macOS
  still has no drag-and-drop backend in either direction.
- **UCTextLayout** *(1.1.2)*: a `TextWrap::WrapNone` layout no longer wraps onto
  a second line when it is also given an explicit height. Pango has no "never
  wrap" flag — a no-wrap layout is one that Pango is told to ellipsize, and the
  layout *height* decides when that kicks in: `-1` (the default) means "ellipsize
  the first line of each paragraph", but a positive height means "ellipsize once
  that many pixels are used up", so a box two lines tall lets the text word-wrap
  once before anything is ellipsized. Widgets set an explicit height purely to
  centre the glyphs vertically (`VerticalAlignment::Middle`), which silently
  turned single-line text into two lines whenever the box was at least twice the
  line height. `SetExplicitHeight` now keeps that value for the vertical
  alignment maths only and leaves Pango's own height at `-1` while the layout is
  in no-wrap mode; `SetWrap` re-applies it, since it may be called after the
  height is set. `GetExplicitHeight` reports the requested height rather than
  Pango's.
- **UltraCanvasBreadcrumb / UltraCanvasLabel**: fixed as a result — the filer's
  and media viewer's path strips kept long folder names such as
  `UCDemo-Windows-0.3.24-x86_64 (1)` on one ellipsized line instead of breaking
  them at the space and drawing two cramped lines inside a one-line strip.
  Whether the break happened depended on the exact font line height against the
  strip's height, which is why it showed on Windows and not on Linux. Long names
  are still capped at `BreadcrumbStyle::maxItemTextWidth` (200px by default; set
  it to `0` for no per-item limit).

#### 2026-08-08 *0.3.33*
- **UltraFiler — Extras > Open prompt**: a new menu bar entry starts the
  operating system's command line program in the folder of the active tab.
  The launch lives in `Apps/UltraFiler/UltraFilerPrompt` *(1.0.0)*, which
  detaches the process (`fork` + `setsid` + `execvp` behind a reaped
  intermediate child on POSIX, `ShellExecuteExW` on Windows), so closing the
  file manager never takes the terminal with it and no zombie is left behind.
  Without configuration the platform default is detected at run time:
  `%COMSPEC%` on Windows, Terminal.app (started with `open -a <bundle>
  <folder>`) on macOS, `$TERMINAL` or the first installed terminal emulator on
  Linux; when nothing is found the failure is reported in an alert instead of
  silently doing nothing.
- **UltraFiler — settings**: the settings window gained an *Extras > Open
  prompt* page holding the application that menu entry starts
  (`extras.prompt.application` in the config file). The folder button next to
  the path field opens the file dialog filtered to this platform's
  applications, **Save app** persists the chosen program, **Use system
  default** clears the setting again and **Test** starts the program in the
  field to check the path. The dialog's buttons now come from one
  `MakeButton` helper instead of per-button styling.
- **UltraCanvasCircleDiagram** *(1.0.0)*: new hub-and-spoke circle diagram
  infographic — a centre hub, a backbone ring, and equally sized labelled node
  discs threaded onto that ring, each with a fan of satellites on leader lines.
  It is the node-on-ring member of the circular family: every existing circular
  element subdivides the ring into sectors, while this one threads discrete
  discs onto it, so a node's radius is independent of ring thickness and it can
  carry children outside the ring. Presentation-only (no viewport, dragging,
  inline editing or undo); interaction is hover highlighting, tooltips and
  `onNodeClick` / `onSatelliteClick`. Structure and colour are independent
  presets — `CircleDiagramDesign` (`SatelliteWheel`, `BandedWheel`, `Custom`)
  and `CircleDiagramPaletteKind` (seven themes plus `Custom`) — and both work
  at any node count, because each palette is a hue ramp sampled at N points
  rather than a fixed list. Every layout quantity derives from the per-node arc
  of 360/N: the auto-fitted node radius is a share of the chord between
  neighbours, the satellite fan is narrowed to what one node's wedge can hold
  (shrinking auto-sized satellite discs when K of them will not fit side by
  side), and anything outside the backbone — fans, or labels placed with
  `CircleNodeLabelPlacement::Outside` — is reserved for by shrinking the
  backbone radius so nothing is clipped. Disc labels shrink to fit, testing the
  longest single word as well as the wrapped block, since a word too wide to
  break ellipsizes rather than wrapping. Node discs are all one radius and
  satellites another; `value` is tooltip/callback payload and never scales a
  disc. `SetNodeCount()` clamps to 3–12 rather than degrading silently. Docs in
  `Docs/UltraCanvas/UltraCanvasCircleDiagram.md`, survey and roadmap in
  `Docs/UltraCanvas/CircleDiagramInfographicVariants.md`, demo scene in
  `Apps/DemoApp/UltraCanvasCircleDiagramExamples.cpp`.

#### 2026-08-08 *0.3.32*
- **Version numbers** are now derived from the changelogs at build time, so the
  version the demo app's info window shows can no longer disagree with the
  version in the file name of the build it came from. The packaging scripts
  already parsed `#### YYYY-MM-DD *x.y.z*` off the first changelog line for the
  artefact names; the number compiled *into* the binaries was a separate
  hand-maintained copy that only moved when someone remembered to run
  `set-version.sh`, and it had fallen ten releases behind (the info window
  reported 0.3.21 against a 0.3.31 changelog). The new
  `cmake/UltraCanvasVersion.cmake` reads the same first line at configure time
  and feeds `project(VERSION)`, `ULTRACANVAS_VERSION` and `ULTRATEXTER_VERSION`;
  `UltraCanvas::versionString` and `UltraCanvasTextEditor::version` take their
  value from those defines instead of a literal. Adding a changelog entry
  re-triggers the configure step, so an existing build tree picks the new
  version up rather than baking in the one it was first configured with.
  `set-version.sh` now only writes the two Windows resource files that are read
  from disk by windres (`UltraTexter.rc`, `UltraTexter.manifest`) — a configure
  on any platform warns when those are stale.

#### 2026-08-07 *0.3.31*
- **UltraCanvasGLSurface** *(1.0.1)*: a surface built with a non-zero `(x, y)`
  now keeps that origin. The constructor delegated to the size-only base
  constructor, so the origin was written straight to `finalBounds` without a CSS
  position behind it: the first layout pass re-stacked the surface as an in-flow
  child and the GL content composited in the top-left corner of its container
  while every sibling widget stayed put. It now uses the `(id, x, y, w, h)` base
  constructor, which stamps an AbsoluteUI position for a non-zero origin;
  `(0, 0)` still leaves the surface in flow for flex/grid parents. The doc gained
  a "Positioning" section covering this and the bounds + CSS-box pattern needed
  to move or resize a surface at runtime.
- **DemoApp — OpenGL 3D showcase**: all three tabs (3D Models, Shaders, Zarch)
  share one maximize control, `gldemo::AddMaximizeControl()`. The icon sits in
  the canvas's top-right corner with an 8px margin, and the button, a
  double-click on the canvas or Esc toggles between the normal layout and a
  canvas maximized over the whole tab. Maximizing works now that the zoom writes
  the CSS box as well as the bounds (`gldemo::PlaceElement`) — hiding the chrome
  invalidated the layout, which promptly restored the canvas's original size. A
  new `media/icons/minimise.svg` marks the restore state, the Zarch and Models
  panels match the Shaders tab's column geometry, and the Shaders info text was
  trimmed to what fits its panel.

#### 2026-08-07 *0.3.30*
- **UltraCanvasTooltipManager** *(2.3.0)*: structured tooltips gained
  three-column table rows and definable column alignment.
  `AddRow(label, value, value2)` (and the swatch overload
  `AddRow(color, label, value, value2)`) adds a three-column row next to the
  existing two-column form. Alignment is set per column and separately for
  each arity — `TooltipColumnAlign::Left | Center | Right` via
  `TooltipStyle::columnAlign2` / `columnAlign3` (theme level) or
  `TooltipContent::SetColumnAlignment(...)` (per tooltip, stored outside
  `styleOverride` so `SetStyle()` does not reset it); the defaults reproduce
  the previous look of labels left, values flush right. Two- and three-column
  rows are measured as independent tables, so a two-column "Total" row cannot
  disturb a three-column table above it. Column widths are natural when the
  row fits and otherwise cap the label column at 55 % and share the rest in
  proportion to the natural widths; surplus width is spread over the gaps so
  the last column stays flush with the tooltip's right edge.
- **UltraCanvasTooltipManager**: `TooltipColumnAlign::Decimal` aligns a column
  of numbers on their decimal separator instead of on an edge. A cell's anchor
  is the last `.` or `,` followed by a digit, so a thousands separator never
  wins over the real decimal mark (`1,204.50` and `1.204,50` both resolve) and
  a trailing period cannot steal it; a cell with no separator anchors at its
  end, so integers meet the separator column. The column reserves the widest
  integer part plus the widest fractional part — wider than any single cell —
  so nothing is clipped to stay aligned, and a cell that still does not fit
  falls back to right alignment. Demo tiles cover the three-column table,
  custom alignment, mixed row arity and decimal alignment; the "Title + table"
  tile is now a label / value / unit table with capitalized labels.

#### 2026-08-07 *0.3.29*
- **UltraCanvasSequenceDiagram** *(1.0.0)*: new UML 2 sequence diagram
  feature, layered like the class diagram. `UltraCanvasSequenceModel` is the
  UI-free interaction model — lifelines (object / actor / boundary / control /
  entity / database heads), the seven message forms (sync, async, return,
  create, destroy, lost, found, plus self messages), combined fragments
  (loop / alt / opt / par / break / critical) with guarded operands, and
  anchored notes. Execution bars are computed from the message order
  (`ComputeActivations`), message numbers sequentially or hierarchically
  Visual-Paradigm style (`ComputeMessageNumbers`), and `Validate()` diagnoses
  dangling endpoints, lifecycle misuse, unmatched returns and fragments that
  cross without nesting. `SequenceTextExport` writes PlantUML and Mermaid,
  including activations, fragments, notes and inline create declarations.
  The rendering element solves lifeline spacing from head and label widths,
  attaches arrows to the deepest execution bar, drops created heads onto
  their create row, and offers six themes, zoom/pan/fit, hover + selection
  callbacks, an optional "sd" frame and foot boxes. Six sample models —
  among them the framework's own event pipeline — drive the new DemoApp tab;
  unit tests in `Tests/SequenceModelTest.cpp` (target `SequenceModelTest`).

#### 2026-08-06 *0.3.28*
- **UltraCanvasFilerWidget** *(1.9.0)*: the inline rename editor is now a real
  `UltraCanvasTextInput` overlaid on the item's name instead of a hand-drawn
  append-only field. Typing, caret movement (arrows / Home / End),
  click-to-position, Shift selection and Ctrl+C/X/V/Z all work; the editor
  opens with the base name selected (extension kept, Explorer-style; folders
  select the whole name), commits on Enter and on focus loss (a click
  anywhere else), cancels on Esc. Renaming a search-result entry now renames
  in the entry's own folder instead of resolving against the shown path.
- **UltraCanvasFilerWidget**: rubber-band selection. Dragging from empty
  space draws a selection rectangle; every entry it touches becomes the
  selection, live while the band is dragged, and with Ctrl the rectangle
  adds to the selection held at the press. The band auto-scrolls at the
  viewport edge and Escape abandons it (restoring the previous selection);
  `WantsEscapeKey()` covers it. A plain click on empty space still clears
  the selection (a Ctrl click leaves it alone).
- **UltraCanvasFilerWidget**: video files show their poster frame (grabbed
  via `CaptureVideoThumbnailPixmap` a short way into the clip) as their
  thumbnail in the thumbnail views, the Details/List mini icons, the drag
  badge and the delete-confirmation preview. Decoded on the existing
  background thumbnail workers, so the folder page never waits on a video;
  without a video backend the tile keeps its generic glyph.
- **UltraCanvasTextInput** *(1.3.3)*: typed UTF-8 input is inserted instead
  of dropped — the printable filter on `event.text` kept only ASCII 32..126,
  so multi-byte characters (umlauts, accents, CJK, ...) typed into any text
  field vanished.
- **UltraViewer app** *(1.0.0)*: new universal media viewer application
  (`Apps/UltraViewer`, target `UltraViewer`, `BUILD_ULTRAVIEWER_APP`, default
  ON). One full-window `UltraCanvasMediaViewer` displays bitmaps, vector
  graphics, video and audio (with the player elements' transport controls:
  play / pause / seek / scrub / volume), documents (PDF), e-books,
  spreadsheets (ODS/CSV/TSV), 3D models (STL), text / source / markdown and
  UltraCanvas Document containers (*.ucd). Command line takes a folder (browse
  it), a file (browse its folder with the file shown first), several files
  (exactly that playlist) or nothing (use Open / drag & drop). New app icon
  `media/appicon/UltraViewer.png`.
- **UltraCanvasMediaViewer** *(1.4.0)*: two new media kinds.
  `MediaKind::Book` — e-books (EPUB / FB2 / MOBI / PRC / AZW / AZW3) open in
  an embedded `UltraCanvasEBookViewer` (chapter toolbar, TOC, reflowing
  content); the zoom toolbar drives the reading text scale and PageUp /
  PageDown switch chapters while Left / Right keep browsing the folder.
  `MediaKind::UCDoc` — UltraCanvas Document containers (*.ucd) are recognised
  by the UCD v2 fixed header: the viewer shows the embedded raw HEIC/PNG
  preview thumbnail (readable without parsing the body, as the format
  intends) on the image surface, or a header summary in the text view when
  there is none; the info bar labels the file `UC DOCUMENT` and the details
  popup lists the container fields (type descriptor, version, body encoding,
  compression, encryption, thumbnail). Full rendering arrives with the UCD
  v2 engine. New doc: `Docs/UltraCanvas/UltraCanvasMediaViewer.md`.

#### 2026-08-06 *0.3.27*
- **UltraCanvasMediaViewer** *(1.3.1)*: the `Still` video preview mode shows
  the first frame instead of staying on "Buffering...". The mode relied on the
  load-time preroll frame alone, whose single emission could be flushed away
  while the pipeline was still settling; the `Still` paths now request the
  frame explicitly (a paused seek to 0 re-prerolls and delivers it — the same
  mechanism as a paused scrub) whenever no frame of the current file has been
  shown yet.
- **UltraCanvasVideoPlayerElement** *(0.1.7)*: loading a source resets the
  per-file frame state (shown frame, scrub throttle, time readout), so
  switching files updates the preview instead of keeping the previous video's
  last frame. New `HasVideoFrame()` reports whether a frame of the current
  source has been shown yet.
- **UltraCanvasVideoPlayer** *(0.1.2)* / **GStreamer backend** *(0.1.11)*: a
  freshly opened session no longer receives a spurious playback-rate "change"
  to the default 1.0. Backends apply a rate through a flushing seek, and
  issuing one during the initial preroll discarded the prerolled first frame a
  paused session depends on; the GStreamer backend also skips the seek for any
  unchanged rate (as the MediaFoundation backend already did for 1x).

#### 2026-08-05 *0.3.26*
- **UltraCanvasMediaViewer** *(1.3.0)*: configurable backdrop behind
  transparent images. New `TransparentImageBackground` enum
  (`SolidColor` / `Checkered`) with `SetTransparentBackground()` /
  `GetTransparentBackground()` and `SetTransparentColor()` /
  `GetTransparentColor()` on both the surface and the viewer. The backdrop is
  drawn under the image's displayed rectangle (only its visible part, so a
  high zoom costs nothing extra) — a preset solid colour (default white) or
  the light/dark checkerboard familiar from image editors — and fades with
  the image during slideshow transitions. Transparent pixels now read against
  a defined background instead of the dark canvas colour.
- **UltraFiler app** *(1.3.0)*: menu bar with a *Settings* menu opening the
  new settings window: a tree of settings pages on the left (main pages with
  sub pages — currently *Media Viewer > Transparent Images*) and the selected
  page on the right. The Transparent Images page chooses between the
  checkered pattern and a preset colour (colour picker) for the backdrop
  behind transparent images in the media preview; changes apply live and
  persist to the platform config directory
  (`~/.config/UltraFiler/config.ini` on Linux).
- **UltraCanvasMediaViewer**: new `SetTopBarsVisible()` / `GetTopBarsVisible()`
  — shows/hides everything above the display surface (the folder breadcrumb,
  both toolbar rows and the adjustments panel). For hosts that embed the
  viewer as a plain preview pane and provide their own navigation. Default:
  visible.
- **UltraCanvasTreeView**: new `SetFontSize()` / `GetFontSize()` for the row
  label font size (default 12, previously hardcoded).
  `UltraCanvasColumnsTreeView` uses it for its cell text, column headers and
  group headers too.
- **UltraCanvasFilerWidget**: new `FilerStyle::folderIconScale` (default 1.0)
  — shrinks the folder glyph inside a thumbnail tile's image box, centered,
  so folders can read lighter next to image thumbnails.
- **UltraCanvasFilerWidget**: file-list display for search results. New
  `ShowFileList(paths)` / `IsShowingFileList()` shows an explicit list of
  paths (stat-ed like scanned entries) instead of the folder listing, in the
  current view mode; `SetPath()` returns to the folder display. The Details
  view gains a `Path` column (the entry's containing folder) shown only in
  that mode, so normal folder displays are unchanged. The Open-Path context
  item now sits at the *top* of the menu followed by a separator, and
  `SetOpenPathMenuItemVisible(visible, label)` takes a caption.
- **UltraFiler app** *(1.2.0)*: search field on the right of the path bar —
  searches the current folder recursively for names containing the text
  (case-insensitive, capped at 1000 matches) and shows the matches in the
  tab's current view mode, with the *Path* column after the name in Details
  view and "Open path (in new tab)" as the context menu's first entry.
  Clearing the field or navigating returns to the folder display; each tab
  keeps its own search. Every UI element now uses a single 9 pt font size
  (toolbar, breadcrumb, dropdowns, tabs, folder tree, file panel, status
  bar). The preview pane hides the media viewer's breadcrumb and toolbars —
  the filer provides the navigation, the pane shows only the media. Folder
  icons in the thumbnail views draw at 70% size.

#### 2026-08-04 *0.3.25*
- **UltraCanvasFilerWidget** *(1.7.0)*: entries are properly draggable. A press
  on an item captures the mouse and, past the slop threshold, picks up that
  item — or the whole selection when the press landed inside it. Inside the
  widget the drag is drawn by the widget (a badge with the entry icon and name
  / "N items" following the cursor, the folder under it highlighted) and a drop
  on a folder of the view **moves** the files into it, **Ctrl** drops a copy;
  Escape abandons the drag. Leaving the widget hands the same set to the native
  OS drag as before — which is also what fixes dragging files out: the capture
  makes a fast flick out of the widget start the drag instead of losing the
  move to whatever the cursor passed over. New `SetDragEnabled()` /
  `IsDragEnabled()` turn the gesture off.
- **UltraCanvasFilerWidget**: a drag no longer changes the selection. What a
  plain press would select is applied on the release, so dragging a file does
  not fire `onSelectionChanged` and no longer re-targets a preview pane fed by
  it (UltraFiler loaded the dragged file into the preview mid-drag).
- **UltraCanvasFilerWidget**: the selection now survives a rescan — it is
  remembered by path instead of by row index, so `Refresh()` after a file
  operation no longer leaves the selection pointing at whatever moved into
  those indices. New `onFolderRefreshed` callback fires after every scan of the
  shown folder, so hosts can refresh their folder description.
- **UltraCanvasMediaViewer**: SVG files preview as the rendered image instead
  of their source code. `ClassifyFile()` matched the syntax tokenizer's SVG /
  XPM / XBM languages before the image formats, so markup-based image files
  opened in the read-only text area. New `IsImageFile()` is checked first.
- **UltraFiler app**: the status bar follows the folder listing again — it is
  refreshed from `onFolderRefreshed`, so item counts stay correct after a drop
  or another file operation rescans the view.
- Merge "UltraFiler font size standardization"
- Add UltraFiler app icon

#### 2026-08-02 *0.3.24*
- **UltraCanvasMediaViewer**: selectable video preview behavior. New
  `VideoPreviewMode` (`Autoplay` — full playback with sound, the previous and
  default behavior; `PreviewClip` — the first seconds muted and then paused,
  the `UltraCanvasAlbum` hover-preview style; `Still` — the prerolled first
  frame, paused) with `SetVideoPreviewMode()` / `GetVideoPreviewMode()` and
  `SetVideoPreviewClipSeconds()` (default 5 s). Changing the mode applies to a
  currently shown video too. New `StopPlayback()` stops video / audio playback
  and the pending clip timer — for hosts that hide or detach the viewer, where
  the sound used to keep playing invisibly.
- **UltraCanvasSupportedFormats**: new `CanImagePipelineLoad(extension)` —
  whether the raster/SVG image pipeline behind `UCImage` (libvips + the
  built-in SVG renderer, including the ImageMagick delegate fallback for known
  raster extensions) can decode files with that extension. The existing
  inventory reports plugin-provided formats (CDR, XAR, ...) as loadable, so it
  could not answer "may this file go to the image loader?".
- **UltraCanvasFilerWidget**: thumbnail decoding is now gated on
  `CanImagePipelineLoad`. Vector-category files that only a graphics plugin
  understands (e.g. `.xar`, `.cdr`) were handed to the libvips loader by both
  the thumbnail worker and the delete-confirmation folder preview (which fed it
  *every* file type), producing `VipsForeignLoad "... is not a known file
  format"` warnings and wasted decode attempts; such files now keep their
  category glyph.
- **UltraFiler app** *(1.1.0)*: tabbed browsing — a "+" button on the left of
  the toolbar opens additional tabs, each with its own folder view, history
  and sort/view settings (drag to reorder, closable except the last). The
  preview pane now folds away while nothing previewable is selected, giving
  the folder display the whole width; the Preview toggle enables / disables
  the feature. New "Video" dropdown selecting the preview behavior for video
  files: Autoplay (default), 5 s muted clip, or still image.

#### 2026-08-01 *0.3.23*
- **UltraCanvasDendrogram** *(1.5.1)*: fixed radial leaf labels rendering upside
  down in the upper-right quadrant. The left/right half test was
  `angle > -pi/2 && angle < pi/2`, which assumes angles in `(-pi, pi]`, but
  `DendrogramLayoutEngine::ApplyRadialLayout` produces `[0, 2pi)` — so every leaf
  between `3pi/2` and `2pi` failed the test, took the left-half branch and picked
  up an extra 180-degree rotation. Both the leaf labels and the group arc labels
  now test `cos(rotation)`, which is precisely the condition for "this text is not
  upside down" and does not depend on which angle range the layout uses. Audited
  the other radial components at the same time — Sunburst, Chord, RadialBar,
  CircularInfoGraphic and PolarChart all normalise correctly and were unaffected.

#### 2026-08-01 *0.3.22*
- **UltraCanvasNodeDiagram** *(2.2.0)*: organizational-network features. Node
  size can now be driven by the data - `NodeSizeMode::ByDegree` sizes a node
  from its connection count, `ByValue` from a new `NodeDiagramNode::value`
  field, both through a sqrt transfer so node AREA tracks the quantity rather
  than the diameter. Degrees can count total / incoming / outgoing links and are
  cached, so bulk-loading a graph costs one rebuild instead of one per
  `AddLink`. New `NodeDiagramGroup` cluster containers wrap a set of member
  nodes in an auto-fitted boundary box (solid or dashed, optional fill and
  corner radius, title in any corner) that follows its members through dragging
  and re-layout; boxes draw behind the links and titles after the nodes, both
  sized in screen pixels so they hold up at any zoom. `SetGroupCohesion()` adds
  a per-group centroid attraction to the force-directed layout - without it
  repulsion scatters a cluster and the boxes overlap into mush. New color legend
  overlay (`NodeDiagramLegendConfig`, `BuildLegendFromGroups()`) drawn in screen
  space in any corner. Groups and sizing round-trip through `ToJson()` /
  `FromJson()`, and group boxes are included in `ComputeContentBounds()` so
  `FitView()` and the minimap account for them. New demo tab (Diagrams > Node
  Diagram > Organization): a five-department company network with a control
  column for layout, sizing mode, degree mode, cohesion, link style, node shape,
  theme, and toggles for the boxes, legend, grid, minimap, controls and snap.
- **UltraCanvasDendrogram** *(1.5.0)*: hierarchical edge bundling (Holten 2006).
  `AddRelation()` registers a leaf-to-leaf association that does not follow the
  tree's parent/child structure; each one is routed through the tree path source
  -> lowest common ancestor -> target, relaxed toward the straight chord by
  `SetBundlingStrength()` (beta), and smoothed with a clamped cubic B-spline. A
  radial dendrogram can now show its hierarchy and the cross-links between its
  leaves at the same time without becoming a hairball. Also new: area-proportional
  node dots via `DendrogramNodeSizeMode::ByValue` and `DendrogramNode::nodeValue`,
  normalised against the largest value in the tree - replacing the single global
  `style.leafNodeRadius` as the only leaf size available.
- **UltraCanvasJitterPlotElement** *(1.3.0)*: per-point encodings. A parallel
  vector of size magnitudes plus `JitterPointSizeMode::ByValue` turns a beeswarm
  into a bubble beeswarm, with the packer receiving the real per-point radii so
  mixed sizes pack without overlapping. A parallel vector of color values plus
  `JitterPointColorMode::ByValue` samples any `UltraCanvasColormap` palette,
  including the diverging ones with a configurable midpoint, so a signed quantity
  reads correctly around zero. Fixes: `minScoreFilter` defaulted to `0.0` and
  silently discarded every negative value before rendering; `AddCategoryData()`
  did not invalidate the point-position cache, so a second call left the previous
  points on screen; `RenderJitterPoints()` always drew a plain circle and ignored
  both `SetPointShape()` and the point edge style.
- **UltraCanvasMediaViewer**: the arrow keys now browse the folder as soon as
  the widget is on screen. The widget takes the window keyboard focus when it is
  attached to a window (`SetGrabFocusOnAttach(false)` opts out,
  `FocusForKeyboard()` requests it on demand) and installs a window key filter,
  so Left / Right no longer require a click into the picture first — with no
  focused element at all the key event never reached the widget before. The
  filter only steps in when the keyboard is unowned or held by one of the
  display views; toolbar buttons, sliders and the breadcrumb keep their own key
  handling. Where the active view uses the bare arrows itself (spreadsheet cell
  movement) `Alt+Left` / `Alt+Right` browse instead.
- **UltraCanvasMediaViewer**: text / source / markdown files open display-only
  instead of read-only-but-focusable. The text area used to grab the focus and
  swallow Left / Right for caret movement, which stopped file browsing dead
  (and showed an editing caret in a viewer). The viewer now scrolls the text
  itself with Up / Down / PageUp / PageDown and copies the selection with
  Ctrl+C.
- **UltraCanvasMediaViewer**: opening a single file through the Open dialog, or
  dropping one file onto the widget, now browses the folder that file lives in
  (with that file shown first) instead of building a one-entry playlist that the
  arrow keys and the slideshow had nowhere to move in. Multi-selections are
  still taken as an explicit playlist.
- **UltraCanvasTextArea**: new `SetDisplayOnly()` / `IsDisplayOnly()`. Display-
  only implies read-only and additionally takes the area out of the keyboard
  focus chain — `AcceptsFocus()` returns false, no caret is drawn and no key
  event reaches it — so a hosting widget keeps the arrow keys for its own
  navigation. Mouse wheel / scrollbar scrolling and mouse selection are
  unaffected.
  
#### 2026-08-01 *0.3.21*
- **UltraCanvasSplitPane**: split lines can now carry an optional **handle**.
  `SplitterHandleShape` picks the form - `Square`, `RoundedSquare`, `Round`
  (a circle when square, a capsule when elongated) or `Image` - and
  `SplitterHandleStyle` sets its size across and along the line, corner radius,
  position along the line (0..1), colors, border and grip lines. The splitter
  strip widens to fit the handle while the painted line stays as thin as
  `splitterThickness`, so a 3 px line can carry a 22 px handle.
  `SetSplitterHandleShape()` is the one-liner; the handle drags exactly like the
  rest of the line. The shape enumerator for "no handle" is `NoHandle`, not
  `None`, because `<X11/X.h>` defines `None` as a macro.
- **UltraCanvasSplitPane**: **image handles**. `SplitterHandleStyle::imagePath`
  takes an SVG or raster asset, with `SetSplitterHandleImage()` as the
  one-liner. With `SplitterHandleShape::Image` the asset is the whole handle;
  with any drawn shape it is centred on top of it and the grip is suppressed.
  Leaving `axisLength` at 0 takes the handle's proportions from the asset, so
  the ready-made `media/icons/scrollbar-handle-v.svg` (14x48) renders as a grip
  rather than a squashed square. `imageAsMask` re-tints a monochrome glyph with
  `imageColor`, or with the handle's own normal/hover/active color when that is
  left transparent, so an image handle can react to hover and drag like a drawn
  one.
- **UltraCanvasSplitPane**: **action icons on the split line**. Any number of
  icons per splitter (`AddSplitterIcon`, `SetSplitterIcons`,
  `InsertSplitterIcon`, `RemoveSplitterIcon`, `ClearSplitterIcons`), each with
  an image or text glyph, tooltip, enabled/visible state and its own click
  handler, plus a pane-level `onSplitterIconClicked`. Icons are centred across
  the line and grouped along it, either inside the handle (which auto-sizes to
  wrap them) or at their own `SplitterIconStyle::position`. Pressing an icon
  does not start a drag: the click fires on release over the same icon, the
  cursor turns into a hand, and a disabled icon dims and swallows the press.
  Icons live on the split pane rather than on the splitter objects, so they
  survive pane insertion and removal.
- **UltraCanvasSplitPane**: `SplitPaneStyle::splitterHitMargin` is wired up -
  it now widens the grab strip on each side of the line without thickening the
  painted line. New guide `Docs/UltraCanvas/UltraCanvasSplitPane.md` (the demo
  already pointed at it) and three new demo sections covering handle shapes,
  icons in a capsule handle, and icons on a bare vertical split line.

#### 2026-08-01 *0.3.23*
- **UltraCanvasDendrogram** *(1.5.1)*: fixed radial leaf labels rendering upside
  down in the upper-right quadrant. The left/right half test was
  `angle > -pi/2 && angle < pi/2`, which assumes angles in `(-pi, pi]`, but
  `DendrogramLayoutEngine::ApplyRadialLayout` produces `[0, 2pi)` — so every leaf
  between `3pi/2` and `2pi` failed the test, took the left-half branch and picked
  up an extra 180-degree rotation. Both the leaf labels and the group arc labels
  now test `cos(rotation)`, which is precisely the condition for "this text is not
  upside down" and does not depend on which angle range the layout uses. Audited
  the other radial components at the same time — Sunburst, Chord, RadialBar,
  CircularInfoGraphic and PolarChart all normalise correctly and were unaffected.

#### 2026-07-31 *0.3.22*
- **UltraCanvasNodeDiagram** *(2.2.0)*: organizational-network features. Node
  size can now be driven by the data - `NodeSizeMode::ByDegree` sizes a node
  from its connection count, `ByValue` from a new `NodeDiagramNode::value`
  field, both through a sqrt transfer so node AREA tracks the quantity rather
  than the diameter. Degrees can count total / incoming / outgoing links and are
  cached, so bulk-loading a graph costs one rebuild instead of one per
  `AddLink`. New `NodeDiagramGroup` cluster containers wrap a set of member
  nodes in an auto-fitted boundary box (solid or dashed, optional fill and
  corner radius, title in any corner) that follows its members through dragging
  and re-layout; boxes draw behind the links and titles after the nodes, both
  sized in screen pixels so they hold up at any zoom. `SetGroupCohesion()` adds
  a per-group centroid attraction to the force-directed layout - without it
  repulsion scatters a cluster and the boxes overlap into mush. New color legend
  overlay (`NodeDiagramLegendConfig`, `BuildLegendFromGroups()`) drawn in screen
  space in any corner. Groups and sizing round-trip through `ToJson()` /
  `FromJson()`, and group boxes are included in `ComputeContentBounds()` so
  `FitView()` and the minimap account for them. New demo tab (Diagrams > Node
  Diagram > Organization): a five-department company network with a control
  column for layout, sizing mode, degree mode, cohesion, link style, node shape,
  theme, and toggles for the boxes, legend, grid, minimap, controls and snap.
- **UltraCanvasDendrogram** *(1.5.0)*: hierarchical edge bundling (Holten 2006).
  `AddRelation()` registers a leaf-to-leaf association that does not follow the
  tree's parent/child structure; each one is routed through the tree path source
  -> lowest common ancestor -> target, relaxed toward the straight chord by
  `SetBundlingStrength()` (beta), and smoothed with a clamped cubic B-spline. A
  radial dendrogram can now show its hierarchy and the cross-links between its
  leaves at the same time without becoming a hairball. Also new: area-proportional
  node dots via `DendrogramNodeSizeMode::ByValue` and `DendrogramNode::nodeValue`,
  normalised against the largest value in the tree - replacing the single global
  `style.leafNodeRadius` as the only leaf size available.
- **UltraCanvasJitterPlotElement** *(1.3.0)*: per-point encodings. A parallel
  vector of size magnitudes plus `JitterPointSizeMode::ByValue` turns a beeswarm
  into a bubble beeswarm, with the packer receiving the real per-point radii so
  mixed sizes pack without overlapping. A parallel vector of color values plus
  `JitterPointColorMode::ByValue` samples any `UltraCanvasColormap` palette,
  including the diverging ones with a configurable midpoint, so a signed quantity
  reads correctly around zero. Fixes: `minScoreFilter` defaulted to `0.0` and
  silently discarded every negative value before rendering; `AddCategoryData()`
  did not invalidate the point-position cache, so a second call left the previous
  points on screen; `RenderJitterPoints()` always drew a plain circle and ignored
  both `SetPointShape()` and the point edge style.

#### 2026-07-31 *0.3.21*
- **UltraCanvasTimelineChart**: added the swimlane grouping mode
  (`TimelineLaneMode::Swimlanes`). The same date axis and the same entries, with
  rows given identity: one named band per workstream, a name column on the left
  and a tinted background per row. Rows are declared with `SetSwimlanes()` or
  derived from the distinct `TimelineChartEntry::swimlaneName` values in
  first-appearance order; milestones move inside their band with the label
  beside them; the axis is forced to the top. Each band sub-packs with the same
  packer and the bands share one height budget - rows are compressed before any
  sub-row is dropped, so a busy row can keep three sub-rows while quiet rows
  keep one. New `SwimlaneProgram()` / `ProgramSwimlanes()` samples and a
  Swimlanes demo tab with a row-grouping control.
- **UltraCanvasTimelineChart**: the lane packer now tracks each row's full
  interval list instead of only its right edge. Point events are placed in
  importance order rather than date order, so with a single right edge an event
  earlier than everything already placed could not be inserted and ended up
  overlapping a bar. Affects packed mode as well as swimlanes.
- New **UltraCanvasTimelineChart** element
  (`Plugins/Charts/UltraCanvasTimelineChart`): the chronological counterpart to
  the timeline diagram — milestones and spans placed to scale on a real date
  axis, with no task table and no dependency graph. Four entry kinds
  (milestone, span, era band, project bookend), eight marker styles, four bar
  styles, open-ended spans, uncertain dates, span progress, and five design
  presets (`Modern`, `Classic`, `Minimal`, `Roadmap`, `Dark`) over the usual
  palettes and dark theme. Spans and milestone callouts share one shelf packer
  per side of the axis, so a label is never drawn on a bar; when a side runs
  out of room the least important labels are dropped rather than overprinted,
  and the marker is always kept. Wheel zoom anchored on the cursor's date, drag
  pan, double-click to refit, selection, tooltips and callbacks. New demo page
  (Info Graphics > Timeline Chart) and guide in
  `Docs/UltraCanvas/UltraCanvasTimelineChart.md`.
- New header-only **UltraCanvasTimeAxis**
  (`include/Plugins/Charts/UltraCanvasTimeAxis.h`): date<->pixel projection,
  automatic scale resolution (minutes through decades) from the current
  pixels-per-day, two-tier tick generation with Monday-based weeks and
  calendar-correct month/quarter/year stepping, and cursor-anchored zoom. Day
  serials match `GanttDate::serial`, so the axis, the Gantt chart and the
  timeline elements share one date representation.
- New **UltraCanvasTimelineDiagram** element
  (`Plugins/Diagrams/UltraCanvasTimelineDiagram`): the narrative timeline
  infographic — an ordered list of events laid out along a decorative path,
  with nine design presets (`Bar`, `Line`, `Alternating`, `Cards`, `Vertical`,
  `Serpentine`, `Hanging`, `Chevron`, `Steps`). Items carry a period caption,
  title, paragraph and icon glyph; cards, boxes and bubbles size themselves to
  their text, and the `Hanging` design wraps bubble text to the circle and
  staggers bubbles over several tiers so they never collide. Palettes
  (`CorporateBlue`, `Vibrant`, `Pastel`, `Ocean`, `Sunset`, `Forest`, `Slate`,
  `Mono`, custom), `PerItem`/`Single`/`GradientAlongPath` color modes, a dark
  theme, side policies, reversible direction, a "current position" pending
  style, an independent scale-label track, hover/selection/tooltips with
  callbacks and node/content geometry queries. `TimelinePlacement::Proportional`
  positions items by real dates (serials compatible with `GanttDate`) while
  keeping the decorative design. New demo page (Info Graphics > Timeline
  Diagram) and guide in `Docs/UltraCanvas/UltraCanvasTimelineDiagram.md`; the
  research behind splitting narrative timelines from date-accurate ones is in
  `Docs/UltraCanvas/UltraCanvasTimelineDiagramProposal.md`.
- Fixed duplicated Trim and base64 code.

#### 2026-07-30 *0.3.20*
- **UltraCanvasScatterPlotElement**: correlation / trend line display. The
  element can now fit a least-squares regression line over its data
  (`SetShowTrendLine`), styled solid, dashed or dotted with settable colour
  and width, plus an optional readout of the fitted equation and the Pearson
  correlation (`SetShowCorrelationInfo` draws `y = ax + b`, r and r² in the
  plot corner). `ComputeLinearRegression` and `GetCorrelationCoefficient`
  expose the fit programmatically. Points whose `ChartDataPoint::color` is
  set now render in that colour, so outliers or categories can be marked
  without extra elements.
- New **UltraCanvasScatterPlot3DElement**
  (`Plugins/Charts/UltraCanvasScatterPlot3D`): a software-rendered 3D scatter
  plot for (x, y, z) point clouds. Perspective camera with drag-to-orbit,
  wheel zoom and view presets; perspective axes with ticks, titles and an
  optional ground grid that re-anchor to the corner nearest the camera;
  depth cueing (perspective point sizing plus optional fade towards the
  background); per-point colours; hover tooltips with X/Y/Z. An optional 3D
  correlation line — the principal axis of the cloud, i.e. the orthogonal
  least-squares fit from the covariance matrix's dominant eigenvector — is
  depth sorted into the points so it threads through the cloud with correct
  occlusion. `GetCorrelationLine` returns the fitted centroid and direction
  in data space. The Charts > Scatter Plot demo page now shows both the 2D
  trend line and the 3D cloud side by side, and a programmer's guide lives in
  `Docs/UltraCanvas/UltraCanvasScatterPlot3D.md`.

#### 2026-07-30 *0.3.20*
- New **UltraCanvasGitGraph** element
  (`Plugins/Diagrams/UltraCanvasGitGraph`): renders a Git commit history — a
  DAG of commits decorated with branch, tag and `HEAD` refs. Two layout
  families share one data model: **Lanes**, one lane per open line of
  development with the newest commit first (the gitk / GitKraken / SourceTree
  repository view), and **Swimlane**, one band per branch either side of a
  nominated trunk (the git-flow teaching diagram). Lane assignment is a single
  active-lane sweep with two strategies — `Stable` keeps a branch on one lane
  for its whole life (straight branches), `Compact` recycles freed lanes
  (narrow graphs) — plus trunk pinning so a nominated branch always holds lane
  0. Handles merges, octopus merges, multiple roots, boundary commits whose
  parents lie outside the loaded window, and cherry-picks (dashed non-parent
  edges). Four orientations, four commit orderings (as-given, commit date,
  author date, topological), four edge routings (orthogonal with rounded
  corners, Bezier, arc, straight), ref chips with `+n` overflow, per-commit
  changed-file boxes with leader lines, callout annotations, an arrowheaded
  trunk baseline, six themes, virtualised rendering (only rows in the viewport
  are drawn), zoom/pan/selection/tooltips/keyboard navigation and SVG export.
  Ingest is programmatic, from `git log` output (`LoadFromGitLog` +
  `GitLogFormat`), or via the authoring API (`Branch`/`Commit`/`Merge`/
  `CherryPick`/`Tag`) — the element is read-only and never executes git.
  The layout core (`UltraCanvasGitGraphLayout`) is headless and covered by
  `Tests/GitGraphLayoutTest.cpp` (11 cases, including 200 randomised DAGs
  checked against the placement invariants). New demo page (Diagrams > Git
  Graph), guide in `Docs/UltraCanvas/UltraCanvasGitGraphExamples.md`, research
  write-up and roadmap in `Docs/UltraCanvas/UltraCanvasGitGraphProposal.md`.
- **UltraCanvasGitGraph**: second feature pass. **Lazy loading** —
  `IGitGraphDataSource` pages a history into the element as the viewport nears
  the end of what is loaded. **Mermaid I/O** — a headless
  `UltraCanvasGitGraphMermaid` unit imports and exports the `gitGraph` DSL
  (commit/branch/checkout/switch/merge/cherry-pick, the id/tag/type/msg/order/
  parent attributes, `%%` comments, the `%%{init: ...}%%` header and the
  LR/TB/BT direction suffixes), reporting parse errors with a line number.
  **Native `.git` reader** — new `UltraCanvasGitRepository` (core) reads refs
  (loose, `packed-refs`, annotated tags peeled), loose objects and packfiles
  including `OFS_DELTA`/`REF_DELTA` chains, inflating with the vendored miniz;
  no git executable is spawned and no dependency is added, and
  `UltraCanvasGitRepositorySource` adapts it to the lazy loader. **Filtering**
  by text, author, path, branch, date range and merge status, with edges through
  filtered-out commits re-pointed at the nearest surviving ancestor and drawn
  dashed. **Crossing reduction** — lane columns reordered by a barycentre
  heuristic that keeps the best measured permutation (about a third fewer
  crossings over 300 randomised layouts, never worse), budgeted and off by
  default. **Commit table pane** row-aligned beside the graph with configurable
  columns and a date formatter, plus a row-alignment API for pairing an external
  `UltraCanvasTableView`. **Search** over sha, subject, author and refs with
  next/previous navigation. **Minimap** with a viewport rectangle and
  click/drag navigation. New tests: `GitGraphMermaidTest`, `GitRepositoryTest`
  (runs against a real repository) and six more `GitGraphLayoutTest` cases.
- **UltraCanvasGitGraph**: third feature pass. **Time-proportional axis** —
  row position follows the commit timestamp, with a per-gap floor and ceiling so
  a burst of same-second commits stays readable and a multi-year quiet period
  costs one large gap instead of an unusable amount of empty axis; an optional
  date ruler prints one label per calendar day. **Collapsing** — a run of plain
  single-parent commits folds into one dashed pill labelled with how many
  commits it stands for, expandable by double-click; refs, merges, roots and
  cherry-picks are never folded. **Parallel rows** — commits made at the same
  moment share a row (mermaid's `parallelCommits`), but only when neither is the
  other's parent and they sit in different lanes, so no edge is ever flattened.
  **Badges** for GPG signature and build status, **stash chips**, **author
  avatars** (initials on a colour derived from the author) in the table pane,
  **label collision avoidance** so overlapping text is dropped rather than
  stacked, and **JSON export** of the laid-out geometry. Four more layout tests
  cover collapsing and parallel rows. Note: `GitGraphSignature` /
  `GitGraphBuildStatus` use `NoSignature` / `NoStatus` and `Passed` / `Failed`
  rather than `None` and `Success` / `Failure`, because X11's `X.h` defines
  `None` and `Success` as macros and the header reaches the window backend.
- **UltraCanvasGitGraph**: fourth feature pass. **Explicit lane ordering** —
  `SetLanePriority()` puts named branches in the columns you choose, applied
  after crossing reduction so it always wins over the heuristic; a pinned trunk
  keeps column 0 and unlisted branches keep their relative order. **Diff pane**
  across the bottom of the element: a header naming the commit, the files it
  touched coloured by A/M/D status, and the patch for the selected file coloured
  by unified-diff prefix, each half scrolling independently. The element never
  computes diffs — `SetFileListProvider()` and `SetDiffProvider()` supply them.
  **Drag to author** — dragging a commit onto another adds a merge on the
  target's branch, dragging into empty space branches off it; a drag that never
  moved is just a selection, and a merge duplicating an existing parent link is
  refused. Also `GetCommitScreenPosition()` for anchoring popovers to a node.
- **UltraCanvasGitRepository**: `ReadChangedFiles()` produces a commit's changed
  files by recursively diffing its tree against its first parent's (a root
  commit reports its whole tree as added), handling files replaced by
  directories and vice versa. Verified against `git show --name-status` on this
  repository, including merges. Blob-level diffing is not implemented.
- **VirtualFS / UltraCanvasFilerWidget**: fixed archives always listing as
  "(empty folder)" on Windows. `VirtualFSPath::Resolve()` prefixed a slash to
  the real-filesystem part of every absolute path, turning a drive-letter path
  like `C:/Users/…/archive.zip` into `/C:/Users/…/archive.zip` — a path no
  provider could open, so double-clicking any ZIP (or other archive) in the
  filer showed an empty view. Drive-letter paths now keep their bare `C:`
  prefix through resolution, and `Normalize()` treats them as absolute so
  `..` components can no longer escape above the drive root. The filer also
  distinguishes an unreadable archive from a genuinely empty one: when the
  provider layer cannot open the archive (missing format provider, corrupt or
  password-protected file), it reports "Cannot read archive: …" through the
  widget's error callback instead of silently rendering "(empty folder)".
  New header-only regression test `Tests/VirtualFSPathTest.cpp` covers Unix,
  relative, backslash and drive-letter archive paths, including nested
  archives.
- New **UltraCanvasCircularProgressChart** element
  (`Plugins/Charts/UltraCanvasCircularProgressChart`): the angle-encoded
  member of the circular chart family — every ring carries one independent
  value drawn as an arc whose sweep is proportional to that value within the
  ring's own range (concentric "activity rings"). Sub-styles cover the single
  thick progress ring and the progress pie (filled sector over a track disc).
  Rings take their colour from a palette or per-ring override, draw the
  remainder as an auto-tinted, explicit or hidden track, and end in round or
  butt caps. Labels: percentage/value callouts at each arc tip (optionally in
  a bubble), ring names inside the band at the arc start, or a stacked column
  of `label value` rows aligned with each ring's start point. A centre disc
  with title + subtitle, a numbered-chip or swatch legend on any side (with
  optional per-ring icons), configurable start angle and winding direction,
  hover highlighting, tooltips and click/hover callbacks complete the P1
  feature set. New demo page (Charts > Circular Progress Chart), programmer's
  guide in `Docs/UltraCanvas/UltraCanvasCircularProgressChart.md`, plus the
  family-wide guide `Docs/UltraCanvas/UltraCanvasCircularCharts.md` and the
  previously missing `Docs/UltraCanvas/UltraCanvasCircularInfoGraphic.md`.
- **UltraCanvasPieChartElement**: family-standard controls added —
  `SetStartAngle` / `SetClockwise` for orientation and winding,
  `SetCenterKPI(text, caption)` drawn in the donut hole, and
  `onSliceClick` / `onSliceHover` callbacks.

#### 2026-07-29 *0.3.19*
- New **UltraCanvasPolarChart** element
  (`Plugins/Charts/UltraCanvasPolarChart`): a general polar coordinate chart
  where every observation is an (angle, radius) pair. One element covers the
  whole family of round plots built on that system — polar scatter, line,
  spline, area, spline area and columns (the Nightingale rose / stacked polar
  column chart) — and the types can be mixed in a single chart. The angular
  axis works in numeric mode (each point carries its own angle, mapped through
  a configurable domain) or categorical mode (one evenly spaced slot per
  category, placed either on the grid spokes or between them), with settable
  zero angle, winding direction, sweep angle for fans and wedges, automatic or
  explicit tick intervals, horizontal/tangential/radial label orientation and
  an optional second ring of angular labels with its own interval and side.
  The radial axis supports linear, logarithmic and square-root (area-true)
  scales, automatic "nice" ticks or a manual range, negative minima, reversed
  direction, a donut hole and unit-suffixed labels on a selectable spoke.
  Circular or polygonal spider-web grids, minor rings, alternating ring
  shading, radial tolerance bands and angular sector bands render behind the
  data; column and area series support stacked and percent-stacked modes while
  unstacked columns group side by side inside their slot. Interaction covers
  hover highlighting, tooltips, click/hover callbacks, click-to-toggle legend
  entries in four positions and optional drag-to-rotate. New demo page
  (Charts > Polar Chart) with six examples and a live control panel, plus a
  programmer's guide in `Docs/UltraCanvas/UltraCanvasPolarChart.md`.

#### 2026-07-28 *0.3.18*
- **UltraCanvasMediaViewer**: the folder path strip now uses the same path
  mechanism as the filer. The shared builder `BuildFolderBreadcrumb()` (plus
  `ListDriveRoots()` and `FolderBreadcrumbOptions`, declared in
  `UltraCanvasBreadcrumb.h`) fills a breadcrumb with a leading **"Computer"**
  node whose dropdown lists every drive / mounted volume, the drive (or root)
  node, and one node per folder. The media viewer built its own strip before, by
  iterating the whole `std::filesystem::path`, which on Windows turned the root
  separator into a node of its own (`C:` → `\` → `Users` → …) and offered no way
  to reach another drive. The filer demo's private copy of the logic is gone —
  both now call the shared builder, so a path strip behaves identically wherever
  it appears (segment click browses the folder; the segment dropdown lists the
  sibling folders at that level).
- **UltraCanvasBreadcrumb**: fixed long paths overflowing the strip instead of
  collapsing when an interlocking item style (`Arrow` / `Parallelogram`) was
  used — as seen on the media viewer, whose last folder was clipped at the right
  edge with no `...` menu. Those styles butt their segments together and add the
  notch depth (`arrowSize`) per neighbour, but overflow handling was still
  costing a separator plus its spacing (0 for both presets), so the strip
  under-measured itself by `arrowSize × segments` and decided everything fitted.
  Measurement, collapse and slot building now share one per-neighbour cost, and
  the `...` placeholder carves the same left notch as any other segment.
- **UltraCanvasBreadcrumb**: min-content width in `Collapse` mode is now the
  collapsed floor (kept first item + `...` + the trailing items the style keeps)
  instead of the full uncollapsed path, so a parent layout can shrink the strip
  to its own width rather than being widened by a deep path. Other overflow
  modes keep every item and are unchanged.
- New **UltraCanvasSWOTDiagram** element
  (`Plugins/Diagrams/UltraCanvasSWOTDiagram`): classic four-panel SWOT
  analysis infographic rendering four text item lists (Strengths, Weaknesses,
  Opportunities, Threats) in six design presets — corner-badge panels with a
  central SWOT circle, classic 2x2 matrix (optional internal/external and
  helpful/harmful axis captions), separated header-bar cards, central letter
  diamond, stacked rows with big letter blocks, and four columns with header
  chips. Light/dark theme, per-quadrant titles/badges/accent colors,
  hover tooltips, item selection with callbacks and built-in sample data.
  New demo page (Info Graphics > SWOT Diagram) with six tabbed designs and
  runtime theme/decoration/data controls, plus a programmer's guide in
  `Docs/UltraCanvas/UltraCanvasSWOTDiagramExamples.md`. A survey of the
  SWOT presentation styles found in the wild is in
  `Docs/UltraCanvas/SWOTDiagramDesignVariants.md`.
- Fixed slow video thumbnail generation in MacOS in Album control

#### 2026-07-25 *0.3.17*
- **UltraCanvasFilerWidget**: double-click vs rename behavior corrected
  (0.3.16 had it wrong). Double-clicking an entry — name or icon — now always
  opens/activates it: folders and compressed archives are entered, files fire
  `onFileActivated` (executable start / open with the designated program).
  The inline rename is instead triggered Windows-style: a single click on the
  **name** of the entry that is already the only selected one opens the rename
  editor after a short delay (500 ms, longer than the double-click interval, so
  the first click of a double-click never starts a rename). A drag, a
  double-click, a key press, a refresh or a folder/view change cancels the
  pending rename.
- **UltraCanvasFilerWidget**: double-clicking a compressed archive now really
  opens it like a folder (it always listed as empty before). Three fixes:
  - The widget listed archive interiors with `VirtualFS_ListDirectory()`
    without ever initializing VirtualFS, so no archive provider was registered
    and every archive came back empty. `ScanFolder()` now runs
    `UltraCanvasVirtualFSBridge::Initialize()` (idempotent) before listing.
  - VirtualFS entries carry archive-internal paths (`sub/file.txt`); the
    widget stored them as the entry path, so descending into a folder inside
    an archive navigated to a nonsense path. Entry paths are now built as
    `<current folder>/<name>`, giving full virtual paths
    (`/path/archive.zip/sub`) that also work for nested archives.
  - Without the VirtualFS module in the build, activating an archive now
    fires `onFileActivated` like any other file instead of navigating into a
    permanently empty view.
- **VirtualFS (libarchive provider)**: archives that store no explicit
  directory headers (Python `zipfile`, several archivers) now show their
  subdirectories. The entry cache synthesizes a Directory entry for every
  path ancestor implied by a member (`sub/b.txt` ⇒ `sub`), so subfolders
  appear when listing the parent and can be descended into; an explicit
  directory header arriving later replaces the synthesized entry's metadata
  without duplicating it in the listing.
- New **UltraCanvasQuadrantChart** element
  (`Plugins/Charts/UltraCanvasQuadrantChart`): interactive 2x2 strategic
  matrices with presets for SWOT, BCG, Ansoff, Eisenhower, Gartner magic
  quadrant, risk and priority frameworks plus fully custom quadrant
  labels/colors/axis captions. Data points support per-point color, radius
  (BCG-style bubbles), shape (circle/square/triangle/diamond) and outline;
  hover tooltips, click (multi-)selection, double-click callbacks and
  per-quadrant statistics utilities are built in. New demo page
  (Charts > Quadrant Chart) with six tabbed examples and runtime style/data
  controls, plus a programmer's guide in
  `Docs/UltraCanvas/UltraCanvasQuadrantChartExamples.md`.

#### 2026-07-22 *0.3.16*
- **UltraCanvasFilerWidget**:
  - Default display font reduced from 13 to 12 px (Windows standard 9pt @ 96dpi).
  - The inline rename editor now uses the same font size as the on-screen name
    for the current view (base size in the row views, the small size in the
    thumbnail / treemap captions) instead of a fixed larger size.
  - Double-clicking a file's **name** now starts an inline rename; double-clicking
    its **icon** (or, in Details view, another column) still opens/activates the
    entry.
- eBook reader: the table-of-contents toolbar button now uses the
  `list-ordered` icon (drawn as a mask so it takes the button's text color)
  and highlights while the TOC pane is open (accent fill with a light icon),
  so its active state is visible. It reverts to the normal toolbar-button
  look when the pane is hidden.

#### 2026-07-22 *0.3.15*
- **UltraCanvasFilerWidget**: new **Display > Dataset** submenu with toggles for
  extra per-file facts shown under the name in the thumbnail views — Size, Edit
  date, Creation date, Attributes, Length (audio/video) and Dimensions
  (bitmaps). Each enabled field adds a caption line (Length/Dimensions only
  appear on the file kinds they apply to); tiles grow to fit and the grid stays
  aligned. Also available programmatically via `SetDatasetField()` /
  `SetDatasetFields()` with the new `FilerDatasetField` flags.

#### 2026-07-22 *0.3.14*
- **UltraCanvasFilerWidget**: picking a format from the context menu's
  "Compress" submenu now opens a modal compress dialog instead of creating the
  archive immediately. The dialog shows:
  - the archive's file-type icon on top,
  - an editable file name (with the format's extension shown as a suffix),
  - the destination folder as smaller, separate text.

  The icon can be **dragged onto any folder in the view** to retarget the
  destination path — the folder under the icon highlights while dragging, and
  dropping on it updates the "Location". Enter / the Compress button creates the
  archive; Esc / Cancel dismisses. Because the icon must be droppable onto the
  folders behind it, the dialog is an in-widget overlay rather than a separate
  top-level modal window.

#### 2026-07-22 *0.3.13*
- **UltraCanvasFilerWidget**: the right-click context menu now closes on a
  left click anywhere outside it. Previously the popup registered the whole
  Filer widget as its `popupOwner`, and the window's dismissal logic treats a
  click on the owner as "inside" the popup — so clicking in the file view left
  the menu stuck open. The context menu no longer sets an owner, so any click
  outside the menu bounds dismisses it.
- **UltraCanvasFilerWidget**: the context menu's "Compress" entry is now a
  submenu listing the available archive formats — ZIP, 7-Zip, TAR, TAR+gzip,
  TAR+bzip2, TAR+xz and TAR+Zstd. `CompressSelection(extension)` takes the
  target extension (default `zip`) which selects the format.
- **VirtualFS (libarchive provider)**: `CreateArchive` now recognises compound
  archive extensions (`.tar.gz`, `.tar.bz2`, `.tar.xz`, `.tar.zst`, `.tar.lz4`)
  when picking the format and filter. It previously inspected only the final
  token (e.g. `gz`), which selected the ZIP format and then layered a gzip
  filter on top, producing a corrupt archive for those names.
- Fix two MOBI/KF8 eBook rendering bugs (seen with the DemoApp eBook demo on
  `media/ebooks/Game-of-rat-and-dragon.mobi`):
  - **Drop caps.** Mobipocket/Project-Gutenberg books set the decorative
    first letter of a section as a floated image
    (`<div class="figleft"><img alt="P"/></div>` in front of the paragraph).
    The layout engine has no CSS `float`, so each big letter stacked as a
    centred block *above* its paragraph instead of leading it. The MOBI
    engine now folds such single-letter drop-cap figures into a large inline
    first letter at the start of the following paragraph, so "P" reads in
    front of "inlighting" as intended. Genuine illustrations (multi-character
    or empty `alt`) are left untouched as block images.
  - **Inline table of contents.** kindlegen/calibre append the book's own
    "Table of Contents" page as the last part of the file, so it appeared at
    the very end of the chapter list. It is now moved to the second page,
    right after the cover/title image, where readers expect it.
- **UltraCanvasListView now supports variable row heights.** The delegate
  hook `IItemDelegate::GetRowHeight(model, row)` — previously declared but
  never consulted — is now wired into the view when variable mode is enabled
  via `SetVariableRowHeights(true)`. Every row can report its own height; the
  view keeps a lazily-rebuilt prefix-sum of row tops so scrolling,
  hit-testing (`GetRowAtY`), `GetRowRect`, `EnsureRowVisible`, `ScrollToRow`,
  culling and Page Up/Down navigation are all height-aware. Uniform rows stay
  the default and keep their original arithmetic fast path (no table). Call
  `InvalidateRowHeights()` when a custom delegate's sizing changes without a
  model signal (e.g. an async delegate finished measuring a row).
- **UltraCanvasFilerWidget shrinks thumbnail rows to fit landscape images.**
  In the thumbnail grid views the tiles are square (the selected Small /
  Medium / Big / Maximized edge), which leaves a tall empty band above and
  below wide photos. A grid row whose images all display shorter than the
  tile edge is now shortened to the tallest image actually shown in it; a row
  that holds any full-height item (a folder, a generic-glyph file, a
  vector/portrait/square or not-yet-measured image) keeps the full edge.
  Natural image sizes come from the existing header-only probe (no decode),
  cached per file. Controlled by `SetShrinkThumbnailRows(bool)` (default on).

#### 2026-07-21 *0.3.12*
- Fix GIF export failing with `magicksave: libMagick error:
  NoEncodeDelegateForThisImageFormat 'gif'` (seen on the DemoApp bitmap
  performance-comparison page). When libvips has no native cgif `gifsave`,
  the previous fallback routed the write through ImageMagick's `magicksave`,
  but ImageMagick's GIF *coder* is itself an optional build-time delegate —
  on systems without it the save threw at run time. GIF export no longer
  depends on either cgif or ImageMagick: a bundled, dependency-free GIF89a
  encoder (`libspecific/Cairo/UltraCanvasGifEncoder.h`, median-cut
  quantisation + LZW, like the existing bundled BMP/QOI encoders) is used
  whenever native `gifsave` is unavailable. It honours the requested colour
  depth and interlacing and keeps 1-bit transparency for RGBA sources.
  Both `UCImageRaster::Save` and PixelFX `SaveGif` route through it.
 
#### 2026-07-20 *0.3.11*
- Make work VTracer/Vectorizer plugin.
- Implement RemoveFromCache() method for images used for reload
- OCR plugin now supports **all Tesseract languages**, not just the bundled
  English pack. The full upstream catalogue (~130 languages) is exposed via
  `UltraCanvasOCR::SupportedLanguages()`, and any language's `traineddata` is
  fetched on first use instead of having to be pre-bundled:
  - `EnsureLanguages({codes}, err, tier)` seeds each requested pack from a
    local copy when one exists, otherwise downloads it (via UltraNet),
    consolidates them into a single directory so Tesseract can load them
    together, and reconfigures the engine.
  - `DownloadLanguage(code, tier, err)` fetches a single pack; `OCRDataTier`
    picks the source repo (`Fast`→tessdata_fast, `Standard`→tessdata,
    `Best`→tessdata_best).
  - `InstalledLanguages()` / `IsLanguageInstalled(code)` report what is
    present locally; downloaded packs are cached once under
    `LanguageDataDir()` (per-user data dir) and discovered automatically by
    the Tesseract engine's data-path resolver.
  - On a build without network support, packs can be dropped into the
    per-user directory manually and are picked up the same way.
- The DemoApp OCR screen gains a language dropdown populated from the full
  catalogue; languages that are not installed yet are marked and downloaded
  on demand when "Run OCR" is pressed.
#### 2026-07-19 *0.3.10*
- Fix GIF export failing with `VipsOperation: class "gifsave" not found` on
  builds whose libvips lacks cgif (the MSYS2/Windows package is built with
  `-Dcgif=disabled`). `UCImageRaster::Save` and PixelFX `SaveGif` now probe
  for the native `gifsave` operation and fall back to the ImageMagick bridge
  (`magicksave` with `format=gif`), which the Windows package already ships.
- Image export errors no longer include stale libvips messages from earlier
  operations (e.g. recoverable HEIF "bad seek" noise appearing inside a GIF
  save failure): the libvips error buffer is cleared before each save.
- `UltraCanvasLabel` now renders its text vertically centered by default
  (`LabelStyle::verticalAlign` and the `SetAlignment()` vertical default
  changed from `Top` to `Middle`), so labels line up with the text of
  neighbouring buttons/checkboxes in toolbar rows. Auto-sized labels are
  unaffected (their box hugs the text); labels that need top alignment can
  request it explicitly via `SetAlignment(h, VerticalAlignment::Top)`.
  Fixes the misaligned file-dimensions info label in the DemoApp codec
  comparison benchmark toolbar.
- Filer widget: optional **"Compressed thumbnails"** mode
  (`SetCompressedThumbnails(bool)`, default off; toggle in the Filer demo).
  Finished thumbnails are held in memory QOI-compressed instead of as raw
  ARGB32 pixmaps (measured 6.4× smaller on the demo set; typically 2–4× on
  photos), with a 32 MB hot cache of decompressed tiles covering the
  visible + prefetch bands so scrolling still draws raw surfaces. The codec
  is a new Cairo-native QOI variant (`libspecific/Cairo/QoiPixmapCodec.h`,
  separate from the `qoi.cpp` file-format codec) that compresses the
  premultiplied ARGB32 buffer in place — bit-exact round trip (rendering is
  pixel-identical in both modes), ~140 µs encode / ~32 µs decode per medium
  tile, HiDPI device scale preserved. `GetThumbnailCacheStats()` reports
  stored vs. raw bytes for A/B comparison.
- Filer widget: thumbnail decoding is now **viewport-driven with a one-screen
  prefetch**. Only files whose tiles are visible — plus at most one viewport
  height (width in the horizontal List view) ahead in scroll direction — are
  ever decoded, so slow scrolling almost always lands on already-decoded
  tiles. The decode queue is rebuilt every frame in priority order (visible
  tiles first, prefetch band after), and pending decodes that scroll out of
  both bands are dropped from the queue — a fast flick past hundreds of
  photos decodes only what you stop at, and the tiles you are looking at
  never wait behind tiles you scrolled past.
- Filer widget: thumbnails are now loaded **asynchronously**. Opening a folder
  renders its content immediately (names, layout, info bar) with the generic
  category glyph in each tile; the real image thumbnails are decoded on
  background worker threads and fill in as they become ready, each batch
  posting one coalesced redraw via `PostToUIThread`. Previously the first
  frame of a folder blocked until every visible thumbnail was fully decoded
  on the UI thread, which froze the window for seconds on photo folders —
  in all views (the details/list icon column decoded images too). Decoded
  pixmaps land in the shared image/pixmap caches (so other consumers get
  cache hits), the widget's own bookkeeping is bounded by a 96 MB budget,
  decodes of the same file are serialized, and pending work is dropped on
  folder change / view change / widget destruction.

#### 2026-07-19 *0.3.9*
- Merge "JSON support in UltraCanvas API"
- Merge "JMAP support for UltraNet"

#### 2026-07-17 *0.3.8*
- Fix missing method implementation SetIconMaskColor() in the Button
- Fix crash in the PixelFX FloodFill demo

#### 2026-07-12 *0.3.7*
- Filer widget: in the thumbnail views (`ThumbnailsSmall`/`Medium`/`Big`/
  `Maximized`) images smaller than the tile are no longer upscaled to fill
  it — they are drawn at their original size, centered in the tile
  (`ImageFitMode::ScaleDown`). Larger images still scale down to fit as
  before, and the other views keep their `Contain` icon fitting.
- `UltraCanvasTabbedContainer`: the overflow dropdown is now disabled for
  vertical tab layouts (`TabPosition::Left`/`Right`). It rendered a faulty
  display there and was not usable for vertical tabs, so
  `CheckIfOverflowDropdownNeeded()` always returns `false` when tabs are
  vertical — the overflow button never displays or takes part in tab-bar
  layout. If the feature is accidentally switched on for vertical tabs
  (enabling the dropdown while vertical, or switching to a vertical position
  while the dropdown is enabled), a warning is emitted: "Overflow dropdown not
  supported for vertical tabs".
- Rating: fixed the built-in **Circle** symbol never showing its filled/selected
  state, so a circle rating appeared to ignore clicks (the "Circle and Square
  symbols" demo row). The filled portion of each symbol is painted by re-drawing
  the shape inside a `ClipRect` (clipped to the filled fraction); the circle was
  drawn with `FillCircle`, whose arc-based fill is not rendered inside an active
  clip region on some back-ends, so only the unclipped empty base showed and the
  rating looked unselectable. The circle is now drawn as a filled polygon via
  `FillLinePath`/`DrawLinePath` — the same clip-honouring primitive the Star uses
  — so all three built-in shapes (Star, Circle, Square) render their fill through
  a consistent code path. The disc is visually unchanged.
- Album demo: the video player window gained an info bar under the video
  surface showing the clip's title and its source link (clickable — opens in
  the system browser), and the Lola Lexy tile's link line now reads
  `youtube.com/LolaLexy`.
- Animated images (GIF / animated WebP) now play in the lightbox image viewer
  (`UltraCanvasImageViewer`): the zoom / pan surface steps them with the shared
  `UCImageAnimationController`, so zoom and pan apply to the running animation
  — matching `UltraCanvasImageElement` and the media viewer.
- Album demo: the seed list now leads with an animated GIF tile
  ("Charlie Chaplin run", `media/images/charlie-chaplin-run.gif`) that plays
  in the photo lightbox; removed the placeholder tiles "Brand Reel",
  "Chill Beats" and "Roadtrip".
- Fixed Album demo video-window bugs (merged as PR #102): closing the player
  window while a clip is playing (title-bar close button included) now stops
  playback — the demo viewer hooks `onWindowClosed` to stop the player and
  release its retained window reference, so the decode pipeline no longer
  keeps playing audio after the window is gone. Replaying a finished clip
  shows video again: `UltraCanvasVideoPlayer::Play()` rewinds to 0 after
  end-of-stream (an EOS-parked pipeline produces no data), and
  `UltraCanvasVideoPlayerElement` no longer stops its frame timer on EOS
  (Play re-arms it), so frame uploads resume instead of leaving a frozen
  surface with audio only.

#### 2026-07-11 *0.3.6*
- Hover video preview for the album widget: resting the cursor on a Video tile
  plays a short muted inline preview of the clip in place of its static poster
  frame (opt-in via `AlbumConfig::videoHoverPreview`, with configurable dwell
  delay, duration, loop, start offset, mute and preview fps). The engine behind
  it is the new reusable `UltraCanvasVideoHoverPreview`
  (`include/UltraCanvasVideoHoverPreview.h`): dwell-delayed muted playback,
  frames delivered as a ready-to-draw `UCPixmap`, self-limiting duration, one
  decode session at a time, and a silent fallback to the static thumbnail with
  the null video backend. The demo's Album page enables it on its video tiles.
  See the "Hover video preview" section in
  `Docs/UltraCanvas/UltraCanvasAlbumExamples.md`.
- Added "jump to last window": the application now keeps a most-recently-used
  window focus history and `JumpToLastWindow()` raises + focuses the window
  used before the current one, restoring keyboard focus to the input field
  that was active there; repeated triggers toggle between the two most recent
  windows. Bindable to a keyboard shortcut and/or a mouse button via
  `SetJumpToLastWindowKey()` / `SetJumpToLastWindowMouseButton()` (disabled by
  default; the demo binds F6 and the mouse Back button). The Linux and Windows
  back-ends now translate the mouse side/thumb buttons (X11 buttons 8/9,
  Windows XBUTTON1/2) as `UCMouseButton::Back`/`::Forward`. Click-to-focus now
  sends proper `WindowBlur`/`WindowFocus` events to the windows involved
  (previously the raw MouseDown event was re-dispatched to both). See
  `Docs/UltraCanvas/UltraCanvasJumpToLastWindow.md`.

#### 2026-07-11 *0.3.5*
- `UltraCanvasListView`: new cell-level callbacks `onCellClicked` and
  `onCellHovered` (row, column, cell-local position) plus the `GetColumnAt()`
  hit-test helper, so delegates can implement per-cell interactive regions
  (links, buttons) in multi-column views. Hover leaves are reported as
  (-1, -1) and the view now also resets its hover state on `MouseLeave`.
- Demo: the Dependencies & Third-Party page is now interactive. Library names
  render as links — hovering underlines them, shows a hand cursor and a
  tooltip with the website / source repository / license; clicking opens a
  popup with "Website" and "Source code" entries (or opens the site directly
  for OS frameworks without a public repository). Each library additionally
  carries its license tag after the name — e.g. (MIT), (LGPL 2.1) — which is
  its own link to the license description page on spdx.org.
  `Docs/Dependencies.md` gained the matching License column and the
  previously missing OCR / Vectorizer / LaTeX plugin libraries.
- Scrollbar: a custom handle image (`thumbImagePath` /
  `thumbImagePathHorizontal`) is no longer stretched to the thumb rectangle.
  The handle is now always scaled preserving its aspect ratio and centered in
  the thumb, so the grip keeps its shape regardless of thumb length. The
  `thumbImageFit` style field was removed accordingly.

#### 2026-07-10 *0.3.4*
- Demo: the "Networking (UltraNet)" page moved from Tools into the
  "ULTRA OS modules ▸ Ultra Net" tree entry and is now presented like the
  FileLoader module page — Overview / Details / Examples tabs, with the live
  remote-resource loader on the Examples tab.
- UltraNet: fixed https:// requests failing with "Problem with the SSL CA
  cert (path? access rights?)". libcurl bakes the CA bundle path of the
  build machine into the library; when that path does not exist on the
  machine the app actually runs on, every TLS request failed. When no
  `UltraNetConfig::caBundlePath` is set, UltraNet now discovers the system
  trust anchors at runtime — `CURL_CA_BUNDLE` / `SSL_CERT_FILE` environment
  overrides first, then the well-known distro bundle locations
  (Debian/Ubuntu, Fedora/RHEL, openSUSE, Alpine/BSD) and the hashed
  certificate directory — and passes them to libcurl.
- Rating: fixed half-step (0.5) values never displaying. `CreateHalfRating`
  applied the initial value before enabling half steps, so it was snapped to
  a whole number (e.g. 3.5 became 4) and the half-filled symbol never
  appeared. Half steps are now enabled before the value is set, so ratings
  like 3.5 render as three full symbols plus a left-half-filled one.
- Implemented GIF (and animated WebP) animation support. Animated images now
  play in `UltraCanvasImageElement` (auto-play on load, with
  Play/Pause/Stop/SetAnimationEnabled control) and in the media viewer, where
  zoom/pan/rotate/mirror apply live to the running animation and colour
  adjustments freeze it on the current frame. Frames are decoded once through
  libvips' multi-page loader into a shared, cached `UCImageAnimation`
  (per-frame delays, loop count honoured, near-zero delays shown at 100ms);
  playback is stepped by the new reusable `UCImageAnimationController` on the
  same main-thread app timer the video player element uses for its frame
  ticks. Multi-page stills (TIFF/PDF) keep displaying as static images, and
  the info popup shows the frame count for animated files. See
  `Docs/UltraCanvas/UltraCanvasAnimatedImages.md`.
- Fixed Tesseract OCR plugin

#### 2026-07-08 *0.3.3*
- ODT reader: real-world letter documents now render their letterhead
  sections. `draw:text-box` frames (sender/contact blocks) are parsed into
  regular blocks instead of being dropped; master-page headers and footers
  from `styles.xml` (bank details, register lines, region-left/center/right
  columns) are emitted before/after the body separated by a rule;
  page-anchored `draw:frame`s directly in the text flow (e.g. signature
  images) are handled; picture hrefs with a `./` prefix load correctly and
  external (linked) pictures are skipped; hidden sections
  (`text:display="none"`) and hidden text no longer leak into the output;
  text boxes anchored inside table cells flatten into line-broken cell text;
  named/automatic list styles in `styles.xml` are now honored.
- Demo: the ODT Documents page now presents the loaded document as a full
  DIN A4 page (794 x 1123 px at 96 DPI) — a white page centered on a neutral
  desk background with letter-like margins; the demo display area scrolls
  to reach the rest of the page.
- Implemented clipboard handling fort TextInput controls
- Merged "UltraCanvas arrow-key value selector"
- Merged "UC eBook renderer issues"
- Merged "UltraCanvas demo treeview fixes"
- Merged "Docusaurus integration for UltraWeb"
- Merged "UltraCanvas ODT rendering gaps"

#### 2026-07-06 *0.3.2*
- Demo: the LaTeX Documents page now typesets every document **live** from its
  `.tex` source through the on-demand UltraCanvas LaTeX engine instead of
  showing a pre-rendered screenshot. Added a set of math-mode example
  documents (`media/LaTex/math-*.tex`) that render live, and removed the
  TikZ / pgfplots and document-mode (`tabular`, `figure`) examples — which the
  math engine cannot typeset — along with their reference images. A reference
  image fallback remains in the demo for any unsupported `.tex` dropped into
  the folder later.

#### 2026-07-05 *0.3.1*
- Merged "ODT/DOCX support for file elements"
- Merged "UltraCanvas text document support"
- Merged "UltraCanvas eBook file support validation"

#### 2026-07-04 *0.3.0*
- Implemented UltraNet networking module — full v1.0 master-registry surface
  plus v1.1 extensions (`UltraNet/UltraNetCore.h`, `UltraNetHttp.h`,
  `UltraNetUrl.h`, `UltraNetWebSocket.h`, `UltraNetFtp.h`, `UltraNetSocket.h`,
  `UltraNetTls.h`, `UltraNetDns.h`, `UltraNetCookies.h`, `UltraNetPlugins.h`,
  `UltraNetSse.h`)
- HTTP / HTTPS sync + async via libcurl multi-handle worker thread; HTTP/3
  (QUIC) via nghttp3 when libcurl was built with it
- WebSocket client (libcurl native `curl_ws_*`; runtime capability check
  with a clear error when libcurl was built without `--enable-websockets`)
- FTP / FTPS / SFTP download / upload / list / delete / rename / mkdir /
  rmdir; rich listings via MLSD (with a UNIX `ls -l` fallback)
- Raw TCP / UDP sockets (POSIX sockets / Winsock)
- TLS wrap on raw TCP — OS-native backends: OpenSSL (Linux),
  Schannel (Windows, `SCHANNEL_CRED`), SecureTransport (macOS) — no extra
  TLS deps on Windows / macOS
- DNS: A / AAAA / PTR via getaddrinfo; MX / TXT / SRV / NS / CNAME / SOA via
  libresolv (Linux/macOS) and dnsapi (Windows); optional async c-ares
  backend (`ARES_OPT_EVENT_THREAD`) covering the full record set
- Sessions with `CURLSH`-backed cookie + connection-pool sharing
- Plugin system with `IUltraNetPlugin` + seven specialised category
  interfaces (mail, messaging, remote-access, directory, streaming,
  file-share, RPC); v2 host-vtable DSO contract (`UltraNet_PluginInit`) with
  v1 (`UltraNet_PluginRegister`) fallback; dynamic DSO loading via
  dlopen / LoadLibrary
- **All 17 spec plug-ins ship** in `Plugins/UltraNet/`:
    - Mail: SMTP · IMAP · POP3
    - Messaging: MQTT · AMQP
    - Remote access: SSH · Telnet
    - Directory: LDAP
    - Streaming: RTSP · RTMP · RTP (in-tree RFC 3550 receiver) · SIP
      (in-tree RFC 3261 UDP)
    - IoT: CoAP · SNMP
    - Discovery: mDNS (Avahi / Bonjour / DnsQuery_W)
    - Web modern: gRPC · WebDAV
- SSE / chunked HTTP streaming (`UltraNet_SseStream` + parser) — for
  token-by-token LLM responses
- Per-request progress callbacks alongside the global transfer-callbacks bag
- Streamed HTTP upload from disk (constant memory)
- `UltraCanvasApplicationBase::PostToUIThread(std::function<void()>)` for
  marshaling network completions back to the UI thread from background
  worker threads
- `UltraCanvasFileLoader::LoadFile(pathOrUrl)` now dispatches `http://` /
  `https://` URLs to UltraNet automatically
- Networking demo screen in the demo app (Tools → Networking) — loads a
  remote image via `UltraCanvasFileLoader::LoadFile(url)`
- UltraNet test suite (`Tests/UltraNet/`, 102 tests, in-tree framework, CI
  wired via `ULTRACANVAS_BUILD_NET_TESTS=ON`)

#### 2026-06-26 *0.2.32*
- Implemented HiDPI and scaling for all platforms
- Merge "LaTeX document renderer for UltraCanvas"
- Merge "OCR and vectorize solution for UltraCanvas"
- Merge "UltraCanvas heatmap demo presets"
- Merge "Media viewer widget for UltraCanvas"
- Merge "UltraCanvas Gauges demo fixes"

#### 2026-06-26 *0.2.31*
- Merge "UltraCanvas popup changelog link"
- Merge "UltraCanvas Gauge demo layout"
- Merge "UltraCanvas Slider demo layout"

#### 2026-06-26 *0.2.30*
- Demo: moved the **Waveform Chart** example out of *Audio Elements* and into the *Charts* category, where it belongs alongside the other data visualizations (a waveform is an amplitude-over-time plot, not a structural diagram).
- Waveform: added a **Display range** control to the waveform demo and a backing `SetVisibleWindowSeconds()` API on `UltraCanvasWaveformElement`. The view can now show the whole track ("All audio") or a trailing window that scrolls with the playhead — "Last 10 seconds" or "Last 60 seconds". Rendering, click-to-seek and the playhead all map to the visible window.
- Merge "UltraCanvas Album demo optimizations"
- Merge "UltraCanvas Gauge layout overlap"

#### 2026-06-24 *0.2.29*
- Merge "Waveform chart UltraCanvas integration"
- Merge "UltraCanvas Heatmap demo optimization"
- Merge "UltraCanvas treeview auto-scroll"
- Merge "Module infos MD diagram/image viewer"
- Changelog link on startup info page
- Merge "UltraCanvas demo Modules info pages"

#### 2026-06-24 *0.2.28*
- Fixed crash in the UltraCanvasDependenciesExamples demo screen
- In the UltraCanvasListView change model pointer to shared_ptr instead of raw pointer, prevent possible crashes

#### 2026-06-23 *0.2.27*
- Fixes Video Player issues (freeze video/audio mainly in Linux)
- Fix Video Player layout issues, wrong aligned text, use icons instead of manual drawing

#### 2026-06-23 *0.2.26*
- Docs: audited every implemented demo element for a wired-up Programmer's Guide / example doc and filled all the gaps. Added five new guides — `UltraCanvasScrollbarExamples.md`, `UltraCanvasSlideshowExamples.md`, `UltraCanvasQRCodeExamples.md`, `UltraCanvasSpreadsheetExamples.md` and `UltraCanvasXARExamples.md` — each documenting the real public API (no invented symbols) with runnable examples drawn from the matching demo source.
- Demo: wired the **C++ source** and **documentation** header icons for the elements that were missing them — Scrollbars, Spreadsheet, Slideshow, QR code and XAR now point at their example `.cpp` and new `.md`; Video and Audio now point at their existing `UltraCanvasVideoExamples.cpp`/`UltraCanvasVideo.md` and `UltraCanvasAudioExamples.cpp`/`UltraCanvasAudio.md`; and Album now links its existing `UltraCanvasAlbumExamples.md`.

#### 2026-06-23 *0.2.25*
- Gauges: added a Programmer's Guide (`Docs/UltraCanvas/UltraCanvasGaugeExamples.md`) covering the full mode-driven API — all 17 `GaugeMode`s, the round-gauge (CircularRing) style system, decorations (ranges/thresholds/external pointers/sub-dial), live clock & stopwatch controls, and a runnable code example per gauge family.
- Demo: the Gauges demo page now shows the **C++ source** and **documentation** header icons (wired its `demoSource`/`demoDoc` to `UltraCanvasGaugeExamples.cpp` and the new guide), matching every other element, plus the four tab variants (Round Gauges, Progress & Linear, Specialized, Analog) in the tree.

- Fix Windows video playback: the player loaded a file and played audio but showed no picture, and the play/pause/stop transport misbehaved (multiple clicks to stop, no resume after pause). Media Foundation's sample-grabber video sink rejected the session's rate control (`MF_E_UNSUPPORTED_RATE`) and never delivered a single frame, which also corrupted the session and scrambled the transport state. `MFDecodeSession` now decodes video through an `IMFSourceReader` (RGB32, advanced video processing) paced to the audio clock, with an audio-only Media Session providing sound + the master clock (video-only files fall back to a wall clock). Also force opaque alpha on MF RGB32 frames — the unused "X" byte was 0, which rendered fully transparent in the premultiplied Cairo `ARGB32` pixmap. Linux/GStreamer playback is unchanged.
- Video: added a cross-platform thumbnail / poster-frame API (`UltraCanvasVideoThumbnail.h`). `CaptureVideoThumbnail()` returns a single decoded frame, `CaptureVideoThumbnailPixmap()` a ready-to-draw `UCPixmap`, and `SaveVideoThumbnail()` writes a file (encoder chosen from the extension: `.qoi` → QOI, otherwise PNG — both need no libvips), e.g. for `UltraCanvasAlbum` `thumbnailPath`. Each takes a `VideoThumbnailRequest` (target time — or an automatic position — plus optional aspect-preserving `maxWidth`/`maxHeight`).
- Video: new opt-in backend capability `IVideoBackend::GrabThumbnail()`. The GStreamer (Linux) backend implements it as a throwaway `uridecodebin` pipeline that prerolls to PAUSED, seeks accurately and pulls one preroll sample (no audio, no full playback). Backends without it (null / Media Foundation / AVFoundation) use a generic decode-session fallback, so thumbnails work wherever decoding does.

#### 2026-06-23 *0.2.24*
- Merge "Color picker widget for UltraCanvas" and fix layout errors.
- Fix toolbar button width (make it auto)
- Fixed incremental search in TextArea (stop advance on each typed matched character)
- Refactor Audio element. Use composite widget instead manual draw. Use SVG icons for play/pause/etc.. buttons
  
#### 2026-06-21 *0.2.23*
- `UltraCanvasGLSurface` now resizes its render target / framebuffer to follow the element's actual bounds on every render, however the bounds were changed. Previously the framebuffer size (`surfaceWidth_`/`surfaceHeight_`) was only updated from the `SetBounds` override, so a layout-driven resize — flex/grid stretch, a parent resize, `SetElementSize`, a window resize — left the GL content stuck at its old size (it wrote `finalBounds` without routing through `SetBounds`). `Render()` now syncs the framebuffer size from `GetLocalBounds()` and forces a content re-render that pass, so GL surfaces resize correctly under any layout path (this is what made the Shaders-tab "maximize" need an explicit `SetBounds`; flexible/maximized GL surfaces now grow on their own).

#### 2026-06-21 *0.2.22*
- Fix the "maximize canvas" control in the OpenGL 3D Showcase "Shaders" tab. Three issues: (1) the toggle button was pinned to the tab content-area's right edge instead of the canvas's top-right corner, so once maximized it sat far from the (un-grown) canvas; (2) it showed a "⛶" glyph the demo font lacks (rendered as "…"); (3) clicking it did not enlarge the canvas. The canvas was being resized via `SetElementSize`, which only sets a CSS dimension — but the tab content is absolutely positioned (no layout pass re-applies it) and, crucially, `UltraCanvasGLSurface` only grows its GL framebuffer from its own `SetBounds` override. The maximize now resizes the surface with `SetBounds` (so the framebuffer actually grows to fill the tab), pins the button to the surface's current top-right corner in both states, and renders the new `media/icons/maximise.svg` icon (drawn as a white mask on the dark translucent button) instead of the missing glyph.

#### 2026-06-21 *0.2.21*
- Fix the per-effect parameter-slider captions ("Brightness", "Splat radius (px)", etc.) being partially obscured in the OpenGL 3D Showcase "Shaders" tab control panel. The sliders use an always-on `Number` value display, which `UltraCanvasSlider` draws just above the track — and since the slider pins its track to the bottom `handleSize` (16px) of its bounds, a short (22px) slider drew the value text ~10px above its own top edge, on top of the caption placed above it. The four per-effect slider groups (Ball Surface, Pulse, Fragments, Circles) now use 36px-tall sliders (handle + a value-text row + margin) and re-spaced rows, so each value number sits inside its slider below the caption with no overlap.

#### 2026-06-21 *0.2.20*
- Curated the OpenGL 3D Showcase "Shaders" tab effect list. Removed four effects together with their GLSL/source, formula headers, per-effect uniforms, state fields and parameter-slider groups: "Warp Starfield", "Rössler Attractor", "Mandala (12-wave)" and "ULTRA OS Logo". Reordered the remaining list so the three Twigl one-liners "Horizon", "Protostar2" and "Plasma Orb" appear first. The info text and `Docs/UltraCanvas/UltraCanvasGLSurfaceExamples.md` were updated to match; effects with live sliders are now Ball Surface, Pulse, Fragments and Circles.

#### 2026-06-21 *0.2.19*
- Fix the OpenGL 3D Showcase demo showing horizontal and vertical scrollbars inside every tab even when the window had room. The tabbed container was `980×690` with a 34px tab bar, so each tab's content area was only `980×656` — smaller than the tab contents, whose control panel reaches x≈986 and whose Shaders source viewer reaches y≈680. Since each tab's root is resized to the content-area bounds (`autoShowScrollbars` defaults on), the small overflow forced scrollbars. The showcase container (`1024×800`) and tabbed container (`1004×726`, content area `1004×692`) were enlarged so the content area clears every tab's children with margin; the three tab roots were updated to match. No scrollbars now appear in any tab.

#### 2026-06-20 *0.2.18*
- Breadcrumb: added a `Parallelogram` item style (`BreadcrumbItemStyle::Parallelogram` + `BreadcrumbStyle::Parallelogram()` preset) — interlocking slanted/skewed segments (outer edges of the first/last segment stay vertical) sharing the `arrowSize` skew depth with the Arrow style.
- Breadcrumb: added an optional **level indicator** — a leading numbered badge per item. `BreadcrumbStyle::showLevelIndicator` enables it; `levelIndicatorBackground` selects the badge background (`Round`, `Rectangle`, or `NoBackground`); `levelIndicatorBorder` outlines it; plus `levelIndicatorSize`/`levelIndicatorColor`/`levelIndicatorTextColor`/`levelIndicatorBorderColor`/`levelIndicatorBorderWidth`. The new `BreadcrumbStyle::Steps()` preset combines Arrow segments with round numbered badges (dark "wizard step" strip). Works with any item style.
- Demo: added "15. Numbered steps", "16. Parallelogram", and "17. Level indicators" (round / rectangle / none / bordered) rows to the breadcrumb demo page.

#### 2026-06-20 *0.2.17*
- Breadcrumb: added an `Arrow` item style (`BreadcrumbItemStyle::Arrow` + `BreadcrumbStyle::Arrow()` preset) — interlocking right-pointing arrow/chevron "steps". Each segment grows a pointed tip past its right edge that nests into the next segment's matching left notch (the first segment is flat-left, the last one's tip trails off), with the current step highlighted. New `BreadcrumbStyle::arrowSize` controls the tip/notch depth. Added a "14. Arrow steps" row to the breadcrumb demo page.

#### 2026-06-20 *0.2.16*
- Fix the Breadcrumb demo: item text was not vertically centered within the strip/pills. The element hand-rolled its vertical centering as `centerY - (int)textHeight / 2`, truncating the half-height to an integer (drifting the glyphs ~1px off the center line shared by the separators and icons) and, more importantly, never routing through the text layout's automatic centering — so it never compensated for the font's top line-leading. On fonts that split external leading above the baseline (e.g. Segoe UI on Windows) this pushed the visible text several pixels low. Breadcrumb item/overflow text now centers via the text layout's `VerticalAlignment::Middle` over the slot height (full sub-pixel precision), and `UCTextLayout::GetLayoutVerticalOffset` re-enables the top-leading compensation for Middle alignment, computed in Pango units to keep the sub-pixel offset (a previous int-pixel version had regressed Segoe UI 12pt). The compensation is a no-op where `baseline == ascent` (DejaVu/FreeSans on Linux), so other platforms are unaffected.

#### 2026-06-19 *0.2.15*
- Fixes Gstreamer build in Linux
- Fixes Gstreamer (video player) and native dialogs crash in Linux
- Merged "Add Radar Chart element"
- Fix Radar Chart rendering and animation
- Show ULTRA OS overview page when "ULTRA OS modules" tree node is selected
- Merge "Add rounded corner label examples and fix resource paths"
- Merge "Add digital clock, segmented ring, centre content, and faded colours to gauges"

#### 2026-06-19 *0.2.15*
- Added intro description for different Modules

#### 2026-06-19 *0.2.14*
- Merge "PDF Text demo page layout" fix
- Merge heatmap implementation
- Merge "Shader graphics for demo" (more shader examples)
- Merge "UltraCanvas module demo content"

#### 2026-06-18 *0.2.13*
- Fix the Gauge element's linear/progress "rounded bar" rendering at low values. `RenderLinearBar` drew the value fill with a corner radius of half the bar's thickness regardless of the fill length. `FillRoundedRectangle` does not clamp the radius, so once the fill became shorter than the bar's thickness (roughly under 10% on a wide bar) the four corner arcs overlapped and collapsed the fill into a perfect circle. The radius is now clamped to half of the fill's smaller dimension, so short fills render as a proper pill that stays inside the track.

#### 2026-06-17 *0.2.12*
- Fix two bugs in the OpenGL "Zarch" 3D demo:
- The application could not be closed while an animated (Continuous) GL surface was on screen, and the debug log kept spinning. `UltraCanvasGLSurface::Render` re-posted a full-window `Redraw` event every frame; it now invalidates only the surface's own region and stops re-arming once the window is closing/closed/hidden, so sibling widgets no longer repaint at the animation frame rate and the event loop can shut down.
- The release build crashed when opening the Zarch tab (before any 3D was drawn) while the debug build did not. The terrain/tree hash (`Hash2`) multiplied signed `int`s past `INT_MAX`, which is undefined behaviour that an optimised (`-O3`) build is free to miscompile; it now uses well-defined unsigned arithmetic. The Models/Shaders tabs do not use this hash, which is why only Zarch was affected.
- Merge "Groupbox border alignment bug" fix
- Merge "DatePicker demo layout bug" fix
- Merge "Spreadsheet save button with format options" fix
- Merge "Texter unsaved tab indicator bug" fix
- Merge "UltraCanvas audio layout improvements"

#### 2026-06-17 *0.2.11*
- Spreadsheet demo: added a "Save…" button to the toolbar that offers every format the engine can write (OpenDocument `.ods`, `.csv`, `.tsv`). Choosing a CSV/TSV name opens a new "Text Export" options dialog (character set, field separator, text delimiter, quoting policy, line ending, optional BOM) with a live text preview; `.ods` saves directly.
- Spreadsheet engine: added `SaveCSVWithOptions`/`ExportCSVToString` plus a `CSVExportOptions` struct and a `CSVEncodeFromUtf8` charset encoder (UTF-8/UTF-16/Latin-1/Windows-1252, optional BOM). Fixed `.tsv` saving to actually use a tab separator.
- Merge "PDF support", implemented PDF demo (viewer in the demo app)
- Merge "Datepicker multi-month display"
- Merge "UltraCanvas album widget improvements"

#### 2026-06-15 *0.2.10*
- Fix the spreadsheet component, editing, keys navigation, cell size changing, scrolling, loading files

#### 2026-06-14 *0.2.9*
- OpenGL surface support enabled on Windows: implemented the WGL context manager (hidden helper window + legacy-context bootstrap to load `wglCreateContextAttribsARB`, then a requested core/compatibility context). Modern GL entry points are resolved via GLEW (`mingw-w64-x86_64-glew`), since `opengl32.dll` only exports OpenGL 1.1. `ULTRACANVAS_ENABLE_GL` now defaults ON for Windows when GLEW is found.
- OpenGL surface support enabled on macOS: completed the CGL context manager (honors the requested GL version/profile and color/depth/stencil config, real extension querying via `glGetStringi`) and let `ULTRACANVAS_ENABLE_GL` default ON for macOS as well as Linux.
- Merged with the "UltraCanvas date picker widget" code
- Merged with the "UltraCanvas Album widget" code
- Merged with the "UltraCanvas Demo Slideshow layout" code
- Merged with the "UltraCanvas QR code demo page" code

#### 2026-06-12 *0.2.8*
- Slideshow demo: reworked the options panel into a labelled-row grid (label column on the left, wrapping option buttons on the right) grouped Controls / Indicator / Indicator edge / Fade style / Panel layout / Image / Letterbox fill. Each group now behaves like a radio with the active choice highlighted, the panel-layout split/overlay/off positions are grouped under sub-labels, and crop focus dims unless the Cover fit is selected
- Slideshow demo: retitled the page to "UltraCanvas Slideshow widget" and removed brand-specific references throughout the slideshow widget and its demo
- Slideshow demo: framed the live widget with a captioned "Slideshow widget" box and added a "Slideshow widget options" heading above the controls, so it's clear which part is the actual widget and which are programmer-facing options
- Slideshow: the widget now takes keyboard focus on click and supports manual navigation with the arrow keys — Left/Down go to the previous slide, Right/Up to the next (numpad arrows too)

#### 2026-06-09 *0.2.7*
- Merged and fixed the "# Spreadsheet support for UltraCanvas"

#### 2026-06-09 *0.2.6*
- Merged and fixed the "OpenGL 3D showcase demo"

#### 2026-06-09 *0.2.5*
- Slideshow: added comprehensive info-panel layouts via `SlideshowInfoLayout` — split on any of the four sides (`SplitLeft/Right/Top/Bottom`), edge overlays on the image (`OverlayLeft/Right/Top/Bottom`), corner overlays (`OverlayTopLeft/TopRight/BottomLeft/BottomRight`), `OverlayCenter`, `OverlayFull`, and `Hidden`
- Slideshow: indicators can now hug any edge via `SlideshowIndicatorEdge` (Top/Bottom rows, Left/Right stacked columns)
- Slideshow: added `SlideVertical` and `ZoomFade` transition styles
- Slideshow: configurable image fitting for mismatched images — `imageFit` (Cover auto-crop, Contain, Fill, ScaleDown, NoScale), an `imageFocus` focal point that picks which part survives a crop, and `gapFill` for letterboxed images (background color, dedicated letterbox color, or a zoomed image backdrop)
- Slideshow demo: added pickers for info-panel layout, indicator edge, image fit, crop focus and letterbox fill, plus the new transitions

#### 2026-06-07 *0.2.4*
- Fix QRCode examples page (fix wrong character and change the default QR Code generation)

#### 2026-06-07 *0.2.3*
- QR code decoder fixed (installed missing lib and configured github build)
- Merged branch "UltraCanvas QR demo field visibility bug"
- Merged branch "Barcode widget for UltraCanvas" and fix barcode widget and its demo

#### 2026-06-04 *0.2.2*
- Added Gauges, Compositor diagram, QR code
- Fixed Tabbed container and inner tabs layout
- Implemented SortChildNodes method and autoSortChildren property in the TreeView
- Fix label layout resize bug
- Make Arc diagram and Architectural Adjacency Diagram use Grid layout instead of fixed elements positions

#### 2026-06-03 *0.2.1*
- Major update. Implemented CSS Flex/Grid/Absolute layout support.
 
#### 2026-05-20 *0.1.39*
- Show cursor and allow selection in the TextArea in read-only mode
- Autodetext syntax highlighting rules by filename with auto fallbask to extension
- Fix possible bug in menu and tooltips rendering (wrong color)

#### 2026-05-19 *0.1.38*
- Implemented the SplitPane element
- Fix bug when scrollbar of outer container overlap with inner container's elements or scrollbar (mouse events incorrectly goes to inner container instead of outer container's scrollbar)
- Implemented the Barcode widget with 1D symbology encoders: Code 39 / 39 Extended / 93 / 128 (A/B/C/auto), GS1-128, EAN-13/8, UPC-A/E, ISBN-13, ITF, ITF-14 (with bearer bars), Standard 2 of 5, Codabar, MSI Plessey (4 check-digit modes), Pharmacode. From-scratch C++20 encoders, no external dependencies; live editor + gallery in Tools → Bar code

#### 2026-05-18 *0.1.37*
- Arc diagram improvement
- Change layout of "Bitmap elements" screen in the Demo app
- Use TextArea instead of Markdown element in the "Text Document/Markdown" screen in the Demo app
- Replace DejaVu default embedded font by Ubuntu font

#### 2026-05-17 *0.1.36*
- Change the Breadcrumbs element demo
- Make more Docs for different controls 
- Added Slideshow example in Widgets/Slideshow section of Demo app
- Fix for Pie Chart labels

#### 2026-05-15 *0.1.35*
- Remove JXL from Image Performance test as JXL format does not supported by currently used libvips
- Fix buttons size to fit text
- Fix bug when element is deleted but capturing the mouse or focused then app may crash
- Implemented Breadcrumbs element demo

#### 2026-05-15 *0.1.34*
- Use TextArea to show Markdown info in the Modules section
- Add more modules description to Modules section
- Implement Breadcrumb demo
- 
#### 2026-05-14 *0.1.33*
- Implemented new Image performance demo
- Fix problem with AltGr+key in Windows
- Fix ordered list content offset in MD-mode in TextArea
- Implemented Pie Chart element
- Implemented Adjacency Diagram element
- Implemented Arc Diagram element
- Implemented Breadcrumb element

#### 2026-05-12 *0.1.32*
- Implemented UltraCanvasFileLoader
- Implemented OS Recent files support (add opened files to OS Recent files list)
- Fix wrong calculation of mouse coordinates and bounds in the some diagrams 

#### 2026-05-10 *0.1.31*
- Fixed font rendering, now Windows and Linux will rendered using same included DejaVue fonts

#### 2026-05-09 *0.1.30*
- Implemented more different diagrams
- Implemented JitterChart
- Attempt to fix menu crash on MacOS

#### 2026-05-06 *0.1.29*
- Refactor Checkbox code, split to Checkbox/Redio/Switch
- Implemented more visual styles for Switch
- Attempt to fix crash on MacOS

#### 2026-05-06 *0.1.28*
- Implement support tooltips for menu itmes
- Implement maxWidth option for menu and ellipsize mode for menu items

#### 2026-05-04 *0.1.27*
- Attempt to implement MacOS HiDPI support

#### 2026-04-30 *0.1.26*
- Fix cursor position in Markdown mode in TextArea

#### 2026-04-28 *0.1.25*
- Major rework in the UltraCanvas rendering, implemented optimized rendering.
  Now popups/tooltips rendered in own surfaces (does not need to repaint main content after show/hide)
  Implemented partial rendering using dirty rectangles (for any content) 

#### 2026-04-23 *0.1.24*
- Fix tooltips rendering

#### 2026-04-23 *0.1.23*
- Some color fixes for Dark mode

#### 2026-04-20 *0.1.21*
- Refactored elements rendering/events coordinate system to use element-based coordinates where 0,0 is
  top-left element's corner instead of container-based where 0,0 was container top-left corner.
  Set clipping to element's bounds and save/restore state in container's rendering loop instead rely on element Render().
  Don't render invisible elements (hidden by scroll position)

#### 2026-04-20 *0.1.20*
- Shard very long lines to speed up TextArea on big files.
  Lines longer than 4000 codepoints are split at a break char
  (space/tab/punct) or force-split at 12000 during SetText.
  Known problems: 
    if line has no break chars (very rare case) it will splitted at 12000 char boundary,
    attempt to glue that lines (backspace or delete) will cause reshard (resplit) again
    and it will splitted at same boundary, visually it will looks like delete/backspace did not work 
- Show current line marker (red box) for cursor position
- Calculate and show real logical line numbers for split lines. 

#### 2026-04-17 *0.1.19*
- UpdateGeometry for visible childs in container only

#### 2026-04-16 *0.1.18*
- Revert back to the Sans font for Windows insetad of detecting default font. It detected the "Segoe UI" and this shit font can't be vertically centerted without special patches especially for that font, it always shifted down a little (even in browsers)
- Fixed bug with high CPU usage and slow cursor movement on the big files with very long lines.

#### 2026-04-16 *0.1.17*
- Add on-the-fly submenu regeneration to UltraCanvasMenu
- Fix Windows high CPU usage when app is idle.
- Fix main-row digit/punctuation key mappings on Linux and macOS
- Fix vertical text position issues in Windows (was shifted down a little)

#### 2026-04-16 *0.1.16*
- Full rework of display and rendering the text. Use Pango layout to format text. Allow to use variable height lines

#### 2026-04-06 *0.1.15*
- Add platform-native system font detection, replace hardcoded "Sans" defaults

