# UltraCanvas on WebAssembly

<!-- doc-check: std::vector<uint8_t> documentBytes; -->

**Status: experimental.** The framework builds with Emscripten and runs in
Chromium: windows render through the same Cairo/Pango pipeline as the desktop
backends, and mouse, wheel, touch and keyboard input drive the widgets. This
page is what an *application* author needs to know about the browser as a
platform. The build itself (the wasm sysroot, the Emscripten flags, the demo
app) is documented in
[`UltraCanvas/OS/WASM/README.md`](../../UltraCanvas/OS/WASM/README.md).

## What is the same

Everything in `UltraCanvas/include` and `UltraCanvas/core`. An application
uses `UltraCanvasApplication`, `CreateWindow`, the element catalogue and the
file APIs exactly as on the desktop; `Initialize()`, `CreateWindow()` and
`Run()` are the same calls. Text layout, HiDPI, dirty-rect composition,
popups and the caret are pixel-identical to Linux because the render context
is the same code drawing into an offscreen Cairo surface that is then blitted
onto an HTML `<canvas>`.

## What is different

| Concern | On the desktop | In the browser |
|---|---|---|
| `app.Run()` | Blocks until the last window closes | **Never returns.** The browser owns the event loop; each animation frame runs one framework iteration. The application object must have static or heap storage (see below). |
| Files | The user's disk | The Emscripten virtual filesystem (MEMFS), lost on reload. `WASMFileSystem` can mount an IndexedDB-backed subtree that persists. |
| Open / Save dialogs | Native pickers | Synchronous pickers are impossible; `UltraCanvasNativeDialogs::OpenFile` / `SaveFile` / `SelectFolder` return "cancelled". Use `SaveContent()` to save (it becomes a browser download) and `WASMBrowser::PickFilesAsync()` to import. |
| Message boxes | Native message dialogs | `window.alert` / `window.confirm`; two buttons at most, the title becomes the first line. |
| Text input dialog | Native | `window.prompt`. Password prompts report Cancel (prompt() echoes the text). |
| Clipboard | System clipboard, all formats | Text only, through the async Clipboard API plus a `paste`-event bridge. Copy (Ctrl+C) writes to the system clipboard; paste works with Ctrl+V / Cmd+V. Menu-driven Paste sees the last text the browser let the page read. |
| `OpenURL()` | Default browser | `window.open()` in a new tab (subject to popup blocking outside a user gesture). |
| Recent files, file associations, launching programs | Native | No-ops or "not available" errors. |
| Printing | Native print dialog | `ShowPrintDialog()` opens the text in a new window and calls `window.print()`. |
| Folder watch, file lock, volume monitor, hardware info | Native probes | The core fallbacks: polling, "unknown", "null" backend. |
| Spell checking | System service or Hunspell | Hunspell only, with dictionaries preloaded into the virtual FS. |
| Threads | `std::thread` | Web Workers from a preallocated pool (`-sPTHREAD_POOL_SIZE=8`); needs cross-origin isolation headers on the page. |
| Networking (UltraNet), OpenGL, audio, video, most file-type plugins | Available | Compiled out (`UltraCanvas/CMakeLists.txt` forces them off for `EMSCRIPTEN`). |

### Application lifetime

```cpp
int main(int, char* argv[]) {
    static UltraCanvasApplication app;   // static, NOT a stack local
    if (!app.Initialize(argv[0])) return 1;
    // ... create windows and elements as on the desktop ...
    app.Run();                           // never returns in the browser
    return 0;
}
```

`emscripten_set_main_loop` unwinds `main()`'s frame while the loop keeps
ticking; a stack-local application would be destroyed under it.

## Browser utilities (`UltraCanvasWASMSupport.h`)

Optional helpers for the browser features that have no desktop equivalent.
They are only built for WebAssembly, so guard their use. The header lives in
`UltraCanvas/OS/WASM/`, which is not on the library's public include path: add
the checkout's `UltraCanvas` directory to the application's include directories
(`target_include_directories(myapp PRIVATE <checkout>/UltraCanvas)`) and include
it by that path:

```cpp
#if defined(__EMSCRIPTEN__)
#include "OS/WASM/UltraCanvasWASMSupport.h"
#endif
```

Browser APIs are asynchronous. Every method that needs one returns at once
and calls back later on the main thread, between frames; the callback may
touch UI elements.

### Persistent storage

```cpp
// Mount /data on IndexedDB and load what it holds from the last session.
WASMFileSystem::MountFileSystem("/data", [settings](bool loaded) {
    if (loaded) settings->LoadFrom("/data/settings.json");
});

// Later, after writing files under /data:
WASMFileSystem::SyncToBrowser([statusLabel](bool ok) { statusLabel->SetText(ok ? "Saved" : "Save failed"); });
```

`FileExists`, `ReadFile`, `WriteFile`, `DeleteFile`, `CreateDirectory` and
`ListDirectory` are plain helpers over the virtual filesystem; `std::filesystem`
and `std::fstream` see the same files.

### Importing and exporting files

```cpp
// Let the user pick one or more images; they land in the virtual FS.
WASMBrowser::PickFilesAsync(".png,.jpg,image/*", /*multiple=*/true,
    [imageViewer](const std::vector<std::string>& paths) {
        for (const auto& path : paths) imageViewer->LoadImage(path);   // empty: cancelled
    });

// Save: the framework facade already does the right thing.
UltraCanvasNativeDialogs::SaveContent(documentBytes.data(), documentBytes.size(),
                                      FileDialogOptions().SetDefaultFileName("drawing.svg"));
// or directly:
WASMBrowser::DownloadFile("drawing.svg", documentBytes, "image/svg+xml");
```

### Fetching

