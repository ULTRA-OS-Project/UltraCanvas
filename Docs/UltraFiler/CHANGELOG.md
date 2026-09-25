#### 2026-09-25 *1.53.0*
- **Find text: Match case and file patterns.** *Extras > Find text* opens a
  dialog of its own instead of the one-line prompt: besides the text it asks
  **In files named** - patterns such as `*.cpp; *.h` (`*`, `?`; separated by
  `;`, `,` or blanks; empty for every file), checked before a file is opened,
  so a narrowed search reads only the files it names - and **Match case**,
  which compares the text byte for byte. The dialog opens with the previous
  search's entries, and the status line names the patterns and the case
  option along with the text.

#### 2026-09-24 *1.52.0*
- **Every source-text type shows its miniature page, and has its own switch.**
  Swift, SQL, Rust, Ruby, PHP, Lua, Kotlin, Java, Go, C#, CSS, Pascal and the
  assembler files (asm, arm, 68k, ...) drew a blank sheet on their thumbnail
  tile, while cpp, py, js and yaml showed the first lines of their text. The
  file display only knew 19 text types. It now takes every extension the
  syntax highlighter knows, so all of them show their text. **Settings >
  Display > Thumbnails > Text** lists each one as a checkbox, so single types
  can be switched back to the plain icon.
  - **R, Scala, MATLAB and VBA files show their text too.** Their languages
    were written into the syntax highlighter but switched off, so `.r`,
    `.rmd`, `.scala`, `.sc`, `.sbt`, `.m`, `.vba`, `.cls` and `.frm` files
    still drew the blank sheet. The highlighter now has them switched on,
    so each of them shows its miniature page, gets a switch under Text, and
    is named after its language ("MATLAB Text"). `.bas` stays BASIC.
  - **The Thumbnails and Detail view settings pages could not be scrolled.**
    Since containers stopped scrolling unless asked (framework 0.9.x, the same
    day as 1.45.0), those two pages were cut off at the window's foot with no
    scrollbar and no wheel scrolling. Every kind below Vector graphics - 3D,
    PDF, Text, Docs, Spreadsheets, Videos, Audio, Fonts - could not be reached.
    The pages opt in to scrolling again.
#### 2026-09-24 *1.51.0*
- **Delete moves to the Trash now, and asks which it should be.** The delete
  confirmation offers **Move to the Trash** and **Delete permanently** as two
  radio buttons; Del opens it on the trash, Shift+Del on the permanent delete,
  as in Explorer. (The framework side - the trash itself and the dialog - is
  in the UltraCanvas changelog.)
  - The toolbar's delete button asked "Delete X?" in a box of its own and then
    the widget asked again. It now opens the widget's confirmation directly.
  - The folder tree's **Delete** used its own "Delete X and everything in it?"
    box and always deleted for good; it now opens the same confirmation, with
    the trash choice and the preview of what the folder holds.
- **An open Settings window follows the file display's context menu.** The
  menu's *Display > Thumbnails*, *Detail view*, *File extensions*, *File
  icons* and *Folder previews* write straight into the settings, but the
  Settings pages read them only once, when the window was built. A
  Settings window left open kept showing, for example, *Bitmaps* ticked on
  the Detail view page after a click in the menu had switched the detail
  pane off for every JPG and PNG (`display.detailview.kinds.off = bitmaps`
  in config.ini). The window now re-reads those settings whenever the menu
  changes one: the kind and format ticks of both pages, the extension and
  badge choices, the file-icon choice and the folder-previews box.
- **Scan sub folder found nothing inside hidden folders, and did not say
  so.** Searching "UltraFiler" from `C:\Users\<name>` came back with "No
  entries", although `AppData\Roaming\UltraFiler` is right there: `AppData`
  is a hidden folder, and the walk skipped every hidden entry and never went
  into it. (The same happens under `.config` on Linux.) The search matches
  folders as well as files; it was the hidden folder on the way that hid the
  result.
  - The search now follows the display's **Show hidden files** setting
    (Settings > Display > Files, or the context menu's *Display > Hidden
    files*): with hidden files shown, hidden folders are searched and hidden
    matches listed.
  - With them hidden, the walk still leaves them out, as Explorer does, but
    says so. The status line ends with *1 hidden folder was not searched
    (AppData)*. When nothing was found, the middle of the display says the
    same and where to turn hidden files on, instead of a bare "No entries". A
    search with no match and nothing left out says *No match for "X" in N
    folders.*
- **An archive extracted only in part says which entries it skipped.**
  Extraction now refuses entries that would land outside the destination
  folder (see the UltraCanvas changelog). The status line used to say only
  "Extraction failed for X", even though everything else had been unpacked.
  An **Extraction Incomplete** dialog now lists every skipped entry and why.
#### 2026-09-24 *1.50.0*
- **Files can be taken off a drive now, and the status line counts them down.**
  1.49.0 gave every job on a drive a line in the status strip and a progress
  bar, and FTP learned to report its bytes in both directions - but only one
  direction existed. There was no way to fetch a file off a drive at all, so
  the download half of that reporting had nothing to describe. Dragging
  entries from a drive onto a local folder now copies them down, and says so
  the same way an upload does: `Downloading "clip.mp4" - 3.2 MB of 8.0 MB`,
  with the bar in the status strip following the bytes and sliding when the
  server did not say how big the file is.
  - **Two places take the drop**: a local folder row in the tree, and the
    folder a display is showing. Both used to hand the dragged
    `ultracloud://` path to the local move machinery, which asked
    `std::filesystem` about a path no disk has and did nothing; a drop that
    carries entries from a drive and files from this computer at once is now
    split, and each half goes its own way.
  - **A download never overwrites.** The name is settled before the request
    goes out, using the same rule the display's "Keep both" paste uses, so a
    file already in that folder is left alone and the copy lands beside it as
    "name (2)".
  - **A folder is refused, not half-fetched.** A tree is not one transfer, the
    same way it is not one upload; the entry's own listing is still cached, so
    saying so costs a lookup rather than a request.
  - Each file is one queued job, so a drop of five reports `(4 more queued)`
    and the folder they land in is refreshed once each has arrived.

#### 2026-09-23 *1.49.0*
- **The status line says what a drive is doing, and a transfer gets a bar.**
  1.46.0 gave the folder area its own message while a listing is on its way.
  This is the other half: the status strip, and the jobs that are not
  listings. A drive is the one place in this file manager where the answer to
  "why is nothing happening?" is "a server is thinking about it", and an
  upload, a delete or a rename still said nothing at all - and nothing
  anywhere said how far a transfer had got. Every job the drives run now
  reports itself as it starts -
  `Opening "Videos" - receiving folder data...`, `Uploading "clip.mp4" - 3.2 MB
  of 8.0 MB`, `Deleting ... on the drive` - with what is still queued behind it
  (`(2 more queued)`), so a drop of five files does not read as one.
  - **The status strip is a row now**, the line of text on the left and the
    framework's progress bar - `UltraCanvasGaugeDiagramElement` in
    `GaugeMode::LinearBar` - on the right. The bar is there only while a file
    is actually moving: a listing is one round trip with nothing to count, and
    an empty bar sitting in the status line at all times is furniture rather
    than information.
  - **A server that does not say how big the file is** gets "3.2 MB sent" in
    the words and the gauge's indeterminate slide in the bar: there is no
    progress to draw, but the transfer is running and the bar says so. The
    slide is the gauge's own timer, so it keeps moving between the byte
    reports rather than freezing whenever a chunk is in flight.
  - **The report is said before the job runs, not after.** The whole point is
    to fill the wait, and a report that arrives with the answer fills nothing.
    Bytes are thinned to one report every 80 ms on the way to the UI thread -
    libcurl counts bytes, not milestones - with the first and last of each job
    forced through.
  - It is said whatever the window is showing: the History view, the Computer
    page, another tab. What a server is doing is the one thing in this window
    the user cannot see for themselves.
  - The drives share the process-wide UltraNet transfer callbacks while a
    transfer runs and put the previous ones back afterwards. An upload is no
    reason to deafen the rest of the application.

