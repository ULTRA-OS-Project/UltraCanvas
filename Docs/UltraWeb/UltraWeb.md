# UltraWeb

**Status:** Phases 1 and 2 under way. UltraWeb 0.3.0 runs WebAssembly
apps on Linux, with timers, fetch, storage and the clipboard, and shows web
pages through the HTML reader, without JavaScript.
**Last Modified:** 2026-10-12
**Author:** UltraCanvas Framework / ULTRA OS

UltraWeb is the ULTRA OS browser. It shows **web pages** - HTML and CSS
laid out as native UltraCanvas elements by the framework's HTML reader, with
links, pictures, back and forward, and no JavaScript yet ([Web
pages](#web-pages)) - and it runs **WebAssembly apps
that build their UI out of UltraCanvas elements**: a URL or a file names a
`.wasm` module, UltraWeb runs it in a sandbox, and the module creates real
UltraCanvas elements — containers, labels, buttons, text inputs, checkboxes —
through the *element ABI*, holding only handles to them. Beside its
elements an app may start timers, fetch from the network under a browser's
rules, keep data across runs and copy text to the clipboard. React or
Angular apps, through a JavaScript engine compiled to WebAssembly, come in
later phases: the plan, the measurements behind it and the
architecture are in [`UltraWebProposal.md`](UltraWebProposal.md).

## Running it

```bash
UltraWeb                      # opens about:demo
UltraWeb https://example.org/ # a web page
UltraWeb ~/docs/index.html    # a local page (or a folder, listed)
UltraWeb ~/apps/counter.wasm  # a local app (binary .wasm or WebAssembly text)
UltraWeb https://example.org/app.wasm
UltraWeb --check about:demo   # start an app without a window, print what it built
UltraWeb --page example.org   # load a page without a window, print its text
```

The address bar takes the same: a web address (`example.org` is read as
`https://`), a path, a `file://` URL, `about:demo`, or the address of an
app. An address ending in `.wasm` or `.wat`, and `about:` ones, are apps;
everything else opens as a page, and a page address that turns out to serve
`application/wasm` runs as an app. Pages and apps fetched over the network
come through UltraNet with TLS verification on. The status bar shows the
page's address (or the link under the pointer), or what app is running, how
many elements it holds and the engine.

`--check` loads local addresses only (about:, paths, `file://`), starts the
app with an invisible root, and exits 0 when it started — useful for an app's
own build checks. It runs `uc_main` as a first visit would, offline: storage
starts empty and is not kept, timers are accepted and never fire, and fetch
and the clipboard answer `UC_ERR_DENIED`.

`--page` loads a page the way the window does, without a window, and prints
its title, final address, status, charset, warnings and text. It exits 1 for
a failed load or an HTTP error status.

## Web pages

A page is fetched by `HTML::PageLoader` and shown by `UltraCanvasWebView`,
both framework pieces (`Docs/UltraCanvas/UltraCanvasHTMLReader.md`):

- **Loading.** The page's charset is read from its BOM, its `Content-Type`,
  or a `<meta charset>`, else it is taken to be UTF-8 when it is and
  Windows-1252 when it is not. Every charset UltraNet's iconv knows is read.
  `<base href>` is honoured. Linked style sheets and the sheets they
  `@import` (up to 32) are fetched before the page is shown, with their
  `url()`s made absolute. Pictures
  load after it, and the page is built again as they arrive.
- **What a page may load.** An http(s) page loads http(s) and `data:`
  resources, an https page no http ones (mixed content), and only a local
  page loads local files. Pages, style sheets and pictures are limited to
  16 MB each, and to 512 resources per page. No cookies are sent or kept.
- **Links.** A `#fragment` scrolls without a reload. http(s) links open in
  the view. A local page may open local files. `mailto:`, `tel:` and other
  schemes go to the system's handler. `javascript:` does nothing.
- **History.** Back and forward keep each page's scroll position.
- **Other content.** Plain text shows as text, a picture on a page of its
  own, a folder as a list of its files. A page that cannot be loaded shows
  an error page saying why.
- **Downloads.** A link to anything else - a PDF, a zip, an answer the
  server marks `Content-Disposition: attachment` - opens the save dialog
  in the Downloads folder, named as the server or the address says (the
  name cleaned of folders and hidden dots). The page stays on show, with
  its address and history. A file up to 16 MB is saved from what was
  fetched; a larger one is fetched again and streamed to disk, for as long
  as it takes - there is no time limit, only a server that stops sending for
  30 seconds ends it. A file is written as `<name>.part` and renamed when
  complete, so a broken download leaves no file that looks whole; downloads
  still running when the window closes are cancelled. Nothing is opened
  after it is saved.
- **The downloads list.** The toolbar's **Downloads** button (it reads
  "Downloads (2)" while two run) shows or hides a panel above the status
  bar, which opens by itself when a streamed download starts. Each
  download of this run has a row: its name, then how far it got
  ("5.3 MB of 20.0 MB (26%)", rewritten as it goes) or how it ended
  ("Saved - 20.0 MB", "Failed: the server answered 404", "Cancelled").
  A running one has **Cancel**; a saved one **Open** and **Show folder**,
  which go to the system's default application; a finished one
  **Remove**, which drops the row and keeps the file. **Clear finished**
  drops every finished row. The list is not kept across runs.
- **Forms.** Text and password fields, text areas, checkboxes, radio
  groups, dropdowns and buttons are real widgets. A submit button or Enter
  sends the form: GET opens the action with the fields as its query, POST
  sends them (urlencoded, multipart or plain text) and shows the answer.
  What was typed survives a rebuild of the page. A file input opens the
  file dialog and its files are uploaded as multipart parts; a click on a
  `<label>`'s text acts on its control.
- **No JavaScript.** `<script>` is not run, `<noscript>` is shown.
- **Layout** is the HTML reader's: blocks, inline text, tables, floats,
  flex and grid, backgrounds, `@media` at the window's width (the page is
  built again when the width changes). See the reader's doc for its limits.

