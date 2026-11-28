# UltraTexter

A text and code editor with tabs, built on UltraCanvas, that also edits
word-processing documents (`.odt`, `.docx`, `.doc`) as they look. The program
file is `Texter` (`Texter.exe`, `Texter.app`); the window title shows
`UltraTexter <version>`.

This app versions itself: [`Docs/Texter/CHANGELOG.md`](../../Docs/Texter/CHANGELOG.md).

## What it does

- **Tabs and windows.** Many files in tabs, *New Window* / *Open in New
  Window*, *Close Other Tabs*, recent files and recent folders, *New from
  Template*, drag and drop of files onto the window.
- **One copy per user.** Starting UltraTexter again opens the files in the
  running copy, in a new tab of the window used last, or switches to the tab
  that already shows them (`UltraCanvasSingleInstance`).
- **Autosave and crash recovery.** Open documents are backed up every 60
  seconds; after a crash they come back as *Recovered* tabs.
- **Code.** Syntax highlighting for 48 languages (C/C++, Python, Java,
  JavaScript / TypeScript, Rust, Swift, Kotlin, PHP, Ruby, Lua, SQL, Shell,
  HTML, CSS, XML, JSON, YAML, Markdown, assembly, Pascal and more), picked by
  extension or from the status bar. Line numbers, word wrap, zoom, light and
  dark look - the desktop's, and *View > Theme* picks the other one for
  every UltraCanvas application at once (it writes `Appearance.conf`, the
  setting UOS-Settings' Look page keeps). Under the dark look the text can
  still sit on a light page: *View > Dark Mode: Document Background* picks
  dark, light grey or white.
- **Markdown.** A Markdown toolbar and side bar (headings, emphasis, lists,
  checklist, links, images, tables), with the Markdown rendered live in the
  editor (`UltraCanvasTextArea`).
- **Word-processing documents.** `.odt`, `.docx` and `.doc` open in the
  WYSIWYG editor (`UltraCanvasRichTextEdit`): fonts, sizes, colours,
  alignment, styles, embedded images, tables with merged and editable cells.
  Saving writes the same document back, so formatting survives a round trip.
  *View > Navigator (documents)*, or F5, shows the framework's navigator at
  the tab's left: the headings as a tree that follows the caret (promote,
  demote and move chapters), the tables, pictures, comments, notes and
  tracked changes, and a search box that filters them.
- **Presentations as an outline.** A PowerPoint deck (`.pptx`, `.pptm`,
  `.ppsx`, `.ppsm`, `.potx`, `.potm`) or an OpenDocument one (`.odp`, `.otp`,
  `.fodp`) opens in the same editor as the outline of its slides - each
  slide's number and title, its bullets, tables, pictures' descriptions and
  speaker notes. It can be saved as `.odt`, `.docx`, Markdown or text, or as
  a new `.pptx` or `.odp`; saving the outline under the deck's own name is
  refused, so the deck's slides are never replaced by their outline.
- **Documents as presentations.** Any document saved as `.pptx` or `.odp` is laid out
  as a deck: each first-level heading starts a slide and is its title, the
  paragraphs and lists below it are the slide's text, tables and pictures
  stand under them. A deck's own outline saved this way comes back as its
  slides, hidden slides and speaker notes included.
- **Password-protected documents.** A `.docx`, `.odt`, `.pptx` or `.odp`
  saved with a password (Word, PowerPoint, LibreOffice - its ODF 1.4
  encryption too) asks for the password and opens; a wrong one is asked
  again. Saved again as `.docx` it keeps the password; an `.odt` opened with
  one is not saved without it (save as `.docx` to keep it protected).
- **Find and replace.** *Find…*, *Replace…*, a search-as-you-type bar and
  *Go to Line…*, in text and word-processing tabs alike.
- **Spell checking** (*Edit → Spelling*). The status bar shows the
  dictionary the active document is checked against (*Spelling: English
  (United States)*, or *Spelling: off*); a click on it opens the Spelling
  menu. A document mostly flagged in that language switches to the installed
  dictionary that fits it (*Detect Language Automatically*), and the status
  bar follows. Skipped in the hex view and in PDFs.
- **Encodings and line ends.** The character encoding and the line ending
  (LF, CRLF, CR) of every file are shown and can be converted, through iconv.
  Binary files open in a **Hex/Binary** view.
- **PDF.** PDFs open in the PDF view, and any tab can be exported as a PDF.
  This needs MuPDF; a build without it opens no PDFs and says so.
- **Printing** of text and word-processing tabs, and **File Statistics**
  (lines, words, characters and more).

## Command line

```
Texter [options] [file...]

  -h, --help        Show this help message
  -v, --version     Show version information
  -d, --dark        Start with the dark look for this run (the desktop's look otherwise)
  -l, --lang LANG   Set syntax highlighting language
```

While UltraTexter is running, starting it again opens the files in the
running copy; the options apply to a first start only.

## Build

UltraTexter is built by the root build (`BUILD_TEXTER_APP`, on by default):

```bash
cmake -S . -B build
cmake --build build --target Texter
```

`Apps/Texter/CMakeLists.txt` builds it standalone as well. Its version comes
from the first line of the changelog.

## Layout

| File | What it is |
|---|---|
| `main.cpp` | Command line, single instance, the application and its window |
| `UltraCanvasTextEditor.{h,cpp}` | The editor: tabs, menus, toolbars, documents, autosave, loading and saving |
| `UltraCanvasTextEditorDialogs.{h,cpp}` | Find / Replace, Go to Line, About and the other dialogs |
| `UltraCanvasTextEditorHelpers.{h,cpp}` | Language detection by extension, file-type lists, small helpers |
| `UltraCanvasTextEditorSpellCheck.cpp`, `UltraCanvasMarkdownSpellRanges.{h,cpp}` | Spell checking, and which parts of a Markdown file are checked |
| `UltraCanvasSearchBar.{h,cpp}` | The search-as-you-type bar |
| `UltraCanvasEncoding.{h,cpp}` | Character-encoding detection and conversion over iconv |
| `UltraCanvasTextEditorInfoFileStatistics.cpp` | File Statistics |
| `UltraCanvasTextEditorConfig.h` | Editor settings |

## Built on

`UltraCanvasTextArea`, `UltraCanvasRichTextEdit`,
`UltraCanvasTabbedContainer`, `UltraCanvasPDFView` (with MuPDF),
`UltraCanvasFileLoader` (`LoadTextDocument` / `SaveTextDocument`, and the
file types of every Open and Save dialog), the spell checker, fmt and iconv.