#### 2026-09-23 *1.48.0*
- **An FTP drive's folders appear under it in the tree.** A remote drive was a
  leaf: it had no expand button, and opening it listed its folders on the right
  while its row in the tree stayed empty. The tree deliberately refused to go
  further, because a remote folder's children have to be fetched from a server
  and the tree cannot wait on one while it paints.
  - **It no longer has to wait.** The drives already keep a listing cache whose
    misses are filled by a worker (that is how the folder display shows a
    server without blocking), so the tree reads the same cache: a hit fills the
    row now, a miss only queues the fetch, and the arriving listing - the very
    one that refreshes the display - fills the row when it lands. Opening a
    drive is therefore enough to make its folders appear beneath it, and
    expanding the row without opening it works too.
  - **Folders inside a drive expand in turn**, down as far as the server goes,
    each one fetched only when it is asked for.
  - **The expand button is offered rather than probed for.** Asking whether a
    remote folder has subfolders means listing it, which would be one round
    trip per row before the user has asked to see any of them; the button is
    there from the start and withdrawn again when the listing turns out to
    hold no folders.
  - **The rows follow the drive.** A folder created, renamed or deleted on it
    updates them, a folder that is gone from the server leaves the tree with
    its subtree, a drive that is removed takes its rows with it, and the tree
    follows the display into a remote subfolder once the row to select exists.
  - **A remote path never reaches the local reconcile.** `RefreshTreeFolder`
    answers `is_directory` for the folder it is given; for a path on a server
    that is "no", and the row - with everything under it - would have been
    dropped from the tree the moment anything changed inside it.

#### 2026-09-23 *1.47.0*
- **Drop files onto a remote folder in the display to upload them, too.**
  1.46.0 let a drop on the drive's tree row upload; now a drop onto the
  folder shown in a display does the same - from another program, or from
  the other display of the split view. The status bar says how many files
  are on their way to which drive and folder, and what was left out and why
  (a folder, a remote entry). Moving or copying between two places on the
  same drive is still not offered, and says so. Needs the framework change
  that adds the widget's `remoteUpload` hook.