### The sandbox

| What an app can do | How |
|---|---|
| Build and change its UI | the element ABI below; every handle, pointer and length is checked |
| Hear what the user does | `uc_listen` + its `uc_event` export (click, text change, Enter, toggle) |
| Run code later, or every so often | `uc_timer_start` / `uc_timer_stop`: up to 256 timers, 4 ms at the shortest |
| Talk to its server, and to others that allow it | `uc_fetch`: http(s) through UltraNet, TLS verified, no cookies — same origin freely, other origins under CORS (below) |
| Remember things across runs | `uc_storage_*`: key/value bytes, 5 MB per origin, kept on disk |
| Copy text for the user | `uc_clipboard_write`, only while it handles a click, a toggle or an edit |
| Log | `uc_log` (stderr in this version, where UltraWeb also says why it refused a fetch or a clipboard write) |
| Clocks, random numbers, stdout/stderr | WASI preview 1 |

What it cannot do: open files (WASI has no preopened directories), read the
environment, the arguments or the clipboard, send cookies or reach a URL the
fetch rules below forbid, or touch an element it did not create. Limits per
app: **2 s per call** into it (a call that runs longer is interrupted and the
app stopped), **256 MB** of memory (a `memory.grow` past it fails),
**20,000 elements**, **1 MB** per text property, **16 fetches** open at
once (a finished one counts until it is closed). An app that traps, or is
stopped, is torn down and its error shown in its place; it cannot crash or
freeze the browser.

#### The fetch rules

They are a browser's `fetch` with `credentials: "omit"`, decided in
`Apps/UltraWeb/host/UltraWebFetch.cpp`:

- **Schemes:** http and https only. An **https app cannot fetch http**, and
  a redirect to http (or to any other scheme) is refused as well.
- **The app's origin** is the scheme, host and port it was loaded from. A
  relative URL is resolved against the app's address (RFC 3986), so
  `uc_fetch("data.json")` from `https://x.org/app/main.wasm` asks
  `https://x.org/app/data.json`. A request to the app's own origin may use
  any method (`GET`, `POST`, `PUT`, `DELETE`, `PATCH`, `HEAD`) and content
  type, and its answer — every header but `Set-Cookie` — reaches the app.
- **Another origin** gets an `Origin` header and only *simple* requests:
  `GET`, `HEAD`, or `POST` as `text/plain`, `application/x-www-form-urlencoded`
  or `multipart/form-data`. Anything else would need a CORS preflight, which
  UltraWeb does not send yet, so it is refused before it leaves. The answer
  reaches the app only when the server sent `Access-Control-Allow-Origin: *`
  or the app's own origin, and then only the CORS-safelisted headers
  (`Content-Type`, `Content-Length`, `Cache-Control`, `Content-Language`,
  `Expires`, `Last-Modified`, `Pragma`) and those it lists in
  `Access-Control-Expose-Headers`. A redirect to another origin is judged
  the same way, by where it ended.