```cpp
WASMNetwork::FetchTextAsync("data/catalogue.json", [this](bool ok, const std::string& text) {
    if (ok) catalogue = JSON::Parse(text);   // catalogue: a JSONValue member
});
```

Same-origin URLs always work; cross-origin needs CORS on the server.

### Images and fonts

```cpp
// Decode through the browser (any format it supports); pixels are straight RGBA.
WASMResourceLoader::LoadImage("photo.webp", [](bool ok, int w, int h, const std::vector<uint8_t>& rgba) {
    // ok: rgba holds w * h * 4 bytes, row by row
});

// Fetch a font file into the virtual FS and register it with Pango.
WASMResourceLoader::LoadFont("Inter", "fonts/Inter.ttf", [](bool ok, const std::string& path) {
    if (ok) UltraCanvasApplication::GetCurrent()->RegisterFontFile(path);
});
```

### Page and browser

`WASMBrowser` also wraps `alert` / `confirm` / `prompt`, the console,
`navigator.userAgent`, the screen size and `localStorage`. `WASMURL` reads the
page URL, its query parameters (`GetQueryParameter`, `GetAllQueryParameters`)
and hash, and can navigate or reload. `WASMTime` exposes `performance.now()`
and performance marks.

## Serving an application

Threads need `SharedArrayBuffer`, which browsers enable only on cross-origin
isolated pages. Serve with

```
Cross-Origin-Opener-Policy: same-origin
Cross-Origin-Embedder-Policy: require-corp
```

`UltraCanvas/OS/WASM/demo/serve.py` is a minimal server that sets both. The
bundled fonts must be preloaded into the virtual FS at `/share/media/fonts`
(`--preload-file media/fonts@/share/media/fonts`), and so must any other
file the application reads through `GetResourcesDir()` - the front-page demo
preloads its logo at `/share/media/images/...` the same way.

Two sample applications live in `UltraCanvas/OS/WASM/demo/`: `wasm-demo.cpp`,
a window with labels and a button, and `ultraos-frontpage.cpp`, the
https://ultraos.eu front page as a scrolling page of UltraCanvas elements
(a navigation with popup menus, hero, card rows, offers, an FAQ accordion,
forms with text inputs, a text area and a checkbox, a footer). The second
is the pattern for a web-page-like application: one scrollable container
filling the window, sections stacked by Block layout so wrapped text gets
its real height, Flex rows for the horizontal pieces, popup menus anchored
under their buttons, and `Maximize()` under `__EMSCRIPTEN__` to fill the
viewport. Both also build as desktop applications (`-DBUILD_WASM_DEMOS=ON`
at the repository root).

## The demo application in the browser

The framework's demo application (`Apps/DemoApp/`) builds as a page too:
`emcmake cmake` at the repository root produces `UltraCanvasDemo.html`, the
whole page tree of the desktop demo - the elements, the layout system, the
charts and diagrams, the vector and bitmap formats, the 3D model readers,
the text documents, the Filer and Album widgets, the module pages - less
what the browser has no backend for:

| Left out | Why |
|---|---|
| Audio Player, Level Meter, Audio FX, and the Waveform, Spectrogram and Audio Analysis charts | `ULTRACANVAS_ENABLE_AUDIO` is off in the browser: no playback, and the sample tracks the charts analyse are decoded by the audio module |
| Video Player | `ULTRACANVAS_ENABLE_VIDEO` is off: the null backend plays nothing (the Video Timeline, a pure element, stays) |
| STL 3D Models | its one sample is a 25 MB file; the model formats page shows the other readers from the smaller samples |
| OpenGL, CDR, XAR, EPS, PDF, OCR, Vectorizer, LaTeX, Ultra Net | compiled out by `UltraCanvas/CMakeLists.txt` for every WebAssembly build, so their pages are absent or say so, as on a desktop build without them |

**SVG** takes a different road in the browser: the wasm sysroot's libvips has
no SVG loader, so `UCImage` reads an .svg through the vector preview seam
the Vector plugin installs and renders it with the core's vector renderer
(`UltraCanvasVectorConverters.md`, *the vector preview provider*). The
demo registers the plugin in its `main.cpp`, so its SVG page and every SVG
icon - the toolbars', the flat buttons', the tree's - render as on the
desktop; an application that registers no plugins gets "format not
supported" for them.

The pages' files - the fonts, icons, images, vector samples, the smaller 3D
models, the docs and the demo's own sources for the source button - are
preloaded into the virtual filesystem at `/share/...` by the demo's CMake
block, about 50 MB downloaded once. The page's query string replaces the
command line: `UltraCanvasDemo.html?component=piecharts` opens that page
(and, like `--component`, leaves the About window out), and `?about=0` is
`--no-about`. The main window maximizes to the browser viewport. The source
is three `__EMSCRIPTEN__` branches: the static application object in
`main.cpp`, the query string there, and the page registrations left out in
`UltraCanvasDemo.cpp`. `wasm-build.yml` builds the page in CI, with the two
sample applications.

## Known limitations

- Keyboard: no IME composition events (dead keys and CJK input methods are not
  handled); typed text arrives on KeyDown like the desktop backends.
- Mouse: drags that leave the canvas stop receiving moves until the pointer
  re-enters (no pointer capture yet).
- Touch is synthesized into left-button mouse events.
- Three-button message dialogs (Yes/No/Cancel, Abort/Retry/Ignore) lose their
  third choice in `confirm()`.
- Clipboard paste relies on the browser firing a `paste` event for the
  Ctrl+V / Cmd+V chord; Shift+Insert and menu-driven Paste use whatever the
  browser last let the page read (Chromium after a permission prompt; Firefox
  and Safari usually refuse).
- The whole surface is uploaded to the canvas on every composition.
- Firefox and Safari are not yet validated; the backend has been exercised in
  Chromium.