#### 2026-09-23 *1.46.0*
- **A remote folder being fetched shows progress, not "Folder is empty!".**
  Opening a folder on an FTP, SFTP or cloud drive now shows a turning progress
  ring with "Loading folder" and a line about what is going on: "Waiting for
  Backup NAS - 2 requests ahead" while the fetch is queued behind other
  requests, then "Connecting to Backup NAS (ftp://nas.local) and reading
  /photos", with a seconds count once the server keeps it waiting. The line
  updates as the fetch moves along; the listing replaces it when it lands.
  A folder the server refused shows the reason (a rejected login, an
  unreachable host) in the folder area instead of an empty folder. Needs
  framework 0.9.41 (the widget's `remoteListingStatus` hook).
- **Split view: the docked tree's width moves with the tree.** Docking the
  folder tree into a pane widens that pane by the tree's width at the other
  display's expense, and undocking it (or docking it on the other side) gives
  that width back - the display beside the tree used to stay squeezed after
  the tree had gone. The "Folder tree" tooltip no longer lingers over the
  docked tree after the click (framework 0.9.41).
- **Drop files onto a remote drive's row in the folder tree to upload them.**
  The row takes local files the way a folder row takes a move, when the
  drive can take uploads (an FTP drive can; so can a Nextcloud or Dropbox
  drive, which cannot otherwise be changed from here). Each file goes up as
  its own request under its own name, the status bar says how many are on
  their way, and the folder re-lists from the server as they land; a
  refusal comes back as the server's own message. Folders are not uploaded
  (a tree is not one transfer) and remote entries have no local file to
  send: both are counted in the status bar with the reason.
- **Dot-entries on a remote drive are hidden like local ones.** A `.git`
  folder or `.htaccess` on a server was listed on every display while the
  same name on a disk was hidden; the listing now marks them hidden, so
  Display > Hidden files and the display's own toggle govern both alike.

#### 2026-09-22 *1.45.0*
- **Extras > Find text: search inside files.** A new first item in the file
  context menu's *Extras* submenu asks for a text and lists every file in the
  shown folder and its sub folders that contains it. It runs on the
  background walk behind *Scan sub folder*: matches land in the same result
  display (with *Open path* on each entry) while it searches, the status bar
  counts files found and files read, and the search field's *Stop* button
  ends it. The comparison ignores the case of ASCII letters; binary files,
  files over 64 MB, hidden entries and links are skipped. Local folders only.
- **Case-insensitive name comparisons no longer rely on undefined behaviour.**
  The sub-folder name search, the folder tree's sort and the folder-icon keys
  (Windows) lowercased with `::tolower` on plain `char`, which is undefined
  for the bytes of a non-ASCII (UTF-8) name. They now go through
  `unsigned char`; what matches and how folders sort is unchanged.

#### 2026-09-22 *1.44.1*
- **Remote-drive credentials are actually saved now.** UltraFiler handed its
  drive passwords and tokens to UltraCloud's `VaultSecretStore` but never
  opened a vault for it to write into, so every store was refused and a drive
  had to be signed in to again on each start. UltraFiler now has a vault of
  its own — `ultrafiler.vault` and `device.key` under the configuration
  directory, the framework's `UltraVault::DeviceKeyVault` (0.9.23), unlocked
  without a prompt — and the secret store writes into it under UltraCloud's
  `cloud.<accountId>.*` keys. Credentials a build without UltraVault kept in
  `remote-drive-secrets/` are carried into the vault on the first start and
  the files removed. When the vault cannot be opened the remote-drive list
  says so instead of silently forgetting every sign-in.

#### 2026-09-19 *1.44.0*
- **Split view: two folder displays side by side.** A split-screen button in
  the navigation row, left of the clock, replaces the folder tree and the one
  folder display with two displays next to each other: the active tab's on
  the left, a second display of its own on the right - one that is in no
  tab, has its own Back / Forward history, and opens on the folder it last
  showed. A draggable splitter sits between them, and the preview pane, when
  it opens, takes its width from the right-hand display.

  Each pane has a header row: a **folder-tree button** and the pane's own
  breadcrumb, which navigates that pane. The tree button docks the folder
  tree down the left of that display, under its header, and takes it away
  again (so does Esc). There is one tree, so pressing the other pane's button
  moves it over; a docked tree follows and navigates the display it sits
  beside.

  The display clicked last is the active one - its header is tinted - and it
  is what the toolbars, the search field, the status bar and the preview act
  on, exactly as they act on the active tab. Clicking a tab makes the
  left-hand pane active again; the Computer page always opens in the
  left-hand pane. Files drag and drop between the two displays like between
  any two displays of the window. Every setting that reaches "every tab" - a
  changed Display or Handling setting, a vanished volume, a deleted folder -
  reaches the right-hand display too.

  The switch and the right-hand display's folder are saved with the settings
  (`view.split`, `view.split.second.folder`), so the next start opens the
  pair as it was left. Turning the split view off brings the tree pane back
  as wide as it was.

- The command bar's **Preview** toggle now carries a picture icon: the
  split-screen icon it used to share is the split view's.

#### 2026-09-19 *1.43.0*
- **Remote drives are writable: delete, rename and new folder.** A drive added
  through **+ Drive** could be browsed and nothing more; now the three commands
  that act on what is *on* the drive work against the server. Deleting asks
  first, exactly as a local delete does, and a folder goes with `RMD` where a
  file goes with `DELE` - the display already knows which it is, so no round
  trip is spent finding out. **F2** renames in place. **New folder** creates one,
  named after what the folder already holds, and you can rename it once it
  appears.

  Nothing waits on the server while the window paints: each change is queued,
  the folder it touched is dropped from the cache, and the display refetches
  when the answer arrives. A refusal is reported in the server's own words -
  "550 Permission denied" says what to change where "it did not work" says
  nothing - and deleting a dozen entries that all fail the same way is one
  dialog, not a dozen.

  **What a drive can take depends on the drive**, and it says so before you
  try: FTP, FTPS and SFTP can be changed, while a Nextcloud, WebDAV, Dropbox,
  OneDrive or Google Drive account is still read-only here and draws its
  entries with the read-only badge. That follows the provider's own
  capabilities rather than a list kept in the file manager.

  **Copying files to or from a drive is not in this yet.** A transfer needs the
  progress window, the conflict questions and the Cancel that copying already
  has, and that machinery is built on the local filesystem throughout; giving
  it a second backend is its own change rather than a fourth hook. Duplicate,
  paste and New > *document* therefore still say a remote drive cannot take
  them.

  Built on framework 0.8.93's `remoteDelete` / `remoteRename` /
  `remoteMakeDirectory` hooks and the `CloudService` change verbs added with
  them.

#### 2026-09-17 *1.42.0*
- **UltraFiler can now use your desktop's own file icons.** Until now a file
  with no preview of its own was drawn as a coloured sheet with its extension
  on it, and a folder as a drawn folder shape - the same picture on every
  machine, and like nothing else on any of them. **Settings > Display > File
  icons** now chooses between them:

  - **UltraFiler simple** - the drawn folder and sheet. The default, and what
    every earlier release showed.
  - **Host OS icons** - what this system draws for the type: the shell's icons
    on Windows, Finder's on macOS, the installed icon theme's on Linux and
    BSD. A PDF gets the document icon, an archive the package icon, a
    spreadsheet the spreadsheet icon - the same pictures as the rest of the
    desktop, because they come from the same place.

  It is also in each file display's own **Display > File icons** context menu,
  beside File extensions.

  What it does not touch: a file that shows a thumbnail of its own content
  goes on showing it, and a program or shortcut goes on showing the icon it
  carries inside itself - those are the file's own picture rather than its
  type's, and Explorer and the Finder prefer them too. A folder you gave an
  icon with **Extras > Set folder icon**, and the main user folders with icons
  of their own, keep them either way.

  A file type this system has no icon for keeps the simple one, and so does
  every icon until its lookup lands, so the window never waits on the desktop
  and never shows an empty box. On a system with no desktop to ask the choice
  is greyed out and says why. With host icons on, a folder is drawn with the
  system's folder icon - so the pictures inside it no longer peek out of it,
  since there is no drawn folder left to peek out of.

  The setting is saved like every other (`display.file.icons` in the config
  file). Framework change, see UltraCanvas 0.8.84; the application's own part
  is the settings page, the config key and the menu wiring.

- **Fixed: the Display settings were ignored by every file display created
  after start-up.** They are pushed into the displays that exist when the
  settings file is read - which at start-up is the folder preview alone,
  because the tabs are built after it. The first tab, every tab opened later,
  and the History, Favorites and Computer displays therefore browsed with the
  built-in defaults until the user happened to change a setting, at which
  point the whole set was pushed in and they jumped. So a saved **Thumbnails**
  or **Detail view** selection, the **File extensions** switches and
  **Folder previews** all came back wrong after a restart. Each display is now
  given the settings as it is wired up, whenever it is created.

  Found while adding File icons above: it was the reason a saved choice of
  host icons did nothing until the settings window was opened.

#### 2026-09-17 *1.41.0*
- The per-application plugin list is gone. UltraFiler links
  `UltraCanvasAllFormats` and every format plugin the build produced registers
  itself before `main()` - no includes, no defines, no registration calls in
  `main.cpp`, and nothing to update when a plugin is added to the framework.
  Needs framework 0.8.83.
- With the preview tests now asking the graphics registry as well (also
  0.8.83), the formats only a registered plugin can draw stop being greyed on
  Display > Thumbnails and Display > Detail view: the CorelDRAW files libcdr
  parses, `.ccx` and `.cdt` included, which the previous release could
  register but not show.

#### 2026-09-17 *1.40.0*
- **"+ Drive": an FTP / SFTP server or a cloud account as a place you can
  browse.** The navigation row has a new **Drive** button. It offers two
  kinds - *FTP / SFTP server...* and *Cloud storage...* - because the two are
  configured quite differently: a server you type a host, a user and a
  password for, against an account you sign in to through the browser. Either
  choice opens UltraCloud's shared add-account dialog with only that kind's
  providers in it, so neither list is padded with the other's.

  What you add appears under a new **Remote Drives** section of the folder
  tree, between the cloud sync folders and the real drives. The distinction is
  deliberate: **Cloud Storage** above it lists the folders a sync client has
  already put on this disk, which work with the network off, while a remote
  drive is the server itself. Like *Pinned* and *Cloud Storage*, the section
  stays hidden while there is nothing in it.

  Clicking a drive browses it in the folder display - names, sizes, dates,
  folders first - with the icons any local file of the same name would get.
  Up climbs back through the server's folders and steps out to the Computer
  page at the drive's root; **Refresh** on a remote folder asks the server
  again rather than repainting what was cached.

  The server is never waited on while the window paints: a folder that has not
  been fetched yet shows empty for the moment, a worker fetches it, and the
  display fills itself in when the answer lands. An unreachable server costs
  that one folder a message, not a frozen file manager.

  **Read-only for now.** Delete, rename, duplicate, paste, new folder and new
  file all answer that a remote drive can be browsed but not changed, rather
  than failing obscurely. Uploading and deleting on a drive is the next step.

  Credentials go where the rest of the system keeps secrets - UltraVault, or
  the per-app obfuscated file in a build without it - and never into the drive
  list itself. A build made without UltraCloud has the button disabled and
  says why, rather than offering something it cannot do.

  Built on framework 0.8.82's `isRemotePath` / `remoteListing` hooks and the
  add-account dialog's new provider filter; UltraFiler's own part is
  `UltraFilerRemoteDrives` (the drive list, the listing cache and its worker)
  and `UltraFilerRemotePath.h` (the `ultracloud://<account><path>` scheme).
#### 2026-09-17 *1.39.2*
- **Programs and libraries are told apart on sight.** The framework's file
  display now carries a `Library` category of its own, so `.dll`, `.so` and
  `.dylib` are steel grey against the dark red of `.exe` and the installers,
  and the Type column calls `core.dll` a *Dynamic Link Library* instead of a
  *Library Program*. The whole file-type palette moves with it: hue says which
  family a file belongs to, brightness says how efficient its format is (AVIF
  over JPEG over GIF, Opus over MP3, WebM over AVI), and lossless formats sit
  beside their lossy siblings instead of being a duller shade of them.
- The History view's *Apps* tab asks the category rather than matching its own
  list of program extensions — the list existed only because the old category
  counted libraries as programs.
#### 2026-09-17 *1.39.1*
- **The Cloud Storage section no longer gives up after one look.** The folder
  tree's cloud lookup marks itself busy while it runs so two cannot overlap,
  but three of its ways out - finding no cloud folders at all, and either kind
  of failure reading a provider's registry or configuration - forgot to clear
  that mark. The first such lookup left it set for the rest of the session, and
  every later one returned at the door without looking.

  On a machine with no sync client installed the very first lookup finds
  nothing, so this was the normal case: from then on plugging in a drive never
  re-checked for cloud folders - which is exactly what the re-check exists for,
  a Google Drive that mounts as its own drive letter. A provider that threw
  cost the section permanently instead of for that one attempt.

  The mark is now released on every path out of the lookup, and where the
  results are handed to the window it is released once they have been applied,
  so a lookup still cannot overlap with the one before it.
#### 2026-09-17 *1.39.0*
- **Copying, moving and deleting show their progress.** An operation that is
  still running two seconds after it started now opens a window with a ring,
  the percentage, the name of the file being handled and a **Cancel** button -
  the same window compressing and extracting have had all along. Anything
  quicker still passes without one, so copying a text file does not flash a
  dialog at you.

  It covers every way of starting one: **Ctrl+V** and the context menu's
  Paste, **Delete**, **Duplicate**, dragging files between the panes or from
  another program, and the folder tree's own **Delete folder** - which until
  now removed the folder with the window frozen and nothing to look at.

  And UltraFiler no longer stands still while it happens: the work runs in the
  background, so the file display goes on painting and scrolling while a few
  gigabytes are on the move. **Cancel** stops it at the next file - what was
  already copied, moved or deleted stays, and the file the cancel interrupted
  is cleaned up rather than left half-written.

  Framework change, see UltraCanvas 0.8.77; the application's own part is the
  folder tree's delete, which now goes through the filer widget so it gets the
  window and the "cannot delete" dialog like every other delete.
#### 2026-09-17 *1.38.0*
- **Deleting a file that needs administrator rights now works, the way it does
  in Explorer.** A file or folder whose permissions grant deletion only to
  administrators used to fail with "Access is denied", and the dialog's
  offers — *Try again* and *Skip* — could not get past that. Now that dialog
  is **Administrator Permission Needed** with **Delete as administrator**
  preselected: Windows puts up its consent prompt, and on *Yes* the entry is
  deleted. Several such entries in one delete are collected and cost one
  prompt at the end, whatever "do this for all remaining items" was set to;
  what still cannot be deleted even then (a file owned by TrustedInstaller, one
  in use by a running program) is listed with the system's reason. Declining
  the prompt leaves everything in place. Built on framework 0.8.74's
  `UltraCanvasElevatedFileOperations`; UltraFiler's `main.cpp` runs the
  elevated helper before any window exists.
#### 2026-09-17 *1.37.0*
- The CorelDRAW, Xara and EPS viewer plugins are linked and registered too,
  after the Vector plugin rather than before it: both read some of the same
  extensions, the graphics registry's last registration owns them, and for
  those the dedicated viewers are the better reader - libcdr parses CorelDRAW
  files no converter here writes, the XAR plugin covers the compressed Xara
  files the converter's reader does not, and the EPS plugin interprets
  PostScript instead of looking for a preview bitmap in it. The Vector plugin
  keeps what only it reads (DXF, the DWG family, EMF, WMF) and stays the only
  writer, since saving matches on GetSaveExtensions instead.
- Measured, so as not to overstate it: on a build of this container - where
  libcdr is absent, so the CDR plugin is not built - registering XAR and EPS
  changes **nothing** on the Display > Thumbnails and Display > Detail view
  pages. Their formats were already covered, xar through the Vector plugin's
  reader and eps/ps through libvips. The registration is what a build WITH
  libcdr needs to gain cdr/cmx/ccx/cdt in the FileLoader inventory, and what
  gives `LoadGraphicsFile` and the vector rasterizer a real reader for them.
  Lighting up the two settings pages for ccx/cmx needs one more thing, which
  is not in this release: the Filer's and the media viewer's preview tests ask
  the vector preview seam and the embedded-preview probe, never the graphics
  registry, so a format only a registered plugin can draw is still greyed.

#### 2026-09-16 *1.36.0*
- **UltraFiler registered no format plugins at all.** A plugin reads nothing
  until an application links and registers it, and this one linked only the
  core library - so the entire Vector matrix (DXF, DWG and the rest) and every
  3D format but STL previewed as a plain type glyph, greyed themselves out on
  the Display > Thumbnails and Display > Detail view settings pages, and
  produced no detail pane when selected. The readers were compiled into the
  build and sitting idle. Reported as "selecting a DWG file doesn't produce a
  detailed window section", which is exactly what it was.
- The Vector and Models plugins are now linked and registered at startup,
  before the main window, since the settings pages read what the build can
  show when they are first built. Each is an optional CMake target
  (`ULTRACANVAS_PLUGIN_VECTOR`, `ULTRACANVAS_PLUGIN_MODELS`), so each is
  picked up only where it was built and `main.cpp` registers it behind the
  matching define - a build without them behaves exactly as before.
- With both registered, 23 formats change from greyed to live on those two
  pages: dxf, dwg, dwt, dws, sv$, emf, wmf, and sixteen 3D formats (3ds, obj,
  ply, dae, fbx, x, ms3d, blend, abc, step/stp/p21, x3d/x3dv, wrl/vrml). What
  stays greyed now stays greyed for a reason this build can name: no PDF
  plugin, no video or audio backend, no reader for glTF/GLB/3MF, and no
  picture inside an audio file or a Corel .ccx/.cmx to show.
- Needs framework 0.8.73, which is where the drawings become showable at all:
  the vector preview seam, the media viewer's vector view and the Filer's
  vector thumbnails. The lighter greyed-out colour on the settings pages -
  the disabled switches used to be drawn darker than the live ones - is from
  the same release.
#### 2026-09-17 *1.36.0*
- **The History view's lists have a length you can set.** *Settings > Extras >
  History & Favorites* gains **Limit of entries**, a slider from 10 to 1000
  entries; it ships at 300, the length the lists always had, so nothing changes
  until it is moved.

  The limit counts **per section**: *Files*, *Folders* and *Apps* each keep that
  many. That is the point of capping them separately — a morning of opening
  documents cannot push out the applications you launch once a week.

  Lowering it takes effect **now**, not at the next start: the entries past the
  new limit are dropped and `history.txt` is rewritten at its new length, and
  the History view is refreshed if it is the one on screen. A limit that only
  applied after a restart would, to the person who just moved the slider, look
  like it had done nothing. Raising it back does not bring the forgotten
  entries back — they are gone from the file. *Restore default limit* in the
  bottom bar puts it back to 300.

  The lists themselves were already saved whenever one of them changed and read
  back at start-up; a shorter limit set while UltraFiler was not running (the
  config file edited by hand, settings arriving from another machine) is now
  applied as the file is read, rather than after the first entry happens to be
  recorded into it.

#### 2026-09-15 *1.35.0*
- **Settings > Extras > Cache: what UltraFiler is holding, and whether it
  should.** A page beside *Open prompt* and *History & Favorites* with two
  switches and four figures.

  The switches are **Keep thumbnails on disk between runs** (on) and
  **Compress thumbnails held in memory** (off - roughly a quarter the size,
  at the cost of unpacking each tile as it comes on screen). Both apply to
  every open tab straight away and are saved, like every other setting here.

  The figures are what is held against what may be held: what is on disk, the
  previews in memory and their ceiling, the application icons and *their*
  ceiling - they have their own so that a folder of photos cannot push the
  programs' icons out - and, with compression on, the tiles unpacked for
  drawing. The ceilings are the framework's own numbers, asked for rather than
  written down here, so this page cannot fall out of step with them. The path
  the files are kept at is shown under the figures.

  **Empty cache** deletes them all and frees the memory; **Refresh** re-reads
  the figures, which is worth a press after emptying, because whatever is on
  screen is decoded again immediately and goes straight back into the cache.

#### 2026-09-15 *1.34.1*
- **Thumbnails come back, and they come back fast.** Two framework changes
  carry to UltraFiler here (see framework 0.8.62); nothing changed in the
  application itself.

  The first is the reason thumbnails could stop appearing altogether. The
  shared image cache miscounted the bytes it held, in a way that could wrap its
  total to an enormous number — after which it evicted everything on every
  insert and effectively cached nothing, so every picture in a folder was
  decoded again on every repaint. It looked like a cache that was full, and in
  the sense that mattered it was: permanently, and with nothing in it. Browsing
  a folder with animated GIF or WebP files in it was enough to trigger it.

  The second is that finished thumbnails are now kept **between runs**, in
  `%LOCALAPPDATA%\UltraCanvas\thumbnails` on Windows (`~/Library/Caches/…` on
  macOS, `$XDG_CACHE_HOME/…` elsewhere), so the folder that cost minutes of
  decoding on its first visit draws from disk on every visit after it,
  restarts included. A thumbnail whose file has since been edited is never
  served — the entry records what it was made from — so this cannot show a
  stale picture.

  The cache looks after its own size: an entry is stamped with the day it was
  last served, and entries not served for **two weeks** are deleted at startup.
  A folder you keep visiting keeps its thumbnails; one you opened once pays for
  itself and then goes away. Application icons are not stored — Windows
  produces those faster than a file read, and an upgraded program must not show
  yesterday's icon.

#### 2026-09-14 *1.34.0*
- **The folder the detail pane is showing can be moved into the folder
  display with one click.** Clicking a folder shows what is in it in the
  detail pane on the right, which answers "what is in there?" without leaving
  the folder you are in - but the pane is a narrow strip, and the way on from
  it was to go back and double-click the folder after all. A round button now
  floats over the middle of the pane's left edge, pointing at the display it
  moves the folder to: click it and the folder display opens that folder full
  width, and the pane - with nothing left to preview - folds away. It takes
  whatever the pane shows at that moment, so a subfolder entered inside the
  pane is moved across just as the clicked folder is. It appears only while
  the pane holds a folder; a previewed file never shows it.

#### 2026-09-14 *1.33.0*
- **Sti_Trace.log, and clutter like it, can finally be got rid of.** The file
  is the Windows Still Image service's trace log: any program that talks to a
  scanner or camera has it written into the profile, and because it carries no
  hidden attribute and no leading dot, neither *Display > Files > Show hidden
  files* nor the Home folder curation could touch it - it sat in the Home
  folder with no setting anywhere that would remove it.
  **Settings > Display > Ignored files** is that setting. It ships with a
  built-in list - `Sti_Trace.log`, `desktop.ini`, `Thumbs.db`, `ehthumbs.db`,
  `ntuser.dat*`, `ntuser.ini`, `.DS_Store`, `._*`, `.Trash-*`, `.directory` -
  on by default and each pattern switchable on its own, plus a field for the
  user's own patterns (globs: `*` for any run of characters, `?` for one,
  matched ignoring case, so `*.bak` covers every backup file). The list is one
  list for every platform on purpose: a Windows share browsed from Linux or
  macOS carries `Thumbs.db` and `desktop.ini` with no hidden attribute to
  filter on. *Apply them* chooses between **Only in the Home folder** (the
  default, where the clutter collects) and **In every folder**. Stored as
  `display.ignored.builtin`, `display.ignored.builtin.off`,
  `display.ignored.patterns` and `display.ignored.scope`; the built-in list is
  persisted as what is switched OFF, so a pattern a later release adds starts
  on rather than absent. "Restore the built-in list" re-ticks the built-ins and
  leaves the patterns the user typed alone.