- **An app from a file or about:** has no origin: it can fetch absolute
  http(s) URLs only, every one counts as another origin, and only
  `Access-Control-Allow-Origin: *` lets the answer in (it sends
  `Origin: null`, which a server's `null` does not match).
- A URL with a user name or password in it is refused; up to 5 redirects
  are followed; a response may be 16 MB, a request body 4 MB, a request
  30 s. A transfer cut off midway (a timeout, a lost connection) is a
  failure, `UC_ERR_NETWORK`, never a short body.

#### Storage

One store per origin — for an app from a file, per file, whatever path
named it; for `about:demo`, its own — under
`<settings folder>/UltraWeb/storage/` (`~/.config/UltraCanvas/UltraWeb/storage`
on Linux), one JSON file each with keys and values in base64, since they
are bytes. A store is written a turn after a change and when the app
closes, through a temporary file and a rename. Keys up to 1 KB; 5 MB per
store, keys and values counted plus 32 bytes per key; a `uc_storage_set`
that would go past it fails and leaves the old value.

## Writing an app

The contract is one C header, [`UltraWeb/guest/ultraweb.h`](../../UltraWeb/guest/ultraweb.h):
element kinds, property and event ids, error codes, and the imports (module
`ultracanvas`). The host includes the same header, so the numbers cannot
disagree. The model:

- The host owns the elements; the app holds `uint32_t` handles.
  `UC_ROOT_HANDLE` (1) is the app's area of the window, a flex column.
- The app exports `uc_main()`, which builds the UI and returns, and — if it
  listens to anything — `uc_event(handle, event, detail)`. It is a WASI
  *reactor*: no `main`, its state lives between calls.
- Strings cross as (pointer, length) in the app's own memory, UTF-8.
- Every import returns `UC_OK` or a negative `UC_ERR_*`; nothing an app
  passes can crash the host.
- Changes the app makes do not raise events back into it.
- Events of the app itself — a timer firing, a fetch finishing — come
  through the same `uc_event`, with handle `UC_NO_HANDLE` (0), event
  `UC_EVENT_TIMER` or `UC_EVENT_FETCH`, and the timer's or the fetch's id
  as the detail. They never arrive while the app is inside a call: one that
  is ready meanwhile waits until the call has returned.

A counter in C:

```c
#include "ultraweb.h"
#include <stdio.h>
#include <string.h>

static uint32_t button, label;
static int clicks;

static uint32_t Add(uint32_t parent, const char* kind) {
    uint32_t h = uc_create(kind, strlen(kind));
    uc_insert(parent, h, UC_NO_HANDLE);
    return h;
}
static void SetText(uint32_t h, const char* text) { uc_set_text(h, UC_PROP_TEXT, text, strlen(text)); }

UC_EXPORT(uc_main) void uc_main(void) {
    uc_set_number(UC_ROOT_HANDLE, UC_PROP_PADDING, 16);
    uint32_t row = Add(UC_ROOT_HANDLE, UC_KIND_CONTAINER);
    uc_set_number(row, UC_PROP_DIRECTION, UC_DIRECTION_ROW);
    uc_set_number(row, UC_PROP_GAP, 12);
    button = Add(row, UC_KIND_BUTTON);
    SetText(button, "Count");
    uc_listen(button, UC_EVENT_CLICK);
    label = Add(row, UC_KIND_LABEL);
    SetText(label, "Clicked 0 times");
}

UC_EXPORT(uc_event) void uc_event(uint32_t handle, uint32_t event, int32_t detail) {
    if (handle == button && event == UC_EVENT_CLICK) {
        char text[64];
        snprintf(text, sizeof text, "Clicked %d times", ++clicks);
        SetText(label, text);
    }
}
```

Timers, fetch and storage, in a few more lines — a clock that keeps its
start time across runs and reads a greeting from its own server:

```c
static int32_t tick, hello;
static uint32_t clock_label, greeting;

UC_EXPORT(uc_abi_version) int32_t uc_abi_version(void) { return 2; }   /* the services are v2 */

UC_EXPORT(uc_main) void uc_main(void) {
    clock_label = Add(UC_ROOT_HANDLE, UC_KIND_LABEL);
    greeting = Add(UC_ROOT_HANDLE, UC_KIND_LABEL);
    char first[32];
    int32_t n = uc_storage_get("first-run", 9, first, sizeof first);   /* UC_ERR_NOT_FOUND the first time */
    if (n < 0) uc_storage_set("first-run", 9, "today", 5);
    tick = uc_timer_start(1000, 1);                                    /* every second */
    hello = uc_fetch("hello.txt", 9, UC_METHOD_GET, 0, 0, 0, 0);       /* relative: the app's own origin */
}

UC_EXPORT(uc_event) void uc_event(uint32_t handle, uint32_t event, int32_t detail) {
    static int seconds;
    if (handle == UC_NO_HANDLE && event == UC_EVENT_TIMER && detail == tick) {
        char text[32];
        snprintf(text, sizeof text, "%d s", ++seconds);
        SetText(clock_label, text);
    } else if (handle == UC_NO_HANDLE && event == UC_EVENT_FETCH && detail == hello) {
        char body[256];
        if (uc_fetch_status(hello) == 200) {
            int32_t length = uc_fetch_body(hello, body, sizeof body - 1);
            body[length < (int32_t)sizeof body - 1 ? length : (int32_t)sizeof body - 1] = 0;
            SetText(greeting, body);
        }
        uc_fetch_close(hello);
    }
}
```

Build it with [wasi-sdk](https://github.com/WebAssembly/wasi-sdk) (version 34
was used to verify everything here):

```bash
<wasi-sdk>/bin/clang --target=wasm32-wasip1 -O2 -mexec-model=reactor \
    -I UltraWeb/guest counter.c -o counter.wasm
UltraWeb --check counter.wasm && UltraWeb counter.wasm
```

C++ with exceptions needs the standard exception encoding, which is what
wasmtime runs:

```bash
SYSROOT=<wasi-sdk>/share/wasi-sysroot
<wasi-sdk>/bin/clang++ --target=wasm32-wasip1 --sysroot=$SYSROOT -O2 -mexec-model=reactor \
    -fwasm-exceptions -mllvm -wasm-use-legacy-eh=false -L$SYSROOT/lib/wasm32-wasip1/eh -lunwind \
    -I UltraWeb/guest app.cpp -o app.wasm
```

An app may also be WebAssembly text: [`about:demo`](../../Apps/UltraWeb/host/generate_demo_app.py)
is one, generated so its string offsets cannot drift. An app built for a
newer ABI can export `uc_abi_version()`, returning the `UC_ABI_VERSION` it
needs; an older UltraWeb then refuses it with a message instead of failing
in the middle.

### Element ABI v2 at a glance

| Kind (`uc_create`) | Text properties | Number properties | Events |
|---|---|---|---|
| `Container` (flex column; row with `UC_PROP_DIRECTION`) | — | `DIRECTION`, `GAP`, `PADDING`, `ALIGN`, `BACKGROUND` | — |
| `Label` (wraps at its width) | `TEXT` | `FONT_SIZE`, `TEXT_COLOR`, `BACKGROUND` | — |
| `Button` | `TEXT` | `FONT_SIZE` | `CLICK` |
| `TextInput` (240 × 28 unless sized; `WIDTH` 0 makes it automatic) | `TEXT`, `PLACEHOLDER` | `FONT_SIZE` | `CHANGE`, `SUBMIT` |
| `Checkbox` | `TEXT` | `CHECKED` | `TOGGLE` (detail 1 / 0) |
| every kind | | `ENABLED`, `VISIBLE`, `WIDTH`, `HEIGHT` (px, 0 = automatic), `GROW` | |

The root takes the container properties too. Nothing is stretched unless
the app asks: a container puts each child at its own size, at the start of
its cross axis (the left of a column, the top of a row). `UC_PROP_ALIGN`
says otherwise — `UC_ALIGN_CENTER`, `UC_ALIGN_END`, or `UC_ALIGN_STRETCH`,
which fills the column's width (the row's height) for every child whose size
on that axis is automatic; a child with a set width keeps it, so a full-width
text input is one with `WIDTH` 0 in a stretching column. Text still wraps at
the width it has. What an app sets on the root goes when the app goes, so
the next one starts from the window's defaults. `uc_get_number` of
`WIDTH` / `HEIGHT` answers the laid-out size, and `uc_bounds` the box in the
app area — both as of the last layout pass, so not yet right after a change
(a synchronous layout flush is Phase 4 of the plan). Colours are `0xRRGGBBAA`.

