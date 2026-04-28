#### 2026-04-28 *1.0.24*
- Fix Markdown toolbar (was not received events)
- Major rework in the UltraCanvas (implemented optimized rendering)

#### 2026-04-24 *1.0.23*
- New: XML syntax highlighting with xml/xsd/xsl/xslt/xhtml extension support.
- New: SVG syntax highlighting (inherits XML) with full SVG element and attribute vocabulary.
- New: POM (Maven) syntax highlighting (inherits XML) with Maven element vocabulary.
- New: RSS/Atom feed syntax highlighting (inherits XML) with feed element vocabulary.
- New: KML syntax highlighting (inherits XML) with Google Earth geographic vocabulary.
- New: JSON syntax highlighting with JSONC/GeoJSON/WebManifest variants.
- New: YAML syntax highlighting with full YAML 1.1/1.2 boolean-spelling constants.
- New: Texter "Data / Config" file filter covering JSON/XML/YAML/TOML/INI/POM/RSS/Atom/KML.
- New: Texter "Vector Graphics (Text)" file filter for .svg/.svgz.
- New: Texter New-Document dialog templates for SVG, POM, RSS, Atom, KML.
- Well-known filenames (pom.xml, manifest.json, feed.xml, atom.xml, rss.xml, site.webmanifest) now auto-select their dialect highlighter before falling back to plain extension lookup.

#### 2026-04-24 *1.0.22*
- Moved Markdown toolbar under the tabs bar so tabs bar deos not moved when Markdown toolbar switched on/off
- Implemented local 'Tab' key handling in the search/replace component to switch between 'search' and 'replace' inputs
- Fixed search/replace bug when replaced string has content of "search" string
- Fix search bar text colors/font size and other minor changes

#### 2026-04-24 *1.0.21*
- Fix tooltips rendering and file paths
- Fix Markdown cursor position on mouse click

#### 2026-04-23 *1.0.20*
- Some color fixes for Dark mode

#### 2026-04-17 *1.0.19*
- Fix restore opened tab after load
- Tabbed container active tab is not updated currently after load new file 

#### 2026-04-17 *1.0.18*
- Many updates in UltraCanvasTextarea
- Disable focus on TextArea in Markdown mode on file load and on tab switch if cursor at 0,0 position
- Do RestoreSessionAndRecoverBackups immediately without waiting splash screen to close.
- Fixed save all files on exit.

#### 2026-04-17 1.0.17
- Change design of Recent files menu. Increase max recent files to 30
- Migrate Texter's Recent Files submenu to the new API. The submenu now regenerates itself on every open from the recentFiles list.
- Fix Texter zoom shortcuts

#### 2026-04-16 1.0.16
- Full rework of display and rendering the text. Use Pango layout to format text. Allow to use variable height lines

#### 2026-04-06 1.0.15
- Fix markdown word-wrap rendering for blockquotes and code blocks

#### 2026-04-05 1.0.14
- restore all tabs on relaunch, fix duplicate "Recovered" titles
- Cursor line highlighting
- Fix splash screen appearing on wrong monitor in multi-monitor setups

#### 2026-03-28 *1.0.13*
New Features

- Inline Search/Replace bar (VS Code-style, UltraCanvasSearchBar) replacing the modal find dialog
- Async match counting with background thread and cancel support
- Search history and replace history persisted to config file
- Match count display ("3 / 12") with live update on text change
- Case-sensitive and whole-word toggle in search bar settings popup
- "First match" jump button added to search bar

Improvements

- onHistoryChanged callback persists search/replace history on every change
- Hex mode cursor byte offset used as search anchor in hex documents


#### 2026-03-25 *1.0.12*
New Features