- **Nothing disappears silently.** An ignored file is only left out of the
  display - it is not moved or deleted, a search still finds it, a path typed
  into the address bar still opens it, and *Show hidden files* brings it back.
  While a folder is holding an ignored name back it says so on the strip along
  its foot, in *every* folder rather than only the Home folder; a folder that
  merely leaves out its dot names stays quiet, the way every file manager does.
  (Framework side: `Docs/UltraCanvas/CHANGELOG.md` 0.8.46.)

#### 2026-09-13 *1.32.0*
- **The Home folder now says when it is hiding something, and hidden files
  have a setting.** The home folder is the one folder UltraFiler holds two
  things back in: what the system calls hidden (`NTUSER.DAT`, the profile
  junctions, every dot name), and - with *Display > Home folder* on its
  Windows default - every subfolder that is not one of the main user folders.
  Nothing said so, so a home folder could look emptier than it is. While its
  display is leaving anything out it now carries a strip across its foot:
  "7 items are hidden here", and a **Show hidden files** button that reveals
  them for that display. It appears in the Home folder only - everywhere else
  just the platform's hidden entries are missing, which is what every file
  manager does and needs no announcement - and it goes away the moment
  nothing is held back.
- **Settings > Display > Files > Show hidden files** makes that choice
  permanent: off by default, as it ships, and when on every folder display
  starts out showing hidden entries (which also shows the Home folder whole,
  whatever *Display > Home folder* says - the switch means "show me
  everything"). It is a starting point, not a clamp: a single display can
  still be switched by its own *Display > Hidden files* context-menu entry or
  by the Home folder's button, and changing an unrelated setting no longer
  pulls that display back. The folder tree leaves hidden folders out either
  way. Stored as `display.files.show.hidden` in the config file.
- **The settings pages showed their explanations half-cut.** The line under
  each page title and the notes block at its foot were drawn one line tall
  with the text clipped through them. The cause was in the layout engine,
  not in the settings window - see the framework changelog for 0.8.43
  (wrapped text in a flex column) - and every page is legible with that fix.

#### 2026-09-13 *1.31.0*
- **Folder icons show the first pictures inside the folder**, peeking out of
  the folder the way Explorer's folder icons do, in every thumbnail view. Up
  to two of the folder's first pictures by name stand in the open folder,
  drawn from the same thumbnails the files themselves get, so the *Settings >
  Display > Thumbnails* switches govern them too: a kind switched off never
  shows inside a folder either. The well-known user folders and any folder
  given an icon through *Extras > Set folder icon* keep their icon. The
  context menu's *Display > Folder previews* and a checkbox at the top of
  *Settings > Display > Thumbnails* turn it off - each shown folder costs one
  listing in the background, which is worth avoiding on a slow network
  volume - and the choice is saved as `display.folder.previews`. (Framework
  side: `Docs/UltraCanvas/CHANGELOG.md` 0.8.39.)
- **The folder tree now follows what happens to folders in the file display.**
  Cutting a folder with `Ctrl+X` and pasting it into another folder moved it on
  disk and in the file display, but the tree kept showing it - with everything
  under it - where it used to be, and the folder it moved into never grew a row
  for it. Deleting a folder left its row behind in the same way, and a new
  folder never appeared in the tree at all. Only restarting UltraFiler put the
  tree right.
- Every change the user makes to a folder's content now re-syncs that folder's
  rows with the disk: rows whose folder is gone leave together with their
  subtrees, rows that appeared are inserted in name order, and a folder row
  gains or loses its expand button accordingly. A folder the tree has not
  scanned yet is not scanned for this - only the expand button it would be
  drawn with is put right - so the tree stays as lazy as it was.
- It covers every place such a change can come from: the file display of any
  tab, the folder preview pane, the History and Favorites lists, the tree's own
  Paste item and a drop onto a tree row. Creating, renaming and duplicating a
  folder reach the tree through the same path as moving and deleting one.
- **A tab showing the folder something was cut out of now re-lists it**, and so
  does the folder preview pane. Cutting a folder in one tab and pasting it in
  another left the first tab listing a folder that was no longer there; every
  display of a folder whose content changed is now re-listed, wherever the
  change was made. The display that made the change is not scanned twice.
- A pin pointing at a folder that has been moved away or deleted leaves the
  tree's Pinned section, exactly as it does when the folder is deleted through
  the tree's context menu. (Framework side:
  `Docs/UltraCanvas/CHANGELOG.md` 0.8.39.)