| Service (v2) | Imports | Arrives as |
|---|---|---|
| Timers | `uc_timer_start(delayMs, repeat)` → id, `uc_timer_stop(id)` | `uc_event(0, UC_EVENT_TIMER, id)` |
| Fetch | `uc_fetch(url, method, body, contentType)` → id; `uc_fetch_status`, `uc_fetch_body`, `uc_fetch_header`, `uc_fetch_close` | `uc_event(0, UC_EVENT_FETCH, id)` once, finished or failed |
| Storage | `uc_storage_get`, `uc_storage_set`, `uc_storage_remove`, `uc_storage_key(index)`, `uc_storage_clear` | — |
| Clipboard | `uc_clipboard_write(text)` | — |

The results a service adds to the element ABI's: `UC_ERR_NOT_FOUND` (no such
key, header, timer or fetch), `UC_ERR_DENIED` (the sandbox does not allow
it, or this UltraWeb lacks the service), `UC_ERR_NETWORK` (no complete
answer). `uc_fetch_status` is the HTTP status (an error status such as 404
is an answer, not a failure), 0 while running, or the failure. Everything
an import copies out works like `uc_get_text`: at most the capacity, and the
full length returned.

## How it is built

```
Apps/UltraWeb/
├── main.cpp                  window, --check, --page, --version
├── ui/UltraWebWindow.*       address bar, back / forward, app area (UC_ROOT_HANDLE) or web view, status bar
├── ui/UltraWebDownloadsPanel.*  the downloads panel: a row per download, Cancel / Open / Show folder / Remove
└── host/
    ├── UltraWebGuest.*       the bridge: handle table, the imports, events → uc_event, timers, fetches
    ├── UltraWebFetch.*       the fetch rules (origin, mixed content, CORS) and the UltraNet service
    ├── UltraWebStorage.*     the per-origin store and its file
    ├── UltraWebLoader.*      address → module bytes (about:, file, file://, http(s) via UltraNet)
    ├── UltraWebPages.*       the page services (UltraNet, files, URLs, charsets); app or page?
    ├── UltraWebDownloads.*   saving a download: bytes through a part file, or streamed via UltraNet
    ├── UltraWebDownloadList.*  this run's downloads and their states (no UI)
    ├── UltraWebDemoApp.h     about:demo (generated by generate_demo_app.py)
    └── generate_demo_app.py
UltraWeb/guest/ultraweb.h     the ABI, shared by host and guests
UltraCanvas/{include,core}/WasmHost/   the WebAssembly runner (Masterfile_modules.md §17)
```