- Drag-and-drop file open: HandleDragEnter, HandleDragOver, HandleDragLeave, HandleFileDrop
- Drop overlay rendered while external drag is hovering
- Recent files list with persistent storage and submenu rebuild
- Tab context menu
- EOL type dropdown in status bar (LF / CRLF / CR)
- Encoding dropdown in status bar with full re-encoding on save (BOM preserved)
- Language / syntax dropdown in status bar
- Zoom dropdown in status bar with configurable zoom percent steps
- TextEditorConfigFile — INI-based config with XDG, %APPDATA%, and macOS paths
- Session restore on startup (reopens previously open files + active tab index)

Improvements

- Encoding detection: BOM check → UTF-8 validation → heuristics (DetectEncoding)
- Binary file auto-detection → opens directly in Hex mode
- Raw bytes cached per document (up to 10 MB) for lossless re-encoding on save
- Windows .rc resource script with version info embedded in EXE (1.0.12)


#### 2026-03-17 *1.0.11*
New Features

- Hex / Binary editor mode (TextAreaEditingMode::Hex) with address panel, hex panel, ASCII panel
- Hex undo/redo stack (separate from text undo stack)
- Hex cursor navigation (nibble-level, row/column, page up/down, panel toggle)
- Hex search (byte-level find/find-next)
- Hex selection with mouse
- MarkdownHitRect::isAnchorReturn — ↩ backlink icon on {#id} headings scrolls to jump source

Improvements

- SetBounds() override triggers layout recalculation on resize


#### 2026-03-11 *1.0.10*
New Features

- AutosaveManager — periodic backup every 60 seconds for modified documents
- Crash recovery dialog on startup: offers to restore autosaved files
- Session + backup restore merged into RestoreSessionAndRecoverBackups()
- Legacy autosave path migration (/tmp/UltraTexter/Autosave/)
- AutosaveDocument() per-tab with stable backup path derived from file path + tab index


#### 2026-03-02 *1.0.09*
New Features

- Backlink icons (↩) on {#id} headings — click returns to the line that jumped here
- Anchor backlink map (markdownAnchorBacklinks) built during prescan
- Abbreviation support (*[abbr]: expansion) — hover tooltip, word-boundary aware
- Footnote support ([^label]: content) — multi-line continuations, inline rendering
- Definition list support (term / : definition pairs, lazy continuation merging)
- Emoji shortcode rendering (:fire:, :check:, :computer: etc.)
- Table word-wrap: cell text wraps inside columns with markdown-aware font measurement
- Code span font switching inside table cells for correct width measurement
- sAnchorReturn hit rect type for ↩ backlink icons

Improvements

- H1–H6 font size clamped to line height with per-level ceiling multipliers (prevents overflow)
- Header text vertically centred within fixed row height
- Blockquote continuation segments rendered with quote indent
- List item continuation segments rendered with list indent
- Abbreviation definition lines and footnote definition lines hidden from rendering (except on cursor line)
- Definition hidden/continuation lines suppressed from rendering
- mdScrollBaseOffset computed from markdownLineYOffsets[firstVisibleLine] for correct table row positioning
- Tokenizer cache: re-uses token vector across display lines from the same logical line


#### 2026-02-26 *1.0.08*
New Features

- Initial standalone Markdown plugin component (UltraCanvas::MarkdownElementType enum)
- Element types: Text, Header, Bold, Italic, Code, CodeBlock, Link, Image, and more


#### 2026-02-21 *1.0.07*
New Features

- Self-contained inline search/replace bar (embedded panel, not a dialog)
- Find mode (single row) and Replace mode (double row)
- Settings popup menu (case sensitive, whole word checkboxes)
- Search history popup and replace history popup
- onFindNext, onFindPrevious, onReplace, onReplaceAll, onSearchTextChanged, onHistoryChanged callbacks
- UpdateMatchCount(current, total) display label


#### 2026-02-14 *1.0.06*
New Features

- DetectEncoding() — BOM + UTF-8 validation + heuristics
- DetectBOM() — UTF-8 BOM, UTF-16LE/BE BOM detection
- ConvertToUtf8() / ConvertFromUtf8() via POSIX iconv
- GetSupportedEncodings() — full encoding list for dropdown
- MAX_RAW_BYTES_CACHE = 10 MB per document
