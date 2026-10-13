- **The demo application builds for WebAssembly.** `emcmake cmake` at the
  repository root now produces `UltraCanvasDemo.html`: a *WebAssembly
  defaults* block in the root CMakeLists.txt leaves the other applications
  and the sibling modules off (as the Android block does), the demo target
  becomes a page with its own shell (`Apps/DemoApp/wasm-shell.html`, a
  loading line until the runtime is up) and preloads what its pages read -
  fonts, icons, images, vector samples, the smaller 3D models, the docs and
  its own sources - at `/share/...` in the virtual filesystem. The
  application object is static in the browser, where `Run()` never returns,
  the main window maximizes to the viewport, and the page's query string
  stands in for the command line (`?component=piecharts`, `?about=0`). Left
  out in the browser, each at its registration under `#ifndef __EMSCRIPTEN__`:
  the audio pages and the three audio charts (no audio backend), the video
  player (no video backend) and the STL page (a 25 MB sample).
  `-DBUILD_WASM_DEMOS=ON` now also works under emcmake, adding the two
  validation apps to the same build, which is how `wasm-build.yml` builds
  all three pages from one library build and uploads them in one folder with
  `serve.py`. Making the root configure and link under emcmake also fixed
  four framework-side things: a toolchain file now counts as an explicit
  compiler choice in the root CMakeLists.txt, so emcmake is not steered to
  the host's clang; the core is forced static for WebAssembly (the cache
  default is the desktop's shared core, and a wasm "shared library" is a
  dynamic-linking side module); the CUPS probe skips the browser, where the
  host's `cups-config` answered and the link asked for a `-lcups` that does
  not exist; and the root passes `-pthread -fwasm-exceptions` to every
  module in a wasm tree, since VirtualFS, compiled before the core without
  them, stopped the link ("--shared-memory is disallowed by
  VirtualFSManager.cpp.o"). New `cmake/UltraCanvasWasmSysroot.cmake` puts
  the wasm sysroot on `CMAKE_FIND_ROOT_PATH`, so `find_package(ZLIB)` finds
  the zlib it holds and the Models plugin's FBX and .blend readers are built
  for the browser too. The demo's LaTeX page is now built and registered only
  with the LaTeX plugin (`ULTRACANVAS_PLUGIN_LATEX`), as the PDF page is:
  its `CreateLaTeXView()` is the plugin's on-demand loader, so any build
  with the plugin off failed to link the demo. Two wasm sysroot patches
  came out of running the page (`UltraCanvas/OS/WASM/patches/`, applied by
  `build-wasm-sysroot.sh`, whose cache key they change): glib's
  `g_slist_free_full`, `g_list_free_full`, `g_queue_free_full` and
  `g_queue_clear_full` called their one-argument `GDestroyNotify` through
  the two-argument `GFunc` of the foreach helpers, and pango's
  `pango_attr_list_copy` passed `pango_attribute_copy` as a `GCopyFunc` -
  both trap on wasm's indirect-call signature check, the first on the first
  pango markup laid out (the demo's tree view), the second on the first
  attribute list copied (the About window).
  Docs: `Docs/UltraCanvas/UltraCanvasWebAssembly.md` (*The demo application
  in the browser*), `UltraCanvas/OS/WASM/README.md`.