#### 2026-09-12 *1.30.0*
- **A double-click starts the program the file type is assigned to.** On
  Windows a double-click means one thing - the file opens in the program
  registered for it - and UltraFiler did not do it: every file the preview
  could show was shown in the preview instead, and the registration was never
  even asked about. *Settings > Handling > Opening files* now chooses between
  **Start the registered program** and **Show it in the preview**, and it
  ships set to the registered program on Windows (what Explorer does) and to
  the preview on Linux and macOS (what every earlier release did everywhere).
  The choice is saved as `handling.files.double.click`. A file type this
  system has no program for is previewed whichever way it is set, so the
  setting can never turn a double-click into nothing happening; a file that
  cannot be previewed - a program, an installer, a file type UltraFiler does
  not read - still goes to the system as before; and the context menu's *Open
  with* is unchanged.
- **What counts as "a program is registered for this" is now the default
  program**, not any application that offered to open the type. On Windows
  the difference is the whole answer: a file type nothing is registered for
  enumerates half the machine, so "is this assigned to a program" was
  answering yes for everything. The font viewer rule - a font file opens in
  the system's viewer when there is one, and in UltraFiler's own window when
  there is not - was reading the same wrong answer.
- **Opening a file on Windows now reports why when it fails**, instead of
  "Could not open". The status line names the reason the shell gave - the
  file was not found, access was denied, another program is holding it, the
  registered program did not answer - and a file type with no program behind
  it puts up Windows' own "How do you want to open this file?" chooser,
  exactly as a double-click in Explorer does. (Framework side:
  `Docs/UltraCanvas/CHANGELOG.md` 0.8.34.)
- **A double-click with the right mouse button no longer opens the file it is
  on.** Windows reports a double-click for the right and middle buttons too,
  and a second right-click on a file is aiming at the context menu.

#### 2026-09-11 *1.29.0*
- **The toolbar's View picker comes before Sort, and every entry shows its
  layout.** The two pickers sat the other way round, so choosing how the folder
  is drawn - the more frequent of the two, and the one the eye goes to first -
  meant reading past the sort field to reach it; *View* now stands immediately
  after the search box, with *Sort* and its direction arrow behind it. The
  eight entries were plain text, which left *Details*, *List* and the four
  icon sizes to be told apart by name alone; each now carries a glyph of what
  it produces - a row of labelled lines for *Details*, two flowing columns for
  *List*, a 4x4 / 3x3 / 2x2 / single tile grid for the four icon sizes, a
  descending bar chart for *Size bars* and a nest of proportional rectangles
  for *Treemap* - so the list reads as a set of layouts, and the closed picker
  shows the current one's glyph beside its name. The eight icons are new:
  `media/icons/view-*.svg`.

#### 2026-09-10 *1.28.0*
- **A dragged-and-dropped move asks first.** Dropping files onto a folder of
  the file display carried the move out the moment the button came up, and a
  drag is the one file operation that starts by accident - a press that
  wandered a few pixels on the way somewhere else - so the first sign of it was
  a folder that had quietly emptied itself into a neighbour. *Settings >
  Handling > Drag & Drop* now carries **Confirmation**: **Always**, **Only when
  files are moved** (the new default) or **None**, the silent drop earlier
  releases had. The question names how many entries are about to be moved or
  copied and into which folder, with that folder's full path underneath -
  a drop lands on whatever was under the cursor - and nothing is touched until
  it is answered. Files dragged in from another program are copies, so only
  *Always* asks about those. (Framework side:
  `Docs/UltraCanvas/CHANGELOG.md` 0.8.1.)
- **The settings window opens on its sections instead of inside one.** The tree
  led with a *Settings* row that held everything and stood for nothing, below
  it every section and every page was already open, and the window opened on
  *Display > Treeview* - one page of eleven, picked because it happened to be
  first. Now the three sections - **Display**, **Handling**, **Extras** - are
  the top level of the list, all three closed, and the page beside them says
  what each one holds. Opening a section still moves straight on to its first
  page, and a page opened from elsewhere in the application (the file display's
  *File formats...* entries) opens its section on the way.
