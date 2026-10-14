# UltraCanvas WebAssembly (Emscripten) Backend

**Status: experimental — validated in a browser.** The full stack (this
backend + wasm-compiled cairo/pango/fontconfig/glib/libvips) links with
Emscripten 6.0 and runs in Chromium: windows render through the shared
Cairo/Pango pipeline with the bundled Ubuntu fonts, and mouse input drives
widget callbacks (see [demo/](demo/)). The framework's demo application
builds as a page too (`UltraCanvasDemo.html`, from the repository root), so
the element catalogue, the charts and diagrams and the format readers have
been seen rendering in the browser. Not yet exercised: touch devices,
Firefox/Safari.

## Architecture

The backend reuses the **same Cairo + Pango render context as every desktop
platform** (`libspecific/Cairo/RenderContextCairo.cpp`) instead of
reimplementing `IRenderContext` on the HTML Canvas 2D API. That keeps text
layout (`ITextLayout` is Pango-shaped), dirty-rect composition, popups, the
caret and HiDPI behaviour pixel-identical to the desktop builds.

```
OS/WASM/
├── UltraCanvasWASMApplication.h/cpp   # Event loop bridge, keyboard, paste bridge, cursors, fonts
├── UltraCanvasWASMWindow.h/cpp        # <canvas>-backed window, input, presentation
├── UltraCanvasWASMClipboard.h/cpp     # Text clipboard over the async Clipboard API
├── UltraCanvasWASMNativeDialogs.cpp   # alert/confirm/prompt, downloads, print
├── UltraCanvasWASMFileLoader.cpp      # NotifyRecentFile (no-op)
├── UltraCanvasSpellCheckSupport.cpp   # No system speller: Hunspell backend
├── UltraCanvasWASMSupport.h/cpp       # Optional browser utilities (IDBFS, fetch, file
│                                      # import/export, image/font loading, URL, ...)
├── build-wasm-sysroot.sh              # Cross-compiles the dependency stack
├── patches/                           # wasm fixes applied to the deps by the script
└── demo/                              # Browser validation apps: wasm-demo, ultraos-frontpage
```

Every `OS/<Platform>/` directory must define the per-platform symbols the core
references unconditionally - the `UltraCanvasNativeDialogs` statics,
`UltraCanvasFileLoader::NotifyRecentFile`, `CreateNativeSpellCheckBackend()`;
the three files above supply them here, so applications that use dialogs or
the file loader link. What each one does in the browser, and the
application-facing view of the platform, is in
[`Docs/UltraCanvas/UltraCanvasWebAssembly.md`](../../../Docs/UltraCanvas/UltraCanvasWebAssembly.md).

| Concern | How it works |
|---|---|
| Main loop | `app->Run()` works unchanged: `RunBeforeMainLoop()` calls `emscripten_set_main_loop(..., simulate_infinite_loop=1)`, which never returns; each animation frame runs one `RunOnce()` iteration. When `running` clears or the last window closes, the tick performs the same shutdown tail `Run()` would. Hidden tabs fall back to a 250 ms `setTimeout` cadence so timers keep firing. |
| Window | One absolutely-positioned `<canvas>` per window (backing store in physical px, CSS box in logical px). `nativeSurface` is an offscreen cairo image surface, like the Windows backend. |
| Presentation | `InvalidateWindowNative()` — the framework's post-composition hook — converts the surface pixels (BGRX) to RGBA and `putImageData()`s them onto the canvas (copying out of the shared wasm heap, which `ImageData` cannot view directly under `-pthread`). |
| Input | Per-canvas Emscripten HTML5 callbacks convert DOM events to `UCEvent`s and `PushEvent()` them (mouse, wheel with notch normalisation, touch with synthesized left-button mouse events, focus). Keyboard is registered once on the browser window and routed to the focused window by `DispatchEvent()`. |
| HiDPI | `deviceScale = devicePixelRatio`; DOM coordinates are CSS px = logical units, so events need no physical→logical conversion. |
| Fonts | Pango + Fontconfig against the bundled Ubuntu/Ubuntu Mono TTFs, preloaded into the virtual FS at `/share/media/fonts` (see below). |
| Clipboard | Text only. `SetClipboardText()` caches the text and calls `navigator.clipboard.writeText()`. Reading is asynchronous in the browser, so the application lets the Ctrl/Cmd+V keydown through to the browser, which answers with a `paste` event; the DOM listener hands the pasted text to the clipboard backend and *then* queues the Ctrl+V key event, so the text field's paste handler finds it in place. `GetClipboardText()` also starts a `readText()` refresh for menu-driven pastes. |
| Dialogs | `alert()` / `confirm()` / `prompt()` for messages and text input; `SaveContent()` triggers a download; the synchronous file pickers report Cancel (imports go through `WASMBrowser::PickFilesAsync()`). |
| Screen | The browser viewport (`window.innerWidth/Height`) in CSS px. `Maximize()` fills it; `SetFullscreen(true)` uses the Fullscreen API. |

