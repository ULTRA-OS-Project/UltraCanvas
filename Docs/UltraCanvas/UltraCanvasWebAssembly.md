# UltraCanvas on WebAssembly

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
They are only declared when building for WebAssembly, so guard their use:

```cpp
#if defined(__EMSCRIPTEN__)
#include "UltraCanvasWASMSupport.h"
#endif
```

Browser APIs are asynchronous. Every method that needs one returns at once
and calls back later on the main thread, between frames; the callback may
touch UI elements.

### Persistent storage

```cpp
// Mount /data on IndexedDB and load what it holds from the last session.
WASMFileSystem::MountFileSystem("/data", [](bool loaded) {
    if (loaded) settings->LoadFrom("/data/settings.json");
});

// Later, after writing files under /data:
WASMFileSystem::SyncToBrowser([](bool ok) { statusLabel->SetText(ok ? "Saved" : "Save failed"); });
```

`FileExists`, `ReadFile`, `WriteFile`, `DeleteFile`, `CreateDirectory` and
`ListDirectory` are plain helpers over the virtual filesystem; `std::filesystem`
and `std::fstream` see the same files.

### Importing and exporting files

```cpp
// Let the user pick one or more images; they land in the virtual FS.
WASMBrowser::PickFilesAsync(".png,.jpg,image/*", /*multiple=*/true,
    [](const std::vector<std::string>& paths) {
        for (const auto& path : paths) imageViewer->LoadImage(path);   // empty: cancelled
    });

// Save: the framework facade already does the right thing.
UltraCanvasNativeDialogs::SaveContent(documentBytes, FileDialogOptions().SetDefaultFileName("drawing.svg"));
// or directly:
WASMBrowser::DownloadFile("drawing.svg", documentBytes, "image/svg+xml");
```

### Fetching

```cpp
WASMNetwork::FetchTextAsync("data/catalogue.json", [](bool ok, const std::string& text) {
    if (ok) catalogue = UltraCanvasJSON::Parse(text);
});
```

Same-origin URLs always work; cross-origin needs CORS on the server.

### Images and fonts

```cpp
// Decode through the browser (any format it supports); pixels are straight RGBA.
WASMResourceLoader::LoadImage("photo.webp", [](bool ok, int w, int h, const std::vector<uint8_t>& rgba) { ... });

// Fetch a font file into the virtual FS and register it with Pango.
WASMResourceLoader::LoadFont("Inter", "fonts/Inter.ttf", [](bool ok, const std::string& path) {
    if (ok) UltraCanvasApplication::GetInstance()->RegisterFontFile(path);
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
(`--preload-file media/fonts@/share/media/fonts`).

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
