# UltraCanvas WebAssembly (Emscripten) Backend

**Status: experimental — validated in a browser.** The full stack (this
backend + wasm-compiled cairo/pango/fontconfig/glib/libvips) links with
Emscripten 6.0 and runs in Chromium: windows render through the shared
Cairo/Pango pipeline with the bundled Ubuntu fonts, and mouse input drives
widget callbacks (see [demo/](demo/)). Not yet exercised: broad widget
coverage, touch devices, Firefox/Safari.

## Architecture

The backend reuses the **same Cairo + Pango render context as every desktop
platform** (`libspecific/Cairo/RenderContextCairo.cpp`) instead of
reimplementing `IRenderContext` on the HTML Canvas 2D API. That keeps text
layout (`ITextLayout` is Pango-shaped), dirty-rect composition, popups, the
caret and HiDPI behaviour pixel-identical to the desktop builds.

```
OS/WASM/
├── UltraCanvasWASMApplication.h/cpp   # Event loop bridge, keyboard, cursors, fonts
├── UltraCanvasWASMWindow.h/cpp        # <canvas>-backed window, input, presentation
├── UltraCanvasWASMSupport.h/cpp       # Optional browser utilities (IDBFS, fetch, ...)
├── build-wasm-sysroot.sh              # Cross-compiles the dependency stack
├── patches/                           # wasm fixes applied to the deps by the script
└── demo/                              # Minimal browser validation app
```

| Concern | How it works |
|---|---|
| Main loop | `app->Run()` works unchanged: `RunBeforeMainLoop()` calls `emscripten_set_main_loop(..., simulate_infinite_loop=1)`, which never returns; each animation frame runs one `RunOnce()` iteration. When `running` clears or the last window closes, the tick performs the same shutdown tail `Run()` would. Hidden tabs fall back to a 250 ms `setTimeout` cadence so timers keep firing. |
| Window | One absolutely-positioned `<canvas>` per window (backing store in physical px, CSS box in logical px). `nativeSurface` is an offscreen cairo image surface, like the Windows backend. |
| Presentation | `InvalidateWindowNative()` — the framework's post-composition hook — converts the surface pixels (BGRX) to RGBA and `putImageData()`s them onto the canvas (copying out of the shared wasm heap, which `ImageData` cannot view directly under `-pthread`). |
| Input | Per-canvas Emscripten HTML5 callbacks convert DOM events to `UCEvent`s and `PushEvent()` them (mouse, wheel with notch normalisation, touch with synthesized left-button mouse events, focus). Keyboard is registered once on the browser window and routed to the focused window by `DispatchEvent()`. |
| HiDPI | `deviceScale = devicePixelRatio`; DOM coordinates are CSS px = logical units, so events need no physical→logical conversion. |
| Fonts | Pango + Fontconfig against the bundled Ubuntu/Ubuntu Mono TTFs, preloaded into the virtual FS at `/share/media/fonts` (see below). |
| Screen | The browser viewport (`window.innerWidth/Height`) in CSS px. `Maximize()` fills it; `SetFullscreen(true)` uses the Fullscreen API. |

## Building

### 1. The wasm sysroot (once)

The REQUIRED pkg-config dependencies (cairo + pixman, pango + pangocairo,
fontconfig, freetype, harfbuzz, glib, tinyxml2, libvips + vips-cpp) do not
ship as Emscripten ports. `build-wasm-sysroot.sh` cross-compiles all of them
(plus zlib, libpng, expat, libffi, pcre2) into a static sysroot with
`.pc` files, following [kleisauke/wasm-vips](https://github.com/kleisauke/wasm-vips)
where that project builds the same dependency — including its pre-patched
glib and libvips branches. pango gets a small local patch
(`patches/pango-1.52-wasm-function-pointers.patch`) fixing one-argument
callbacks cast to `GInterfaceInitFunc`/`GFunc`: native ABIs tolerate the
arity mismatch, wasm's strict indirect-call signature checking traps on it.

```bash
source /path/to/emsdk/emsdk_env.sh   # Emscripten 6.x
./build-wasm-sysroot.sh /path/to/wasm-sysroot
```

The whole sysroot is compiled `-pthread -fwasm-exceptions`; the library
build (CMakeLists.txt WASM branch) propagates the same flags PUBLIC, since
the exception/threading ABI must match across every static library in the
final link.

### 2. The library / an application

```bash
source /path/to/emsdk/emsdk_env.sh
export EM_PKG_CONFIG_PATH=/path/to/wasm-sysroot/lib/pkgconfig
emcmake cmake -S . -B build-wasm -DCMAKE_BUILD_TYPE=Release
cmake --build build-wasm
```

Application executables additionally need the bundled fonts in the virtual
FS at `/share/media/fonts`:

```
--preload-file media/fonts@/share/media/fonts
```

(`GetExecutableDir()` cannot resolve `/proc/self/exe` in the browser, so
`GetResourcesDir()` falls back to `./share/` with the MEMFS cwd of `/` —
**not** the `/usr/share/...` path desktop packages use.)

The core library propagates `-pthread -fwasm-exceptions
-sPTHREAD_POOL_SIZE=8 -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=5MB
-sNO_EXIT_RUNTIME=1` to executables.

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

## Validating in a headless browser

`demo/` contains the minimal app used to validate the backend: build it with
`emcmake cmake` as above, serve `build/` with `serve.py`, and open
`wasm-demo.html`. The window canvas (`<canvas id="ultracanvas-window-N">`)
should show pango-rendered text and a clickable button.

## Known limitations

- **Clipboard**: no backend (`InitializeClipboard()` reports failure and the
  framework continues); the browser clipboard needs an async JS bridge.
- **Mouse capture**: drags that leave the canvas stop receiving moves until
  the pointer re-enters (needs the Pointer Events capture API).
- **Custom cursor images** fall back to the standard cursor (CSS cannot
  reference files inside the Emscripten virtual FS).
- **Native dialogs, drag & drop between windows, window icons**: not
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