- **History & Favorites moved into Extras.** It was a section of its own, one
  row deep, sitting beside the three that group everything else; it is now the
  second page of *Extras*, next to *Open prompt*. What it does is unchanged.

#### 2026-09-10 *1.27.0*
- **The "+" of the tab strip can open the Home folder instead of the folder
  in front of you.** Opening a tab has always meant "the same folder once
  more", which is what a second view of the folder being worked in wants, and
  exactly wrong when the new tab is meant to start somewhere else entirely -
  every one of those began with a walk back up out of the folder the tab
  inherited. *Settings > Handling > Tabs* now chooses between the two: **New
  view of the current folder** (the default, unchanged behaviour) or **Open
  the Home folder**. Only the "+" follows the setting - a tab opened on a
  folder that was named, such as the containing folder of a search result or
  an entry of the History or Favorites view, still opens on that folder - and
  the choice is saved as `handling.tabs.new.tab` in the config file.
#### 2026-09-10 *1.26.0*
- **The Home tab is called Home.** A folder tab is named after the folder it
  shows, and the home folder is named after the account that owns it - so the
  tab the file manager opens on carried a login name and said nothing about
  what was in it. It now reads **Home**, with the home icon beside it: the
  same mark the folder tree's Home row and the Computer page's Home tile
  carry, so the three places that lead to that folder finally look alike.
  Every other tab is unchanged - it keeps the folder's own name and no icon.
  The name follows the tab: navigating into the home folder renames the tab
  and puts the icon on, leaving it takes both away again. The Computer page's
  Home tile, which carried the account name under the same icon, now reads
  *Home* as well (framework side: `displayNameProvider`,
  `Docs/UltraCanvas/CHANGELOG.md` 0.8.0) - the folder it opens, its name and
  what can be done with it are unchanged, only what the tile is labelled.
- **A tab says which of the user's places it is in.** The main user folders -
  Desktop, Documents, Downloads, Music, Pictures and Videos - now put their
  icon on the tab, the same icons the folder tree and the file display already
  draw for them: the monitor for Desktop, the download arrow, the picture, the
  notes, the play mark, the document. A tab keeps the icon *inside* the folder
  too, so one three folders deep in Downloads still shows the download mark -
  which is the whole use of a mark on a tab, and what the folder name alone
  ("2024") does not tell. The nearest folder wins, so a Pictures folder kept
  inside Documents shows the picture icon rather than the document one, and
  the home folder is deliberately not inherited downwards: everything the user
  has is under it, and an icon on every tab marks nothing.
- **A home icon with the user in it.** The plain black house has been replaced
  by `home-user.svg` - a house with its occupant, in the flat two-tone shape
  of the folder icons - in the tree, on the Computer page's Home tile and on
  the new Home tab alike. (Framework side: `Docs/UltraCanvas/CHANGELOG.md`
  0.8.0.)
- **The New folder arrow is worth aiming at.** The split button's menu section
  was marked with a "▾" character, which the text renderer drew a few
  pixels across inside a section nearly thirty wide: it read as a speck rather
  than as the "there is more here" arrow it is. It now carries the framework's
  `dropdown.svg` at 14 px, in the button's own text color, centered in its
  section. Nothing about what the button does has changed - the primary
  section still creates a folder, the arrow still lists the other kinds.

#### 2026-09-09 *1.25.0*
- **Double-clicking a font opens it, full size, in its own window.** A font was
  the one previewable kind whose whole point is the part the preview pane
  cannot hold: the pane shows a two-letter specimen, and what you
  double-clicked for is every glyph in the file. It now opens in a viewer
  window (`UltraCanvasMediaViewerWindow`) over the file manager, with the
  range picker, the size control and the arrow keys walking the rest of the
  folder — unless this system has an application registered for the file, in
  which case that opens it, the way Explorer would. Single-click still shows
  the font in the preview pane, and every other kind of file behaves exactly
  as before. The window is reused, so double-clicking through a folder of
  fonts leaves one window open rather than one per font; Escape closes it.
#### 2026-09-09 *1.24.0*
- **The search field kept its button and lost its text when the window got
  narrow.** Reducing the window width squeezed the search field away entirely
  and left a clipped sliver of the blue "Scan sub folder" button where the
  whole search control used to be — nothing to type in, and a button too
  narrow to read. A flex row shrinks its shrinkable child first, so the text
  input gave up all its width while the fixed-width button kept its 118 px.
  The button is the one of the two that can be done without: Enter runs the
  same scan, and the file display still offers it in the middle of an empty
  result. So the search box now reports when it no longer has room for the
  button *and* a readable field, and the button goes instead of the field.
  Widening the window brings it straight back. The answer is only known once
  the layout engine has sized the box, but acting on it *inside* that pass
  leaves the text input zero-wide for good — a trap now written down in
  `Docs/CSSLayout.md` — so the box reports and the window applies it on the
  next turn of the event loop.
#### 2026-09-09 *1.23.0*
- **Double-clicking a program or a document now shows that it is starting.**
  The launch itself is instant, the application appearing is not, and until
  it did the window looked exactly as it does when a double-click was missed.
  A second after the double-click the pointer changes to the arrow-with-busy-
  sign shape, and it returns to normal by itself — so a program that opens
  at once never changes the pointer, and a slow one no longer invites a
  second double-click. Windows programs launched through UltraWin use it too,
  with the long wait a first launch needs while its Windows environment is
  prepared, and put the pointer back the moment the run actually starts.
  (Framework side: `Docs/UltraCanvas/CHANGELOG.md` 0.3.111.)

#### 2026-09-08 *1.22.0*
- **The settings window reads the same on every page.** The pages had grown
  one at a time and it showed: a title smaller than the choices under it,
  choice labels in a different size from the checkboxes, explanations mixed
  in among the controls at the same size as the choices, a radio label cut
  off at the page's edge, and a *Restore default ...* button sitting wherever
  the page happened to end. Every page is now built the same way: a bold
  title, one line saying what the choice is about, the controls, all in one
  text size, and the notes that explain the setting set apart in a tinted
  block with an accent bar at the foot of the page, a step smaller and
  greyed, so what is acted on and what is read about it are never
  confused. Texts wrap instead of being cut off.
  - The *Restore default colours* / *Restore default widths* button of a
    page now sits at the left end of the window's bottom bar, opposite
    *Close*, the same spot on every page that has one; it is hidden while
    the shown page has nothing to restore.
  - Clicking a heading in the tree (*Display*, *Handling*, *Extras*, or
    *Settings* itself) moves on to its first page: a heading has no page of
    its own, so it and its first entry used to show the same content with
    two different rows selected.
- **Media Viewer > Transparent Images is gone.** The backdrop behind a
  transparent image is chosen from the colour strip the preview shows right
  under the picture, which previews the choice in place; the page that
  duplicated it with a full colour picker had nothing left to do. The
  setting itself is unchanged and still saved from the strip.

