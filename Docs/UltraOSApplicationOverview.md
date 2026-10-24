# ULTRA OS applications: an overview

A short summary of every application in `Apps/`: what each one is for, what
it can do, which modules it is built on and how far it has come. The facts
come from each application's README and docs, the first line of its
changelog and its build file. The modules themselves are summarised in
[UltraOSModuleOverview.md](UltraOSModuleOverview.md).

Every application versions itself: the first line of its changelog,
`Docs/<App>/CHANGELOG.md`, is its version. The versions below are those
first lines as of 2026-10-18.

---

## Overview table

| Application | What it is | Version | Main modules |
|---|---|---|---|
| **Creative and media** | | | |
| [UltraPaint](#ultrapaint) | Bitmap editor (Paint Shop Pro / GIMP class) | 0.2.25 | PaintSurface, PixelFX |
| [ArtCreator](#artcreator) | Vector drawing editor (Xara Designer / ArtWorks class) | 0.7.3 | VectorEdit, Vector plugin |
| [UltraVideo](#ultravideo) | Video editor with timeline and one-button export | 0.7.0 | VideoFX |
| [UltraViewer](#ultraviewer) | Universal media viewer | 1.0.7 | MediaViewer |
| **Documents and files** | | | |
| [Texter](#texter) | Text, code and word-processing editor | 1.67 | TextArea, RichTextEdit, FileLoader |
| [UltraFiler](#ultrafiler) | Windows Explorer-style file manager | 1.75.0 | FilerWidget, VirtualFS, UltraCloud |
| [AnchorPoint](#anchorpoint) | Peer-to-peer file transfer | 0.2.4 | UltraCrypt |
| **Web and publishing** | | | |
| [UltraWeb](#ultraweb) | Browser for web pages and WebAssembly apps | 0.8.3 | HTMLReader, WasmHost, UltraNet |
| [UltraPress](#ultrapress) | Website and shop builder (WordPress class) | 0.8.2 | UltraCloud, UltraNet |
| **Communication** | | | |
| [UltraMail](#ultramail) | Email client | 0.10.59 | UltraNet, UltraDatabase, UltraVault, UltraMessage |
| [EmailCleaner](#emailcleaner) | Mailbox analysis and clean-up | 0.4.8 | UltraMail engine, UltraDatabase |
| [UltraSocial](#ultrasocial) | Cross-posting to social networks | 0.2.2 | UltraNet, UltraDatabase, UltraVault |
| **Security** | | | |
| [UltraPassword](#ultrapassword) | Password vault | 0.1.4 | UltraCrypt |
| [UltraAuthenticator](#ultraauthenticator) | TOTP / HOTP two-factor codes | 0.2.8 | UltraCrypt |
| **AI** | | | |
| [UltraAIApp](#ultraaiapp) | Dashboard for the UltraAI services | 0.1.15 | UltraAI |
| [UltraClaude](#ultraclaude) | Desktop chat with Claude on a Pro / Max subscription | 0.4.2 | core |
| **Business** | | | |
| [UltraFIBU](#ultrafibu) | German double-entry bookkeeping | 0.24.5 | UltraDatabase, UltraCrypt |
| **Desktop and system** | | | |
| [UltraDesktop](#ultradesktop) | The ULTRA OS desktop | 0.4.1 | DesktopShell, ClipboardHistory, UltraMessage |
| [UltraStore](#ultrastore) | The app store: signed catalog, per-user installs, updates | 0.1.0 | UltraNet, UltraCrypt, UltraDatabase |
| [UOS-Settings](#uos-settings) | System settings | 0.5.0 | core |
| [UltraClipboard](#ultraclipboard) | Clipboard history | 0.1.5 | ClipboardHistory |
| [UltraCleaner](#ultracleaner) | Removes caches, temporary files and leftovers | 0.66 | core |
| [UltraNetMonitor](#ultranetmonitor) | Which process holds which network connection | 0.17 | NetworkMonitor |
| [DeviceExplorer](#deviceexplorer) | Printers, scanners and cameras | 0.2.2 | IODeviceManager |
| [UltraWinManager](#ultrawinmanager) | Runs Windows applications on Linux | 0.1.2 (UltraWin) | UltraWin |
| [UltraWinSetup](#ultrawinsetup) | Command line for the UltraWin VM tier | 0.1.2 (UltraWin) | UltraWin |
| [UltraMessageCli](#ultramessagecli) | Command line for the UltraMessage bus | no changelog | UltraMessage |
| **Developer** | | | |
| [UltraCanvasStart](#ultracanvasstart) | Sets a computer up for UltraCanvas development | 0.4.3 | core |
| [DemoApp](#demoapp) | Showcase of every element, chart and format | 0.9.337 (framework) | everything |
| [CSSLayoutTests](#csslayouttests) | Tests for the CSS layout engine | no changelog | CSSLayout |

---

## Creative and media

### UltraPaint

**Purpose:** a bitmap editor with layers, selections, brushes and shapes. Its
feature-gap analysis aims it at the Paint Shop Pro / GIMP class.

**Capabilities:**
- A grouped tool palette: selections (including lasso and magic wand),
  brushes, airbrush, lines, shapes, smudge, clone, dodge / burn, fill,
  gradient, text.
- Layers with opacity, blend modes and locks, plus text layers.
- Adjustments: levels, curves, hue / saturation, posterise, colour to alpha.
- Filters: blurs, sharpen, edge detection, noise, pixelate, oil paint,
  erode / dilate.
- Opens and saves every raster format FileLoader lists. Layered files are
  saved as `.ucd` (UCD 2.1) or `.ucraster`.
- Imports vector drawings and 3D models, pastes from the clipboard history,
  and has a magnifier.

**Built on:** `UltraCanvasPaintSurface`, PixelFX (libvips), the clipboard
history and the Models plugin (both optional).
**Version:** 0.2.25 (2026-10-16).

### ArtCreator

**Purpose:** a vector drawing editor of the Xara Designer / ArtWorks class,
built on the framework's vector editing layer, which other editors can
reuse.

**Capabilities:**
- 19 tools, including selector, shape editor, pen, freehand, line,
  rectangle, ellipse, polygon / star and text.
- Effects: fill and transparency ramps, shadow, feather, contour, bevel,
  blend, mould.
- Rulers, guides, grid and snapping. Arrange, group, align and layers.
- Icon mode: pixel grid, snapping to pixels, a preview, export at icon sizes.
- Opens SVG, XAR, EMF, WMF, DXF and DWG. Saves XAR (native), SVG, DXF, EMF
  and WMF. Exports PDF, AI, EPS, CDR and bitmaps at any size.

**Built on:** VectorStorage, VectorEdit, the Bézier path model, the vector
canvas, the gradient editor and the Vector plugin.
**Version:** 0.7.3 (2026-10-17).
**Notes:** without the Vector plugin it can draw but has no file formats.
The later phases are listed in `Docs/Research/ArtCreatorVectorCanvasProposal.md`.

### UltraVideo

**Purpose:** a video editor for ULTRA OS and the other UltraCanvas desktops:
clips, photos and music on one timeline, a live preview and one-button
export.

**Capabilities:**
- A media bin for video, photos (including HEIC and camera raw) and songs.
- A viewer with scrubbing, playing through proxies on a background thread.
- A timeline with titles, an overlay-video lane, a voice-and-sound lane,
  music, transitions and fades.
- A "Make a story" wizard and an inspector with Clip, Audio and Project tabs.
- Undo / redo. Projects are `.ultravideo` JSON files, which the `videofx
  render` command can also render.
- Export presets: MP4 (720p, 1080p, 4K), WebM, GIF, ProRes, MP3. Export runs
  in the background and can be cancelled.

**Built on:** VideoFX (FFmpeg), the video timeline and timecode field
elements, and GStreamer for the draft player.
**Version:** 0.7.0 (2026-10-16).
**Notes:** it is not built without VideoFX. The roadmap is in
`UltraVideoDesignProposal.md` §11.

### UltraViewer

**Purpose:** a universal media viewer: one full-window
`UltraCanvasMediaViewer` that shows every kind of media the framework
reads.

**Capabilities:**
- Bitmaps (JPEG, PNG, animated GIF, WebP, HEIC, AVIF, JXL, PSD, EXR and
  more) through libvips, and SVG through librsvg.
- Video and audio playback with play, pause, seek and volume.
- PDF, eBooks (EPUB, FB2, MOBI, AZW), spreadsheets (ODS, CSV), STL models,
  text and Markdown.
- Browsing a folder or a playlist, a slideshow with transitions, drag and
  drop.
- Image tools: zoom, rotate, mirror, colour adjustments, sharpen, save as.

**Built on:** MediaViewer, PDFView, EBookViewer, the spreadsheet and STL
elements.
**Version:** 1.0.7 (2026-10-14).
**Notes:** `.ucd` files show only their embedded thumbnail for now. Full
rendering is planned for the UCD v2 engine.

---

## Documents and files

### Texter

**Purpose:** UltraTexter, a text editor with tabs and autosave that also
edits word-processing documents.

**Capabilities:**
- Tabs, autosave (every 60 s by default), and one running copy (a second
  start's files open in it).
- Syntax highlighting and a Markdown toolbar and side bar.
- Word-processing tabs for `.odt` and `.docx` (and `.doc`, read only):
  styles, tables with merged cells, footnotes, comments, tracked changes,
  columns.
- Spell checking, Find / Replace, a search-as-you-type bar, Go to line.
- Character encodings through iconv, and file statistics.
- PDF view (through MuPDF, optional) and printing.

**Built on:** TextArea, RichTextEdit, TabbedContainer, PDFView, FileLoader.
**Version:** 1.67 (2026-10-14).
**Docs:** [Apps/Texter/README.md](../Apps/Texter/README.md).

### UltraFiler

**Purpose:** a Windows Explorer-style file manager built entirely from
UltraCanvas elements.

**Capabilities:**
- Browser-style tabs, a folder tree loaded on demand, a breadcrumb path
  bar, view and sort settings per folder.
- Views: details, list, thumbnails, size bars, treemap. A split view shows
  two folders, with drag and drop between them.
- A preview pane for images, video, audio, PDF, spreadsheets, 3D models and
  text.
- Search that filters as you type, a background scan of sub-folders,
  type-ahead, History and Favorites.
- Remote drives: FTP, FTPS, SFTP, Dropbox, OneDrive, Google Drive,
  Nextcloud and WebDAV, with a connection log and cached listings.
- RAM discs, packing and unpacking archives, finding text in files.

**Built on:** FilerWidget, TreeView, MediaViewer, SplitPane, VirtualFS,
UltraCloud (optional).
**Version:** 1.75.0 (2026-10-17).
**Notes:** Windows, Linux and macOS. Without UltraCloud there are no remote
drives. On Windows without ImDisk a RAM disc is only a folder.

### AnchorPoint

**Purpose:** peer-to-peer file transfer directly from one computer to
another, with no file storage on a server.

**Capabilities:**
- A simple transfer protocol (Hello / Offer / Accept / Chunk / Done /
  Verified) with 256 KiB chunks and a SHA-256 check of the whole file.
- Interrupted transfers resume where they stopped.
- Incoming file names are cleaned, so a peer cannot write outside the save
  folder.
- A command-line program and a window with live progress.

**Built on:** UltraCrypt, plain sockets behind a transport interface.
**Version:** 0.2.4 (2026-10-14).
**Notes:** works on a local network. Internet use (signalling, STUN hole
punching, a relay, end-to-end encryption, pairing codes) is planned for
milestone 3.

---

## Web and publishing

### UltraWeb

**Purpose:** the ULTRA OS browser. It shows HTML / CSS pages as native
UltraCanvas elements, and runs WebAssembly apps that build their UI from
UltraCanvas elements.

**Capabilities:**
- Web pages through the framework's HTML reader: character sets, style
  sheets, pictures, links, back and forward.
- Forms (POST), file uploads, downloads and a downloads list.
- Tabs with their own history.
- A sandbox for WebAssembly apps: timers, fetch under browser rules, storage
  per site, clipboard writes.
- `--check` and `--page` modes that run without a window.

**Built on:** WasmHost (wasmtime), UltraNet, the HTML page loader and
`UltraCanvasWebView`.
**Version:** 0.8.3 (2026-10-16).
**Notes:** no JavaScript yet. Bookmarks, history, per-app permissions and
packaged macOS / Windows builds are still to come.

### UltraPress

**Purpose:** the website and shop builder of ULTRA OS, of the WordPress
class. Published pages are meant to be rendered by the framework compiled to
WebAssembly, with a plain HTML version of every page for search engines and
screen readers.

**Capabilities:**
- One `ultrapress` program: commands (`new`, `check`, `page`, `publish`,
  `import`, `hosts`) and an editor window.
- A block editor (heading, text, rich text, hero, picture, columns, card,
  features) with a live re-render, and text edited in place on the page.
- A site is a folder of JSON files, published as static files.
- A WordPress export importer and a media library.
- Uploads only the changed files to an FTP, SFTP, WebDAV or Nextcloud host.

**Built on:** its own engine, renderer and hosting libraries, UltraCloud,
UltraNet.
**Version:** 0.8.2 (2026-10-16).
**Notes:** phase 1. The WebAssembly renderer, new pages and menus from the
window, undo, newsletters and the phase 2 server side (forms, sign-ups) are
not built yet.

---

## Communication

### UltraMail

**Purpose:** the ULTRA OS email client for everyday mail accounts.

**Capabilities:**
- An account wizard that finds server settings itself (provider presets and
  Mozilla autoconfig), and OAuth2 browser sign-in for Gmail, Outlook and
  Yahoo.
- IMAP sync into a local store, background sync, flags kept in step both
  ways.
- A composer with Reply, Forward, rich text and signatures. Sending goes
  through an outbox that retries.
- Sender badges (contact, business, new, advertisement, spam, scam) and a
  check of each message for phishing and known scam patterns.
- HTML mail drawn natively through the CSS layout engine (no web view),
  attachments in the media viewer, inline images.
- An address book, one-click unsubscribe, copy buttons for one-time codes,
  new-mail alerts on the desktop feed.

**Built on:** UltraDatabase, UltraNet (IMAP / SMTP / POP3 / JMAP plugins),
UltraVault, UltraCrypt, UltraMessage, UltraCloud (optional, for cloud links),
HTMLReader.
**Version:** 0.10.59 (2026-10-17).
**Notes:** the docs describe it as phase 1–2, in progress.

### EmailCleaner

**Purpose:** analyses mailboxes to show who fills them and with what kind of
mail, for cleaning up cluttered or spam-heavy accounts.

**Capabilities:**
- Loads several accounts into an analysis database, from UltraMail's cache
  or from accounts added in EmailCleaner (app password).
- A keyword classifier for advertising, adult content, romance scams,
  phishing, financial fraud and executable attachments. It undoes
  obfuscation tricks first, and its rules are in an editable file.
- A sender map (treemap by sending domain) and a timetable (weekday × hour
  heatmap, traffic over time).
- A detail view with the evidence behind each verdict.
- Actions on a selected block: block the sender, move to trash, unsubscribe.

**Built on:** the UltraMail engine, UltraDatabase, UltraNet, UltraVault.
**Version:** 0.4.8 (2026-10-13).
**Notes:** accounts that need OAuth have to be set up in UltraMail first.

### UltraSocial

**Purpose:** write one post and publish it to several social networks.

**Capabilities:**
- One draft, adapted to each network (character limits, cutting at word
  boundaries, media and caption limits).
- Mastodon, Bluesky, Telegram, Reddit, X, LinkedIn and Facebook Pages.
- OAuth2 / PKCE sign-in, or pasted tokens and app passwords.
- Image posts on Mastodon, Bluesky, X and Facebook; Telegram photos and
  albums.
- "Post later" with a queue and up to 5 retries, and a history of recent
  posts with links.

**Built on:** UltraNet (HTTP, OAuth2), UltraDatabase, UltraVault, UltraCrypt.
**Version:** 0.2.2 (2026-10-12).
**Notes:** Instagram, Reddit media, LinkedIn images and AI adaptation of
drafts are still open.

---

## Security

### UltraPassword

**Purpose:** the ULTRA OS password vault: every password in one encrypted
file, organised in a tree of groups.

**Capabilities:**
- Groups nested to any depth, a card per password, search across the vault.
- Records how each site signs you in, with a rating from Weak to Very
  strong.
- A password generator with a strength meter. Copied passwords are cleared
  from the clipboard after 30 seconds.
- Argon2id + XChaCha20-Poly1305, atomic writes, file mode 0600.
- Locks on demand, after 5 minutes idle or when minimised, with a growing
  wait after wrong passwords.
- Export as a vault file or CSV. Import from Chrome, Edge, Firefox or
  Bitwarden CSV.

**Built on:** UltraCrypt.
**Version:** 0.1.4 (2026-10-13).
**Notes:** its docs say it cannot protect against malware running as the
same user while the vault is unlocked.

### UltraAuthenticator

**Purpose:** generates offline TOTP / HOTP sign-in codes (the codes Google
Authenticator and Aegis produce), kept in a vault behind a master password.

**Capabilities:**
- Adds accounts by QR code (camera, image file or screen) or by typing the
  setup key, with a strict `otpauth://` parser.
- An encrypted vault (Argon2id + XChaCha20-Poly1305). Seeds are wiped from
  memory right after each code is computed.
- A lock screen with a growing wait, locking when idle or minimised, and an
  option to hide codes until clicked.
- A password-protected "Show key" to move an account to another device.
- Encrypted backup and restore with its own passphrase.

**Built on:** UltraCrypt, libzbar for QR decoding, the camera support.
**Version:** 0.2.8 (2026-10-13).
**Notes:** it depends on a correct system clock and cannot recover a lost
master password.

---

## AI

### UltraAIApp

**Purpose:** the dashboard for the UltraAI module: a grid of service tiles,
each opening a dialog that tries out one AI capability.

**Capabilities:**
- Ten services: chat, embeddings, speech to text, text to speech,
  translation, image generation, vision, video generation, music generation,
  code assist.
- A chat dialog and a translator dialog (source language, formality).
- A settings window to choose providers, allow a cloud fallback and edit
  endpoints.
- Uses the module's providers: Anthropic, OpenAI-compatible servers, local
  Qwen / Ollama / llama.cpp, ComfyUI, MiniMax, and mocks.

**Built on:** UltraAI (with UltraNet and UltraVault).
**Version:** 0.1.15 (2026-10-16).
**Notes:** described in its build file as a dashboard demo. Services run
locally by default.

### UltraClaude

**Purpose:** a desktop chat window for Claude that runs on a Claude Pro or
Max subscription. It drives the official Claude Code CLI and never holds an
API key or token itself.

**Capabilities:**
- Sign-in through `claude auth login`, and log out from the app.
- A list of chats, each resuming the same CLI session in its own folder.
- A choice of model, permission mode (Ask / Accept edits / Plan / Allow
  everything) and working folder.
- Answers streamed as they arrive, with a Stop button.
- A badge per chat counting the lines not yet in the repository's default
  branch.
- Headless `--print`, `--list-chats` and `--count-lines` modes.

**Built on:** the UltraCanvas core. It runs the CLI as a child process.
**Version:** 0.4.2 (2026-10-15).
**Notes:** needs `claude` on the PATH.

---

## Business

### UltraFIBU

**Purpose:** German double-entry bookkeeping (Finanzbuchhaltung) with a
German interface, for small businesses.

**Capabilities:**
- Company setup with any fiscal-year start, SKR03 / SKR04 charts of
  accounts, tax keys.
- Documents (Belege) through draft, posting, payment and reversal. Journal,
  account balances, a GoBD audit log, period locking.
- DATEV import and export with file checking.
- Bank statement import and payment matching.
- UStVA calculation with an ELSTER XML file, and One-Stop-Shop.
- Customers and suppliers with EU VAT-ID checks, invoice PDFs, and an
  `ultrafibu` command line.

**Built on:** UltraDatabase (SQLite, or PostgreSQL for several users),
UltraCrypt, UltraVault, the PDF writer.
**Version:** 0.24.5 (2026-10-15).
**Notes:** it does not file tax returns itself. The ELSTER output is an XML
file for manual upload; the ERiC library is not included.

---

## Desktop and system

### UltraStore

**Purpose:** the ULTRA OS app store: lists the UltraCanvas applications
from a signed catalog, installs the build for the machine with the shared
libraries it needs, keeps everything up to date and removes it again. One
program, `ultrastore`: the window when started plainly, the commands when
given one.

**Capabilities (0.1.0, the core and the command line):**
- Knows the machine: os, processor, OS version, glibc, distribution, Rosetta.
- Reads the catalog of apps and the runtime / library components they run
  on, each release with a package per target and the exact component
  versions it was built against, plus the admin backend's settings:
  minimum OS version, pricing, update period, visibility, lifecycle, flags.
- Checks the catalog's Ed25519 signature against the trusted keys and refuses
  a catalog older than the one already seen; checks packages by SHA-256.
- Picks the package for the machine and lays out the install plan with the
  shared components first, skipping what is installed.
- Fetches the catalog from the first mirror that answers and caches it for
  offline use.
- Next: UltraInstaller (download, unpack, registry), the window, the admin
  backend, payment. Design: `Docs/UltraStore/README.md`.

### UltraDesktop

**Purpose:** the ULTRA OS desktop: one screen-sized window under all other
windows, holding the taskbar, the desktop organiser, the info panel and the
wallpaper.

**Capabilities:**
- A taskbar (left by default, or top or bottom) with system buttons and the
  running applications.
- A right bar with virtual desktops, a sticker board, notes and a screenshot
  button.
- Records every copy into an encrypted clipboard history (leaving out copies
  password managers mark as secret), with a quick panel on `Super+V`.
- An info panel: mail count, network traffic, VPN, Bluetooth, Wi-Fi, USB,
  keyboard, webcam, microphone, battery.
- Draws notifications as toasts (UltraMessage is the notification server).
- On Windows it can run beside Explorer or instead of it, and you can always
  switch back.

**Built on:** DesktopShell, ClipboardHistory, UltraMessage and the message
centre.
**Version:** 0.4.1 (2026-10-15).
**Notes:** back ends for X11 (Linux, BSD) and Win32. On macOS it runs, but
the running-apps group stays empty.

### UOS-Settings

**Purpose:** the ULTRA OS settings application, for settings that belong to
the system rather than to one app.

**Capabilities:**
- A page tree with the selected page beside it. There is no Apply button:
  every change is saved at once.
- Desktop: taskbar edge, wallpaper, RAM disc folder, file manager, number of
  virtual desktops.
- On Windows: what starts at sign-in (the Windows desktop, UltraDesktop
  beside Explorer, or UltraDesktop instead of Explorer).
- File dialogs: one shared last-used folder or one per application.
- Devices: the scanner and printer certificates trusted on first use, which
  can be forgotten.

**Built on:** the UltraCanvas core and UltraDesktop's settings code.
**Version:** 0.5.0 (2026-10-15).

### UltraClipboard

**Purpose:** the full clipboard history viewer: find, reuse, edit, save or
pin anything you copied.

**Capabilities:**
- Pinned entries first, then Today / Yesterday / Earlier, with thumbnails.
- Copy, Edit, Save as and Delete on every entry, with Undo for 8 seconds.
- Search (ignoring case and accents) and filters: text, images, files,
  links, pinned.
- Save as text, HTML, ODT or DOCX, any image type, or as the copied files.
- Shares its history with UltraDesktop's quick panel, and records while it
  is open where no UltraDesktop is running.

**Built on:** ClipboardHistory.
**Version:** 0.1.5 (2026-10-14).

### UltraCleaner

**Purpose:** finds and removes the leftovers macOS, Windows and Linux leave
behind, and shows the exact paths before it deletes anything.

**Capabilities:**
- An overview with a usage chart per drive.
- Rule-driven clean-up of temporary files, caches, logs, crash reports,
  thumbnails, package downloads, build leftovers and the trash.
- Only removes paths a rule named, and checks each one twice (when found and
  when removed). Modes: simulate, move to trash, delete permanently.
- Photo albums: identical files, identical images, and similar shots found
  by perceptual hashing.
- On Windows: registry entries that point at missing files, removed after a
  backup, with restore.
- A command line and tests share the same engine.

**Built on:** its own headless engine, the UltraCanvas core for image
decoding.
**Version:** 0.66 (2026-10-17).

### UltraNetMonitor

**Purpose:** shows live which processes hold which network connections, the
view `ss -p` gives, in a window. It only observes and never blocks traffic.

**Capabilities:**
- A live process list beside a connection list, refreshed every second,
  sortable and filterable.
- A "Via" column that follows loopback chains through local proxies, VPNs
  and antivirus software.
- Host names, and open / close events.
- Recording into a history (hour, day, week, month) with roll-ups after 30
  days.
- CSV export, and many command-line options (`--list`, `--by-app`,
  `--history` and others).

**Built on:** NetworkMonitor, UltraDatabase (SQLite).
**Version:** 0.17 (2026-10-15).
**Notes:** without elevation only your own processes are attributed. Byte
counts exist only on Linux.

### DeviceExplorer

**Purpose:** the user interface of IODeviceManager: shows the printers,
scanners and cameras connected to the computer.

**Capabilities:**
- A device tree, groupable by category and filterable, with details
  (General, Connection, Status).
- Scans on a worker thread, then follows hot-plugging.
- Asks a selected printer for its status and ink / toner levels.
- Back ends: CUPS, the Windows spooler, SANE, V4L2, and IPP / eSCL network
  devices.
- Read-only: it never prints, scans or changes a setting.

**Built on:** IODeviceManager.
**Version:** 0.2.2 (2026-10-15).
**Notes:** local scanners and cameras only on Linux. Audio, storage, serial,
Bluetooth and GPIO devices wait on new IODeviceManager back ends.

### UltraWinManager

**Purpose:** the graphical front end for running Windows applications on
ULTRA OS through the UltraWin module.

**Capabilities:**
- Environments: isolated Wine prefixes, component installs, launching a
  `.exe` or `.msi` into an environment.
- Programs: the installed Start-menu programs of all environments, started
  by double-click.
- Windows VM: provisioning from install media, start / suspend / resume /
  stop.
- Shows which engines the host has (Wine, winetricks, QEMU, KVM) and how to
  get the missing ones.

**Built on:** UltraWin.
**Version:** follows the UltraWin changelog, 0.1.2 (2026-10-12).
**Notes:** Linux / ULTRA OS only. The Wine tier works; the VM tier has not
been validated on real hardware yet.

### UltraWinSetup

**Purpose:** `ultrawin-setup`, a command-line tool that drives the UltraWin
VM tier.

**Capabilities:** `status`, `provision` from a Windows ISO and the virtio
drivers, `start` / `stop` / `suspend` / `resume` / `watch`, `run` a program
in the VM or under Wine, install components, list programs.

**Built on:** UltraWin (no UI).
**Version:** follows the UltraWin changelog, 0.1.2 (2026-10-12).
**Notes:** Linux only. You supply your own Windows licence and ISO.

### UltraMessageCli

**Purpose:** `ultramsg`, the command line for the UltraMessage bus, for shell
scripts and tests (the equivalent of `osascript` or `busctl`).

**Capabilities:** `post` a message, `tail` live messages, `query` the
journal, list `conversations` and `endpoints`, `mark-read`, `dismiss`,
`delete`, `export`. It starts a broker itself if none is running.

**Built on:** UltraMessage (no UI).
**Version:** no changelog (source header 0.1.0).
**Notes:** local to one user session; it is not a network protocol.

---

## Developer

### UltraCanvasStart

**Purpose:** sets a computer up for UltraCanvas development: the application
form of [GettingStarted.md](GettingStarted.md), for programmers on a machine
with nothing installed yet.

**Capabilities:**
- Six steps: your computer, features, tools (checked and installed),
  framework (prebuilt SDK or built from source), project, build.
- Writes a project skeleton: `CMakeLists.txt`, `main.cpp`, presets, README,
  and an instruction file for the AI assistant you choose.
- A headless command line (`--check`, `--plan`, `--create`) that CI runs.
- Shipped as standalone archives for Linux, Windows and macOS.

**Built on:** its own engine library on the UltraCanvas core.
**Version:** 0.4.3 (2026-10-17).
**Notes:** it can only check and install for the platform it runs on.

### DemoApp

**Purpose:** the framework's showcase (`UltraCanvasDemo`): every element,
chart, diagram, module and file format, one page at a time.

**Capabilities:**
- 131 example pages: input elements, menus, pickers, tree and list views,
  tabs, the spreadsheet, the ODT and rich-text editors.
- Chart and diagram pages, media and format pages (SVG, CDR, DWG, barcodes,
  audio, PixelFX, AudioFX, AI).
- OpenGL pages (models, shaders).
- The message centre and notification toasts.
- Builds for the browser through Emscripten.

**Built on:** the whole framework and every plugin.
**Version:** the framework version, 0.9.337 (2026-10-18).

### CSSLayoutTests

**Purpose:** a test program without UI for the CSS layout engine, for
framework developers.

**Capabilities:** 35 test cases covering block stacking, absolute / fixed /
relative positioning, Flex (grow, wrap, gaps, nesting), Grid (fixed, `fr`
and auto tracks, spans), invalidation and caching, and intrinsic sizing.

**Built on:** CSSLayout.
**Version:** no changelog (source header 1.4.0).