- **WasmHost** wraps wasmtime 49.0.2 (its prebuilt C API, downloaded and
  hash-checked by `cmake/UltraCanvasWasmtime.cmake`, or
  `-DULTRACANVAS_WASMTIME_DIR=<unpacked c-api>` offline). On by default on
  Linux; on macOS and Windows it is opt-in
  (`-DULTRACANVAS_ENABLE_WASM_HOST=ON`) and not yet built in CI, and without an
  engine UltraWeb starts and says it cannot run apps. The binary is about
  38 MB, most of it the statically linked engine.
- A guest runs on the UI thread. A failure after start is reported on a
  later turn, never from inside the element callback that ran the guest, and
  an element a guest releases while its own callback runs (a button removing
  itself) is kept alive until that call has unwound.
- The services are handed to the bridge (`GuestServices`): the window gives
  it the application's timers, UltraNet, a store on disk and the system
  clipboard; the tests give it fakes. Timer and network callbacks hold the
  guest weakly, and its destructor stops its timers, cancels its fetches and
  writes its store, so nothing reaches an app that is gone.
- Tests: `Tests/WasmHostTest.cpp` (the runner) and
  `Tests/UltraWebGuestTest.cpp` (the bridge, the fetch rules, the store and
  the loader, against inline guests and `about:demo`, with fake timers,
  network and clipboard); neither needs a display, a network or a wasm
  toolchain. Pages: `Tests/HTMLPageLoaderTest.cpp` and
  `Tests/WebViewTest.cpp`, against a fake web.

## Not yet

In the order of the plan (Phase 1 of `UltraWebProposal.md` §10, then the
later phases): manifests, `<link rel="ultraweb-app">` and `.ucpkg` packages;
a compiled-module cache; per-app permissions — with them reading the
clipboard, fetching beyond the app's origin without CORS, a larger store,
and keeping a private-network address from a public app; CORS preflight
requests; C++ element proxies generated from the framework headers; the
macOS, Windows and packaging builds. For pages (Phase 2): tabs,
bookmarks, and a history and downloads list kept across runs;
then modern layout beyond flex and grid, the live DOM, and JavaScript.