#### 2026-09-06 *1.21.0*
- **The file display says when a file is in use.** A file another program is
  holding — a running program, a document something has open, an archive being
  read — now wears a small padlock in the corner of its icon, carries `X`
  among its attributes and is described in the info bar ("In use by another
  program (cannot be replaced)"). That is the answer to a copy, a rename or a
  delete that fails with *"the action can't be completed because the file is
  open in another program"*, given **before** the attempt instead of after it.
  A file that is merely open somewhere without blocking anything — the normal
  state on Linux and macOS, where an open file can still be replaced — is
  marked `O` and wears no padlock, because it is information, not an obstacle.
  Nothing is written by the check: the file is opened for the access an
  overwrite would need, with every sharing flag granted, and closed again.
- **Attributes names the program.** For a single file the Attributes dialog
  adds an "In use" row that says which program is holding it ("In use by
  Firefox (1234)") wherever the system can be asked — Windows through the
  Restart Manager, Linux through `/proc`. That is the one question a file
  manager could never answer and the user always has.
- **Settings > Display > Files in use** turns the marking off. It costs one
  extra open per shown file, which is worth avoiding on a slow network volume;
  the page also says so, and disables itself where the system cannot answer.
  Everything is probed in the background, so a folder never opens slower for
  it.
- **UltraFiler no longer holds its own folder open.** A program's working
  directory is an open handle on that folder, and on Windows that alone stops
  the folder being renamed, replaced or deleted — so starting UltraFiler by
  double-clicking it made its own folder impossible to overwrite with a newer
  version for as long as it ran, with an error blaming a folder whose files
  all looked free. UltraFiler now moves its working directory to the home
  folder at start-up. A folder given on the command line is resolved before
  the move, so a relative path still means what it did.
- **A closed preview lets go of the file it was showing.** Closing the preview
  pane stopped playback of a video or a track but left the decoder holding the
  file, which on Windows blocked renaming, replacing or deleting exactly the
  file the user had just been looking at. The preview now unloads it
  (framework 0.3.106).
- **Browsing into an archive no longer locks it.** The archive stayed open
  behind the scenes after being listed, so on Windows a `.zip` that had been
  looked into could not be overwritten or renamed for the rest of the session
  — and deleting a file *inside* one failed, because that rewrites the archive
  and renames the new file over the old (framework 0.3.106).
- **The tree's Computer entry opens a page of its own.** Clicking *Computer*
  used to do nothing — it was a header over Home, Cloud Storage and the
  drives. It now replaces the folder display with the machine's places:
  **Folders** — Home and every Cloud Storage folder as folder tiles, which
  open on a double-click and carry the usual context menu — and **Drives** —
  one card per mounted volume with a **pie chart of used against free
  space**: green while there is room, amber past 75 %, red past 90 %, the
  percentage in the middle, the drive's name as the button that opens it,
  *232.9 GB free of 476.2 GB* and the mount point under it. The sizes are
  read on a worker thread, so a network share that stopped answering delays
  its own card and never the window, and the cards follow mounts and
  unmounts like the tree's drive rows do. The status bar sums the drives and
  the window title says *Computer*.
- **Up from a drive root goes to the Computer page**, the way Explorer's Up
  from a drive lands on *This PC*; the Up button is no longer greyed out at
  a root. The breadcrumb's leading *Computer* node opens the page too
  (framework 0.3.106: `FolderBreadcrumbOptions::onComputerClick`), and its
  dropdown still lists the drives. Esc, any navigation, Back / Forward or a
  tab switch returns to the folder display, with the tree, the breadcrumb
  and the status bar describing the folder again.

#### 2026-09-05 *1.20.0*
- **Windows shortcuts show the icon of the program they start.** A Desktop or
  Start-Menu folder full of `.lnk` files was a wall of identical grey "LNK"
  sheets that said nothing about any of them. Each one is now read: the tile
  carries the icon the shortcut names — the application's own icon, or the
  `.ico` a browser wrote for a web shortcut — with a small arrow badge in the
  corner marking it as a shortcut, the type column says `Shortcut`, and the
  info column and info bar show what it points at. A shortcut also groups and
  colours as the thing it stands for, so one to a folder sits with the
  folders. This works wherever the files are: a mounted Windows disk, a
  folder synced from a Windows machine, or the `drive_c` of an UltraWin
  environment — the icons are read out of the files themselves rather than
  asked of a Windows shell (framework 0.3.101: `UltraCanvasShellLink.h` and
  `UltraCanvasIconResource.h`).
- **Double-clicking a shortcut starts what it points at.** A shortcut to a
  folder opens that folder; a shortcut to a program runs it through the
  UltraWin environment it belongs to, the same way a double-clicked `.exe`
  already did — through Wine's `start`, so the arguments and working
  directory the shortcut carries are kept. A shortcut to a *document* is not
  a Windows program: it opens with whatever this system opens that document
  with. A shortcut to a program also
  counts as an application in the History and Favorites lists, which is what
  a Start-Menu entry is.
- **Attributes shows what a shortcut is.** For a `.lnk` the dialog adds the
  target, the arguments, the folder it starts in and its comment, plus where
  that target lives on **this** system — or "not found on this system" when
  the shortcut points at a program this machine does not have.

#### 2026-09-04 *1.19.2*
- **The main user folders have icons of their own — in the file display, not
  only in the tree.** Desktop, Documents, Downloads, Music, Pictures and
  Videos are drawn from `media/icons/user-desktop.svg`,
  `folder-documents.svg`, `folder-download.svg`, `folder-music.svg`,
  `folder-images.svg` and `folder-videos.svg` instead of the generic folder
  shape, so the Home folder reads at a glance the way Explorer's and Finder's
  do. The tree and the display now take them from the same place, so they
  cannot drift apart, and a redirected or localized folder ("Bilder", a
  Documents folder moved into OneDrive) gets its icon like any other — the
  match is on the resolved path, not on the name.
- **Extras > Set folder icon: any folder can have any picture as its icon.**
  The entry opens the file dialog filtered to the image formats this build can
  actually read, and the chosen picture — SVG, PNG, JPEG, WebP, QOI, whatever
  the image pipeline loads — is converted once to a 256 px QOI file in the
  application's config directory (`~/.config/UltraFiler/foldericons`,
  `%APPDATA%\UltraFiler\foldericons`, `~/Library/Application
  Support/UltraFiler/foldericons`). It is that copy the filer draws, so the
  icon keeps working when the original is moved, renamed or deleted, and it
  costs no format plugin to decode. The icon shows everywhere the folder does:
  every tab, the folder preview, the History and Favorites lists, its row in
  the folder tree and its bookmark under the tree's Pinned section. With
  several folders selected they all get it; with nothing selected the shown
  folder does. **Extras > Remove folder icon** takes it away again (and
  deletes the converted file) — it is enabled only while one of the selected
  folders actually carries one. A folder the user set an icon on keeps it over
  the built-in icon of a main user folder, which is the point of setting one
  on Pictures.
- The mapping lives in `foldericons.txt` beside the settings, as one
  tab-separated line per folder, and an entry whose converted icon has gone
  missing is dropped on load rather than drawn as nothing.

#### 2026-09-04 *1.19.1*
- **The folder tree shows its connecting lines again.** The tree was built with
  `TreeLineStyle::NoLine`, so a deep folder opened alongside its neighbours left
  the rows below it indented against nothing: which folder a subfolder belonged
  to was a matter of counting indents. Dotted connectors are back, in a gray
  that carries the structure without competing with the folder icons.
- **Folders with and without subfolders now start at the same place.** A folder
  with a `+` had its icon and name pushed one button width to the right of a
  folder without one, so a plain folder looked like it sat a half-level to the
  left of the folder above it. The framework now reserves the expander slot on
  every row (see `Docs/UltraCanvas/CHANGELOG.md` 0.3.99), and the tree lines up
  down each level.

#### 2026-09-04 *1.19.0*
- **Settings > Display > File extensions.** Two switches on one page: whether
  the file display's names still end in their extension, and what the thumbnail
  tiles show about the file type instead — nothing, a bar across the foot of the
  icon with the extension at its right end, or that tag on its own in the
  corner. Hiding the extension only changes what is drawn: renaming, sorting,
  the Type column and every file operation keep using the real name, so a
  hidden extension can neither be lost nor duplicated by a rename, and a name
  whose tail is a version rather than a type (`UCDemo-Windows-0.3.27-x86_64`),
  a dot file and a folder with a dot in it are all left alone. Both settings are
  saved to `config.ini` (`display.extensions.in.names`,
  `display.extensions.badge`) and applied to every file display of the window;
  the display's own `Display > File extensions` submenu carries the same
  switches, and flipping one there is the same setting saved the same way.
- **A file display opened later starts configured.** The Display > Thumbnails
  and Display > Detail view switches were pushed into the file displays that
  existed when a setting changed, so a tab opened afterwards — and the History
  and Favorites lists — came up with everything switched on regardless of what
  was saved, until the next settings change swept them up. Every newly created
  display now takes the saved Display settings at creation.

#### 2026-09-04 *1.18.2*
- **Application icons stay on screen.** In a folder holding both large previews
  and many executables — a program's install directory, say — the `.exe` and
  `.dll` icons could vanish partway through browsing and not come back, leaving
  a wall of generic EXE/DLL glyphs. The cause and the fix are entirely in the
  framework's thumbnail cache: see framework 0.3.98. Nothing changed in
  UltraFiler itself; this release is what carries the fix to it.

#### 2026-09-03 *1.18.1*
- **The Fonts kind persists.** The framework grew a tenth preview kind — font
  files, thumbnailed as a line of their own glyphs — and both list-of-files
  pages show it and its twelve formats without any change here, because they
  are built from what the widget reports. Its kind switch was the one thing
  that did not survive a restart: the config file names the kinds it stores
  rather than storing a mask, so a kind with no name is simply not written and
  comes back on. `fonts` is now one of those names, and Settings > Display >
  Thumbnails > Fonts stays switched off across launches like every other kind.

#### 2026-09-03 *1.18.0*
- **Settings > Display > Thumbnails and Settings > Display > Detail view: two
  lists of files.** Each page shows the nine file kinds as one checkbox each
  and, under every kind, the individual formats belonging to it — so a single
  format (an EPS on a slow share, a PSD that takes a second to decode) can be
  excluded without losing the thumbnails of everything else in its kind. A
  format this build cannot show is listed but greyed, because seeing that
  `dxf` is unsupported here is what explains a missing thumbnail, which an
  omitted row would not. Both pages carry *Everything on* / *Everything off*.
  The file display's own `Display > Thumbnails` and `Display > Detail view`
  submenus keep the quick per-kind switches and end with *File formats…*,
  which opens the matching page. It is all one setting: a switch flipped in
  either place is saved to `config.ini` (`display.thumbnails.kinds.off`,
  `display.thumbnails.formats.off`, `display.detailview.kinds.off`,
  `display.detailview.formats.off`) and applied to every file display of the
  window — each tab, the folder preview, and the History and Favorites lists.
- **The lists hold every format this build can open.** They are built from the
  FileLoader's own inventory (plus the file display's format table), so what
  the application can open and what the lists can switch cannot drift apart —
  audio files, which had fallen through entirely, are in both lists now, and
  the *Audio* group of *Detail view* is what turns the player pane on and off.
  Both settings are persisted as what is switched **off**, so a format or a
  kind a later version adds arrives switched on rather than missing from an
  existing `config.ini`.
