#### 2026-10-12 *0.6.0*
- **The downloads list.** A **Downloads** button on the toolbar shows or
  hides a panel above the status bar. It reads "Downloads (2)" while two
  run, and the panel opens by itself when a large download starts.
  - Each download of this run has a row: its name, then its progress
    ("5.3 MB of 20.0 MB (26%)", rewritten as it goes) or how it ended
    ("Saved", "Failed: why", "Cancelled").
  - A running download can be cancelled. A saved one can be opened, or its
    folder shown, with the system's default application. A finished one
    can be removed from the list (the file stays), and **Clear finished**
    removes them all.
  - New `host/UltraWebDownloadList` (the list, no UI) and
    `ui/UltraWebDownloadsPanel`. `UltraWebDownloadsTest` covers both,
    clicking the panel's buttons.

#### 2026-10-12 *0.5.0*
- **Downloads.** A link to anything that is no page - a PDF, a zip, an
  answer the server marks `Content-Disposition: attachment` - opens the
  save dialog in the Downloads folder, named as the server or the address
  says. Before, the status bar only said it was not a page.
  - Up to 16 MB, the file is saved from what was fetched. A larger one is
    fetched again and streamed to disk, with its progress in the status
    bar.
  - It is written as `<name>.part` and renamed when complete. A failed or
    cancelled download leaves no file, and closing the window cancels
    the ones still running.
  - The page stays on show, with its address and history.
  - New `host/UltraWebDownloads` and its `UltraWebDownloadsTest`, which
    downloads from a loopback server.

#### 2026-10-12 *0.4.1*
- **File uploads.** A form's file input is a "Choose file…" button that
  opens the file dialog (filtered by the input's `accept` list), and the
  chosen files are sent as multipart file parts with their names, types
  and bytes.
- **Labels.** A click on a `<label>`'s text acts on its control: a checkbox
  toggles, a radio is chosen, a file input opens the dialog, a field takes
  the focus.

#### 2026-10-12 *0.4.0*
- **Forms.** A page's form can be filled in and sent. Its controls are the
  framework's widgets: text and password fields, text areas, checkboxes,
  radio groups, dropdowns and buttons, on the line with their labels.
  - A submit button, or Enter in a field, sends the form. A GET form opens
    its action with the fields as the query. A POST form sends them through
    UltraNet as `application/x-www-form-urlencoded`, `multipart/form-data`
    or `text/plain`, and shows the answer. Reset puts the page's values back.
  - What was typed survives the page being built again (a new window width,
    pictures arriving), focus included.
  - Back from a POST answer reloads its address by GET.
  - Not yet: file uploads, and clicking a `<label>`'s text to toggle its box.
- **`@import`** in style sheets, and the margins, padding, backgrounds and
  borders of inline elements (`<mark>`, tags, links spaced apart, mail
  buttons), come with the framework's HTML reader.

#### 2026-10-12 *0.3.0*
- **Web pages.** An address that is not an app opens as a page, laid out by
  the framework's HTML reader as native elements (`UltraCanvasWebView`), with
  no JavaScript: http(s) through UltraNet with TLS verification on and no
  cookies, local files and folders, plain text and pictures. A `.wasm` or
  `.wat` address, `about:`, and a page address that serves
  `application/wasm` still run as apps.
  - The page's charset comes from its BOM, Content-Type or `<meta>`, else
    UTF-8 or Windows-1252. `<base href>` is honoured. Linked style sheets
    load before the page shows and pictures after it.
  - An https page loads no http resources, and a web page no local files.
  - Links: a `#fragment` scrolls. `mailto:` and other schemes go to the
    system.
  - Back and forward keep each page's scroll position.
  - A page that cannot be loaded shows an error page saying why.
  - The status bar shows the link under the pointer.
- **Back and forward buttons** in the toolbar, for pages.
- **`--page <address>`** loads a page without a window and prints its title,
  address, status, charset, warnings and text.
- The framework pieces are `HTML::PageLoader`, `UltraCanvasWebView` and
  `UltraNet_ConvertToUtf8` (see the framework changelog).

#### 2026-10-06 *0.2.0*
- **Apps get timers, fetch, storage and the clipboard (element ABI v2).**
  Beside its elements an app may now start timers (`uc_timer_start`), fetch
  over http(s) (`uc_fetch` and its status, body, headers and close), keep
  key/value data across runs (`uc_storage_*`, 5 MB per origin, on disk under
  the settings folder) and copy text to the clipboard (`uc_clipboard_write`,
  only while it handles a click, a toggle or an edit). Timers and finished
  fetches arrive through `uc_event` with handle 0. Fetch follows a browser's
  rules without cookies: no http from an https app, any request to the app's
  own origin, to another origin only simple ones, and their answers only
  when the server allows the app's origin (CORS), with only the headers it
  exposes. A refused fetch or clipboard write says why on the console. A v1
  app runs unchanged; a v2 app exports `uc_abi_version()` returning 2, so
  UltraWeb 0.1.0 refuses it with a message. `about:demo` counts into storage,
  copies what was typed and shows a timer.
- **Nothing is stretched unless the app asks.** A container stretched every
  child across it, so a button given a width in a column, or a text input's
  240 px, went full width. Children now sit at their own size, at the start;
  the new `UC_PROP_ALIGN` (start, center, end, stretch) is how an app asks
  for something else, and even stretch leaves a child with a set width at
  that width. This is the framework's layout engine's rule now, for every
  app (see the framework changelog).
- **An app no longer inherits the last one's root.** Padding, gap,
  direction, alignment and background an app set on its root stayed for the
  next app opened in the window; the root goes back to the window's setup
  when an app goes.
- **An app cut off while downloading is an error.** A transfer that broke
  after the server's first line (a timeout, a lost connection) used to hand
  over the part that had arrived as if it were the app; UltraWeb now says
  it could not load it.
- `--check` runs `uc_main` as a first visit would: storage empty and not
  kept, timers that never fire, no fetch or clipboard.

#### 2026-10-06 *0.1.0*
- **UltraWeb runs WebAssembly apps.** The first version of the ULTRA OS
  browser loads a `.wasm` (or WebAssembly text) app from `about:demo`, a file
  path, a `file://` URL or an `https://` URL, and runs it in a sandbox: the
  app builds its UI out of real UltraCanvas elements - containers, labels,
  buttons, text inputs, checkboxes - through the element ABI in
  `UltraWeb/guest/ultraweb.h`, and holds only handles to them. An app that
  traps or runs one call for longer than two seconds is stopped and its error
  shown, and its memory cannot grow past 256 MB, so it can neither crash nor
  freeze the browser. `about:demo` is
  built in, and `UltraWeb --check <address>` starts an app without a window.
  HTML pages and JavaScript come in later phases
  (`Docs/UltraWeb/UltraWebProposal.md`). Runs on Linux in this version; the
  framework's WasmHost module, on wasmtime 49, is what it is built on (see the
  framework changelog).
