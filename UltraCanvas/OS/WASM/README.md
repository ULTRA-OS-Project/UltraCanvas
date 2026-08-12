# UltraCanvas WebAssembly (Emscripten) Backend

**Status: experimental — interface-complete against the current framework,
not yet validated in a browser.** The sources compile against the current
`UltraCanvasApplicationBase` / `UltraCanvasWindowBase` / `IRenderContext`
interfaces (verified with clang against real Emscripten 3.1.61 system
headers), but no full Emscripten link or in-browser run has been performed
yet because that requires a wasm-compiled cairo/pango dependency stack (see
[Building](#building)).

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
└── UltraCanvasWASMSupport.h/cpp       # Optional browser utilities (IDBFS, fetch, ...)
```

| Concern | How it works |
|---|---|
| Main loop | `app->Run()` works unchanged: `RunBeforeMainLoop()` calls `emscripten_set_main_loop(..., simulate_infinite_loop=1)`, which never returns; each animation frame runs one `RunOnce()` iteration. When `running` clears or the last window closes, the tick performs the same shutdown tail `Run()` would. Hidden tabs fall back to a 250 ms `setTimeout` cadence so timers keep firing. |
| Window | One absolutely-positioned `<canvas>` per window (backing store in physical px, CSS box in logical px). `nativeSurface` is an offscreen cairo image surface, like the Windows backend. |
| Presentation | `InvalidateWindowNative()` — the framework's post-composition hook — converts the surface pixels (BGRX) to RGBA and `putImageData()`s them onto the canvas. |
| Input | Per-canvas Emscripten HTML5 callbacks convert DOM events to `UCEvent`s and `PushEvent()` them (mouse, wheel with notch normalisation, touch with synthesized left-button mouse events, focus). Keyboard is registered once on the browser window and routed to the focused window by `DispatchEvent()`. |
| HiDPI | `deviceScale = devicePixelRatio`; DOM coordinates are CSS px = logical units, so events need no physical→logical conversion. |
| Fonts | Pango + Fontconfig against the bundled Ubuntu/Ubuntu Mono TTFs, which must be preloaded into the virtual FS (`--preload-file`). |
| Screen | The browser viewport (`window.innerWidth/Height`) in CSS px. `Maximize()` fills it; `SetFullscreen(true)` uses the Fullscreen API. |

## Building

The core CMake build selects this backend automatically under `emcmake`
(`EMSCRIPTEN` is checked before `UNIX`), forces off the subsystems that
cannot exist in the browser sandbox (OpenGL context managers, audio, video,
UltraNet, runtime-loaded plug-ins), and expects the remaining REQUIRED
pkg-config dependencies to come from a **wasm sysroot**:

- cairo (with pixman), pango + pangocairo, fontconfig, freetype, harfbuzz,
  glib-2.0, tinyxml2, libvips (+ the optional ones you enable)

None of these ship as Emscripten ports; they must be cross-compiled once with
the Emscripten toolchain (or taken from a prebuilt wasm sysroot) and exposed
via `PKG_CONFIG_PATH` / `EM_PKG_CONFIG_PATH`:

```bash
source /path/to/emsdk/emsdk_env.sh
export EM_PKG_CONFIG_PATH=/path/to/wasm-sysroot/lib/pkgconfig
emcmake cmake -S . -B build-wasm -DCMAKE_BUILD_TYPE=Release
cmake --build build-wasm
```

Application executables additionally need the bundled fonts in the virtual
FS, e.g. `--preload-file media/fonts@/usr/share/ultracanvas/media/fonts`
(matching what `GetBundledFontsDir()` resolves to in your build). The core
library already propagates `-sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=5MB
-sNO_EXIT_RUNTIME=1` to executables.

## Known limitations

- **Not yet run in a browser** — the first real build will likely surface
  integration issues (font paths, `SetupBundledFontconfig()` on MEMFS,
  glib event assumptions inside pango).
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

## History

The previous contents of this directory (including a hand-written Canvas 2D
`IRenderContext` and its own `RunNative()` loop) targeted an early-2025
snapshot of the framework, was never wired into the build, and could not
compile against the current interfaces. It was replaced wholesale in 2026-08;
only the browser utility classes in `UltraCanvasWASMSupport.*` were kept.