- **The detail pane follows those switches.** It used to open for whatever the
  media viewer could show, which is why switching a kind off left the pane
  showing the very files whose thumbnails had just been switched off. A
  double-click follows the same rule: a file the detail view is switched off
  for opens in its application instead of in the pane.
- **EPS files have thumbnails and a detail view; CorelDRAW and Xara files have
  a detail view.** See framework 0.3.94 — the filer widget and the media viewer
  are framework code, so the work is described there.

#### 2026-09-02 *1.17.1*
- **UltraFiler called itself 0.8.0.** The window title, the `--version` output
  and the Windows file properties all came from a literal in the build files
  that was last edited when the app was at 0.8.0 — so every release since
  reported a version thirteen entries out of date, and the number a bug report
  quoted said nothing about which build it came from. The version now comes
  from the first line of this changelog like every other application's
  (`cmake/UltraCanvasVersion.cmake` already read it and already exported
  `ULTRAFILER_VERSION`; nothing consumed it). `UltraFiler.rc` and
  `UltraFiler.manifest` carry the matching number: those two are compiled from
  disk by windres, so they stay literal — but `./set-version.sh` now writes
  them and a CMake configure on any platform warns when they fall behind
  (framework 0.3.92).
- Thumbnails and vector graphics: see framework 0.3.92. The filer widget is
  framework code, so the fixes are described there.

#### 2026-09-01 *1.17.0*
- **Drives connected while UltraFiler is running now appear in the tree.** The
  drive rows were enumerated exactly once, at start-up, and there was no path
  back to that code: the *Computer* node is marked as already scanned, so
  collapsing and re-expanding it does nothing, and the toolbar's *Refresh*
  re-lists only the file pane. A USB stick, a card, an optical disc, a network
  share or a disk image plugged in afterwards was therefore invisible until
  UltraFiler was restarted — while the path strip's *Computer* dropdown, which
  re-reads the volume list every time it opens, showed it. `RefreshDriveNodes()`
  brings the rows back in line with what is mounted, driven by the framework's
  new `UltraCanvasVolumeMonitor`, so the operating system says when (no
  background polling). *Refresh drives* in the tree's context menu runs the same
  pass by hand.
- **A volume that is removed no longer leaves a dead row and a stuck tab.** Its
  row went on sitting in the tree, painted as a drive and leading into a folder
  that was gone; a tab inside it just showed an empty listing, and could not be
  left by going up, because its parent had gone too. The row and everything the
  tree remembered about it are dropped, any tab inside the volume moves back to
  the home folder, and the status bar names the volume that disconnected.
  Plugging the same stick back in gives a freshly scanned tree rather than the
  one the window last saw.
- **`/run/media` and `/Volumes` were never scanned.** The tree looked only at
  `/media` and `/mnt`, so on Fedora, RHEL, Arch and openSUSE — where udisks2
  mounts under `/run/media` — a stick was missing from the tree even after a
  restart, and on macOS, where every removable volume lands in `/Volumes`, no
  removable volume ever appeared at all. Both are covered now, together with the
  per-user directory level (`/media/bob/USB STICK`) for each.
- **The Cloud Storage lookup is repeated when a volume appears**, so a Google
  Drive that mounts as its own drive letter reaches the section instead of
  needing a restart. It ran exactly once, at tree build.
- Requires framework 0.3.91 (`UltraCanvasVolumeMonitor`, the folder watcher's
  failure callback, and the `UltraCanvasTreeView::RemoveNode()` fix that
  removing a populated drive row depends on).

#### 2026-09-01 *1.16.0*
- **The sub-folder search runs in the background and shows its matches while
  it walks.** Pressing "Search in sub folders" started a
  `recursive_directory_iterator` walk on the **UI thread**: the window stopped
  answering for as long as the tree took, which on a large volume was long
  enough for the desktop to report the application as not responding — and the
  walk followed reparse points, so a Windows profile's compatibility junctions
  ("Documents and Settings", "All Users") could send it round in circles until
  it fell over. The scan is now a worker thread with an explicit folder stack:
  symlinks and junctions are never entered, there is a depth cap on top of
  that, every directory error is contained rather than thrown, and the walk
  stops at 20 000 matches. Matches reach the display in batches roughly five
  times a second through the new
  `UltraCanvasFilerWidget::AppendToFileList()`, which keeps the scroll
  position and the selection — so results can be opened while the rest is
  still being found. The status bar counts matches and scanned folders as they
  arrive.
- **A "Scan sub folder" button inside the search field.** It appears as soon
  as there is something to search for and starts the same scan as Enter; while
  the scan runs it reads **Stop** and ends it, keeping what was found. The
  folder display's centered escalation button now carries the same wording.
  Editing the query, navigating, switching or closing the tab and clearing the
  field all end a running scan.
- Fixed: pressing Enter in the search field emptied the field. Dropping the
  as-you-type filter made the widget report a refresh, and the handler for
  that copied the widget's (now empty) filter text back into the field,
  because the tab was not yet marked as showing a search.

#### 2026-09-01 *1.15.1*
- **Fix: "UltraFiler crashed: exception 0xC00000FF … in ntdll.dll" /
  "exception 0x20474343 … in KERNELBASE.dll" on opening a folder.** Both
  dialogs are the Windows crash reporter's view of one thing: a C++ exception
  thrown where nothing could catch it — inside the window procedure, where an
  event handler on x64 cannot unwind back to `main()`'s `try`/`catch`, or on a
  background thread. Framework 0.3.90 catches the first at the window
  procedure and guards the filer widget's workers; this version does the same
  for UltraFiler's own threads. The subfolder probe that decides which tree
  folders get an expand button, the cloud-storage discovery and the Windows
  program launcher each catch what their work throws, log it (with the folder
  it happened on) and carry on. The probe and the tree's subfolder listing
  also step their directory iterators with the error-code overload: the
  range-for form throws when a read fails part-way through, which a removable
  or network drive can do at any time. An error in an event handler is now
  reported once in a dialog and logged after that; set
  `ULTRACANVAS_DEBUG_LOG` to a file path to capture the operation, file and
  error text if it recurs.

#### 2026-08-31 *1.15.0*
- **UltraFiler keeps its own changelog from here.** Everything up to and
  including this version shipped as part of a framework release and is recorded
  in [`Docs/UltraCanvas/CHANGELOG.md`](../UltraCanvas/CHANGELOG.md) — nothing
  was rewritten or moved, so that history stays where it was published. From
  now on a change to the file manager (`Apps/UltraFiler`) is described here and
  carries this file's version, and UltraFiler no longer moves when the
  framework releases.
- A framework change UltraFiler needs still belongs in the framework changelog.
  Cross-reference it from here when a release depends on it; never describe one
  change in two files under two version numbers.

<!--
Version source of truth: the first line of this file, format
`#### YYYY-MM-DD *x.y.z*`, read by cmake/UltraCanvasVersion.cmake.
-->