## Building

### 1. The wasm sysroot (once)

The REQUIRED pkg-config dependencies (cairo + pixman, pango + pangocairo,
fontconfig, freetype, harfbuzz, glib, tinyxml2, libvips + vips-cpp) do not
ship as Emscripten ports. `build-wasm-sysroot.sh` cross-compiles all of them
(plus zlib, libpng, libjpeg-turbo, libtiff, libwebp, expat, libffi, pcre2)
into a static sysroot with
`.pc` files, following [kleisauke/wasm-vips](https://github.com/kleisauke/wasm-vips)
where that project builds the same dependency — including its pre-patched
glib and libvips branches. pango gets a small local patch
(`patches/pango-1.52-wasm-function-pointers.patch`) fixing one-argument
callbacks cast to `GInterfaceInitFunc`/`GFunc`: native ABIs tolerate the
arity mismatch, wasm's strict indirect-call signature checking traps on it.
A second pango patch (`patches/pango-1.52-wasm-no-threads.patch`) runs
fontconfig's work (FcInit, matching, sorting) on the caller's thread when
the build has no `-pthread`: pango otherwise hands it to a worker it starts
with `g_thread_new`, and GLib aborts on the `pthread_create` a program
without threads cannot make, at the first text drawn. The threaded build
keeps its worker.
glib gets one of the same kind (`patches/glib-2.89-wasm-free-full-casts.patch`):
`g_slist_free_full`, `g_list_free_full`, `g_queue_free_full` and
`g_queue_clear_full` called the `GDestroyNotify` through the two-argument
`GFunc` of their foreach helpers, which the branch's own fixes had not
covered, and the first pango markup a page laid out (the demo's tree view)
trapped in `g_markup_parse_context_free`.

```bash
source /path/to/emsdk/emsdk_env.sh   # Emscripten 6.0.11 - the version CI builds with
./build-wasm-sysroot.sh /path/to/wasm-sysroot
```

The whole sysroot is compiled `-pthread -fwasm-exceptions`; the library
build (CMakeLists.txt WASM branch) propagates the same flags PUBLIC, since
the exception/threading ABI must match across every static library in the
final link. `WASM_THREADS=0 ./build-wasm-sysroot.sh <other-dir>` builds a
sysroot without `-pthread` for a single-threaded application
(`-DULTRACANVAS_WASM_THREADS=OFF` on the library): no `SharedArrayBuffer`,
so no isolation headers on the serving host, and no `std::thread` at run
time. It is also a build without libvips, which the library's CMake forces
(`ULTRACANVAS_ENABLE_VIPS=OFF`): libvips runs every image on a worker
thread, a program without `-pthread` cannot start one, and GLib aborts on
the refused `pthread_create` at the first text. Such a program reads PNG
through cairo's own reader and SVG through the Vector plugin, nothing
else. The two kinds of sysroot never share a directory.

### 2. The library / an application

```bash
source /path/to/emsdk/emsdk_env.sh
# CMake runs the host's pkg-config, and emcmake does not pass it
# EM_PKG_CONFIG_PATH: put the sysroot on PKG_CONFIG_PATH, and on
# PKG_CONFIG_LIBDIR so no host .pc file is found instead.
export PKG_CONFIG_PATH=/path/to/wasm-sysroot/lib/pkgconfig:/path/to/wasm-sysroot/share/pkgconfig
export PKG_CONFIG_LIBDIR=$PKG_CONFIG_PATH
emcmake cmake -S . -B build-wasm -DCMAKE_BUILD_TYPE=Release
cmake --build build-wasm
```

Configured at the repository root like that, the build is the core library
and the framework's demo application as a page, `build-wasm/UltraCanvasDemo.html`
(with `.js`, `.wasm`, `.ww.js` and the `.data` bundle beside it): the root
CMakeLists.txt's *WebAssembly defaults* block leaves every other
application and the sibling modules off, as the Android block does, and the
demo's `EMSCRIPTEN` block gives the page its suffix, its shell
(`Apps/DemoApp/wasm-shell.html`) and its resources. `-DBUILD_WASM_DEMOS=ON`
adds the two validation apps below to the same build. What the demo leaves
out in the browser is listed in
[`Docs/UltraCanvas/UltraCanvasWebAssembly.md`](../../../Docs/UltraCanvas/UltraCanvasWebAssembly.md).

Application executables additionally need the bundled fonts in the virtual
FS at `/share/media/fonts`:

```
--preload-file media/fonts@/share/media/fonts
```

(`GetExecutableDir()` cannot resolve `/proc/self/exe` in the browser, so
`GetResourcesDir()` falls back to `./share/` with the MEMFS cwd of `/` —
**not** the `/usr/share/...` path desktop packages use.)

The core library propagates `-pthread -fwasm-exceptions
-sPTHREAD_POOL_SIZE=8 -sALLOW_MEMORY_GROWTH=1 -sGROWABLE_ARRAYBUFFERS=1
-sSTACK_SIZE=5MB -sNO_EXIT_RUNTIME=1` to executables (the growable
SharedArrayBuffer, where the browser has it, spares every JavaScript-side
heap access the check that memory growth with threads otherwise costs), plus the exports the backend's
JavaScript bridges call (`-sEXPORTED_FUNCTIONS=_main,_malloc,_free` and
`-sEXPORTED_RUNTIME_METHODS=UTF8ToString,stringToUTF8,stringToNewUTF8,lengthBytesUTF8,HEAPU8,FS`).
An application that sets `EXPORTED_FUNCTIONS` itself must keep these.

The JavaScript inside `EM_ASM` blocks is collected by the C preprocessor, so
a **comma at the top level of the block** (outside any parentheses or
brackets, e.g. `var a = 1, b = 2;`) splits the macro arguments and breaks
the build. Use one declaration per statement.

### 3. Serving

Threads need `SharedArrayBuffer`, which browsers only enable on
cross-origin-isolated pages. Serve with:

```
Cross-Origin-Opener-Policy: same-origin
Cross-Origin-Embedder-Policy: require-corp
```

`demo/serve.py` is a minimal python server that sets both.

### Application lifetime rule

`app.Run()` **never returns** in the browser: `emscripten_set_main_loop`
unwinds `main()`'s frame, and with `-fwasm-exceptions` that unwind runs the
destructors of `main()`'s locals while the main loop keeps ticking. The
application object therefore must NOT be a stack local — give it static (or
heap) storage duration:

```cpp
int main(int, char* argv[]) {
    static UltraCanvasApplication app;   // static, NOT a stack local
    ...
    app.Run();
}
```

Window objects are safe either way — the application's window registry holds
owning references.

## Validating in a browser

`demo/` contains two apps used to validate the backend, and the repository
root builds the demo application. Build them with `emcmake cmake` as above,
serve the build directory with `serve.py`, and open:

- `UltraCanvasDemo.html` - the framework's demo application
  (`Apps/DemoApp/`), the same page tree as the desktop build less the pages
  whose backend the browser has not got (audio, video, the 25 MB STL
  sample). `?component=<page id>` opens a page directly and `?about=0`
  skips the About window, as `--component` and `--no-about` do on the
  desktop. Its `.data` bundle is the fonts, icons, images, vector samples,
  the smaller 3D models, the docs and the demo's own sources - about
  50 MB, downloaded once before the page starts.
- `wasm-demo.html` - the minimal app: the window canvas
  (`<canvas id="ultracanvas-window-N">`) shows pango-rendered text and a
  clickable button.
- `ultraos-frontpage.html` - the https://ultraos.eu front page built out of
  UltraCanvas elements (`demo/ultraos-frontpage.cpp`): the site's header
  with its drop-down navigation (popup menus), the hero, the desktop and
  mobile cards, the reasons for ULTRA OS, the newsletter form, the status,
  the two pre-order offers, the donation links, the social networks, the
  ULTRA OS variants, the purpose-based editions, the FAQ accordion, the
  contact form and the footer, in a scrolling page that fills the browser
  viewport. It exercises the layout engine (Block for the stacked sections,
  wrapping Flex rows for the cards and the navigation), wrapped text, hover
  states, images preloaded into the virtual FS, text inputs, a text area, a
  checkbox, popup menus, wheel scrolling, and the History API: a link into
  the site opens a page of the demo under `?page=<site path>` (`PushUrl`),
  the back button returns to the front page (`SetOnNavigate`), and a reload
  opens the same page again; links to other sites, `mailto:` and `tel:` go
  through `OpenURL()`. The texts are the page's, in tables at the top of the
  file; the photos the page shows are placeholder tiles except the three in
  `media/images/ultraos/`.

The manually dispatched `wasm-build.yml` workflow configures the repository
root with `-DBUILD_WASM_DEMOS=ON`, so one library build gives all three
pages, in five variants - `baseline`, `min-size` (`-Os`), `no-vips`,
`four-fonts` (`-DUC_WASM_FONTS=...`, the four the front page uses) and
`single-thread` - writes each variant's download sizes (raw, gzip, Brotli)
into its job summary, and uploads each as the `wasm-demo-<variant>`
artifact, one folder with the three pages and `serve.py`: unzip one, run
`python3 serve.py 8080` there and open any of the pages. The browser
console then shows `first frame presented N ms after navigation start`,
the port's time to first paint on that machine. The framework's
`debugOutput` stream also lands in that console (stderr) in every build
configuration, so a page that fails to build says why there
(`Error creating example for <page>: ...`); a page can turn it off with
`Module.ENV.ULTRACANVAS_DEBUG_LOG = "0"` in a `preRun` hook. The last three variants
are measurements that may fail to build; they never fail the run.

After the upload, each variant's job opens its pages in the runner's Google
Chrome, headless, with `demo/smoke.mjs`: every page must present a frame
(the `first frame presented` line) and keep a console free of
`Error creating example`, uncaught errors and Emscripten aborts, or the job
fails. The demo's start page, a few of its pages (`?component=...`) and the
two validation apps are opened; the screenshots and console logs are the
`wasm-smoke-<variant>` artifact. The same script runs on a local build:

```
npm install --no-save playwright          # the package; the browser is yours
SMOKE_CHANNEL=chrome node UltraCanvas/OS/WASM/demo/smoke.mjs build-wasm \
    UltraCanvasDemo.html 'UltraCanvasDemo.html?component=jpegimages'
```

`SMOKE_CHROME=<binary>` names a Chromium instead of a channel, and
`SMOKE_OUT` the directory for the screenshots and logs.

The same two sources build as desktop applications - configure the
repository root with `-DBUILD_WASM_DEMOS=ON` and run `build/ultraos-frontpage`
or `build/wasm-demo` - so a page can be checked on the desktop before the
wasm pipeline runs; the only `__EMSCRIPTEN__` branch in them is the
`Maximize()` that fills the viewport.

## Known limitations

- **Image formats**: the sysroot's libvips reads and writes PNG, JPEG, TIFF
  and WebP (libpng, libjpeg-turbo, libtiff and libwebp are built into the
  sysroot) and reads GIF with its built-in decoder; QOI comes from the
  framework's own codec and SVG from the Vector plugin (next item). No
  HEIC / AVIF (libheif with dav1d or aom is a build of its own and megabytes
  of download) and no BMP (ImageMagick). The demo's bitmap pages say so in
  place of the picture; an application gets "format not supported" from
  `UCImage`. A build without libvips (the `no-vips` and `single-thread`
  variants) reads PNG through cairo's own reader and SVG through the Vector
  plugin, nothing else.
- **SVG needs the Vector plugin registered**: the sysroot's libvips has no
  SVG loader (librsvg / resvg are not built), so `UCImage` reads an .svg
  through the vector preview seam the Vector plugin installs
  (`RegisterVectorFormatsPlugin()`, as the demo's `main.cpp` does) and
  renders it with the core's vector renderer. An application that registers
  no plugins gets "format not supported" for every SVG icon.
- **Clipboard**: text only, and paste depends on the browser firing a
  `paste` event for Ctrl/Cmd+V. Shift+Insert and menu-driven Paste use the
  last text `navigator.clipboard.readText()` returned, which Chromium allows
  after a permission prompt and Firefox/Safari mostly refuse. No images or
  file lists.
- **File dialogs**: `OpenFile` / `OpenMultipleFiles` / `SaveFile` /
  `SelectFolder` return Cancel - a browser picker cannot answer
  synchronously. `SaveContent()` downloads; `WASMBrowser::PickFilesAsync()`
  imports.
- **Message dialogs** map onto `confirm()`'s two buttons; Yes/No/Cancel and
  Abort/Retry/Ignore lose their third choice. Password prompts return Cancel.
- **Mouse capture**: drags that leave the canvas stop receiving moves until
  the pointer re-enters (needs the Pointer Events capture API).
- **Custom cursor images** fall back to the standard cursor (CSS cannot
  reference files inside the Emscripten virtual FS).
- **IME composition, drag & drop between windows, window icons**: not
  implemented.
- Presentation converts and uploads the full surface on every composition;
  damage-rect-limited `putImageData` is an easy future optimisation.
- The browser logs a "blocking on the main thread" warning at startup
  (fontconfig scans the preloaded fonts synchronously); harmless today, but
  moving font setup off the main thread would silence it.
- Widget-level std::thread use (Album/Filer background scans) relies on the
  preallocated pthread pool (8 workers); raise `-sPTHREAD_POOL_SIZE` for
  apps that spawn more concurrent threads.

## History

The previous contents of this directory (including a hand-written Canvas 2D
`IRenderContext` and its own `RunNative()` loop) targeted an early-2025
snapshot of the framework, was never wired into the build, and could not
compile against the current interfaces. It was replaced wholesale in 2026-08;
only the browser utility classes in `UltraCanvasWASMSupport.*` were kept.
The first full Emscripten link and in-browser validation followed in the
same cycle, together with `build-wasm-sysroot.sh` and the `demo/` app.
2026-09 added the per-platform symbols real applications link against
(native dialogs, file loader), the clipboard bridge, and rewrote the browser
utilities, whose kept 2025 code had returned JavaScript promises through
`EM_ASM_INT` and downloaded empty blobs. These additions were written against
the Emscripten API and syntax-checked with stub headers on a desktop
compiler; they have not yet been exercised in a browser - the manually
dispatched `wasm-build.yml` workflow is the place to do that.
