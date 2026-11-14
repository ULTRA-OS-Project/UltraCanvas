# Brief: the UltraStore web store as a WebAssembly site

A working brief for the chat that builds the UltraStore web store: the shop
of the design canvas (`design/README.md`: `Main`, `AppDetail`,
`Categories`, `PhoneHome`, `PhoneDetail`) as a site any browser opens,
rendered by UltraCanvas compiled to WebAssembly. The brief says what exists
in this repository to build on, the rules that keep the site small and
fast without giving up quality, and what done looks like. It is a plan,
not a record: what the build finds different is corrected here.

Read first: [`README.md`](README.md) (the store, the catalog, *The
window*), [`design/README.md`](design/README.md) (the look, in numbers),
[`../UltraCanvas/UltraCanvasWasmRuntime.md`](../UltraCanvas/UltraCanvasWasmRuntime.md)
(the runtime that loads apps as side modules),
[`../UltraCanvas/UltraCanvasWebAssembly.md`](../UltraCanvas/UltraCanvasWebAssembly.md)
(the browser as a platform), `UltraCanvas/OS/WASM/README.md` (the sysroot
and the flags), and §4.3 of
[`../Research/UltraPressDesignProposal.md`](../Research/UltraPressDesignProposal.md)
(the measured sizes and which levers move them).

## What the repository already has

| Need | What is there | Where |
|---|---|---|
| UltraCanvas in the browser | The Emscripten backend: Cairo/Pango rendering onto a `<canvas>`, input, fonts, `WASMNetwork::FetchTextAsync`, `WASMResourceLoader::LoadImage` (the browser decodes any format it knows, pixels come back), the History API (`WASMURL`, `SetOnNavigate`) | `UltraCanvas/OS/WASM/`, `UltraCanvasWebAssembly.md` |
| **The framework separate from the site's content** | **The UltraCanvas runtime**: UltraCanvas linked once as an Emscripten *main module* (`-sMAIN_MODULE`), served by the site or installed by the browser extension; an app is a *side module* of its own code only, `ultracanvas_wasm_app(<target> <sources>)` in `cmake/UltraCanvasWasmApp.cmake`, loaded with `dlopen` and its ordinary `main()` called. Measured: the sample app is 8.5 KB compressed on top of a runtime of 9.9 MB compressed, paid once per browser (extension) or once per site (served, one-year cache). Needs `-DULTRACANVAS_WASM_THREADS=OFF -DULTRACANVAS_WASM_DYNAMIC=ON` and the `-fPIC` sysroot. Status: a prototype, Chromium only, an app bound to the runtime build it was compiled against (`data-runtime-version` makes the loader fall back to the site's copy). | `UltraCanvasWasmRuntime.md`, `UltraCanvas/OS/WASM/runtime/` |
| The other shape | One Emscripten page with the framework linked in: 6.5 MB module raw, 2.05 MB Brotli, plus the fonts `.data`; every site pays it. CI builds it in four variants and prints a *Demo sizes* table each run. | `.github/workflows/wasm-build.yml` |
| A page-like app, the pattern | `ultraos-frontpage.cpp`: one scrolling container filling the window, Block layout for wrapped text, Flex rows, popup menus, several pages from one module through `?page=`, the rebuild posted to the UI thread, `Maximize()` under `__EMSCRIPTEN__` | `UltraCanvas/OS/WASM/demo/` |
| The catalog | `catalog.json`, signed (Ed25519 over the bytes), `Catalog.h`'s `ParseCatalog` is pure JSON and `Verifier.h` checks the signature through UltraCrypt, which builds under Emscripten; `CatalogClient` fetches through UltraNet, which the browser build compiles out | `Apps/UltraStore/core/` |
| The look | Every colour, size, radius and font of the shop pages, and the app icons as `media/appicon/*.png` and `.svg` | `design/`, `media/appicon/` |
| Image export | `UCImage` writes PNG, JPEG, WebP (quality 80, effort 4), AVIF (quality 65, speed 6), JPEG XL through libvips on the desktop (`UltraCanvasImage.h`, the export option structs); the admin backend's `ImportMedia` copies an app's icon and screenshots into `media/<id>/` | `UltraCanvasImagePerformanceTest.md`, `Apps/UltraStoreAdmin/core/` |

## The decisions to make first, and the recommended answers

1. **Runtime plus side module, not one page per site.** The web store is
   an UltraCanvas app built with `ultracanvas_wasm_app()`, served next to
   the runtime the site hosts (`site/runtime/`, content-hashed file names,
   `Cache-Control: public, max-age=31536000, immutable`, Brotli). A visitor
   with the extension downloads the store's own code only; one without pays
   the runtime once for the whole site. The alternative, one page with the
   framework linked in, is the fallback if the runtime's prototype limits
   (below) block the build; it is the same C++ either way.
2. **Single-threaded.** Dynamic linking needs it, and so does a static host
   that cannot set the cross-origin isolation headers. It costs nothing the
   store needs (10 KB smaller than the threaded build). Without threads
   there is no libvips in the module: **pictures are decoded by the
   browser** (`WASMResourceLoader::LoadImage`), which is what the size rules
   below want anyway.
3. **The catalog comes from the same origin and is verified in the
   browser.** The site lives at the catalog base (decided 2026-11-14), so
   `WASMNetwork::FetchTextAsync("catalog.json")` and its
   `.sig`, `ParseCatalog`, and `VerifyCatalogSignature` through UltraCrypt,
   which builds under Emscripten (decided 2026-11-14). The web store trusts
   what the native store trusts: the compiled-in public keys of
   `Apps/UltraStore/keys/` and the `trusted_keys` an accepted catalog
   announces, kept in `localStorage` the way `CatalogClient` keeps them in
   its cache, with the same sequence check against a rollback. A catalog
   that does not verify is not shown; the page says so and offers the
   native store's download. Build `UltraStoreCore` for the browser without
   `CatalogClient` and `Downloader` (they are UltraNet's) rather than
   rewriting the parser and the verifier: a `ULTRASTORE_NO_NET` switch in
   `Apps/UltraStore/CMakeLists.txt`, or the two files left out of the
   browser target.
4. **Buying and installing stay with the native store and the payment
   service.** *Install* on the web is a deep link (`ultrastore://app/<id>`,
   with `?code=` when a code is in force) and, where the store is not
   installed, the download page of the store; *Buy* opens the payment
   service's hosted checkout by redirect, never in an embedded frame. No
   account, no cart: the design has none.
5. **An HTML shadow of every page.** The build emits plain HTML for the
   Discover page, each category and each app page from the same catalog
   (title, summary, description, icon, price, the deep link). It is what
   search engines, link previews and a browser without WebAssembly read,
   and what shows while the module loads; the renderer takes the viewport
   over when it is ready. The emitter is a small generator run at publish,
   written in the admin backend's publish or as its own command.

## The size rules

Every rule has a number; the build prints the numbers and fails when one
is over. Measure with `wasm-build.yml`'s *Demo sizes* step and
`UltraCanvas/OS/WASM/runtime/test/runtime-test.mjs`.

| Rule | Budget |
|---|---|
| The store's side module, Brotli | ≤ 150 KB. The sample app is 8.5 KB; a page of the shop's size should stay under 100 KB. No plugin (Vector, Charts, Models) in the app: the runtime carries the core only, and a reference into a plugin fails the load. |
| The runtime, served by the site | Whatever the framework release weighs, but **measured and named**: the file name carries the content hash, the page carries `data-runtime-version`, and the four levers of §4.3 are tried in this order: the font subset (0.94 MB Brotli saved), no libvips (0.65 MB, already so single-threaded), `-sMAIN_MODULE=2` with an export list instead of exporting every symbol, `wasm-opt -Oz`. `-Os` alone saves nothing worth having. |
| Fonts | Manrope (700, 800) and Inter (400, 500, 600, 700), Latin plus the characters the catalog's languages need, subset with `pyftsubset` to TTF (Pango reads TTF, not WOFF2) and preloaded at `/share/media/fonts`. Target ≤ 400 KB for all six files. No Ubuntu family in the store's `.data` at all. |
| Pictures per page, Brotli-irrelevant (already compressed) | Discover ≤ 300 KB on the first screen, an app page ≤ 500 KB with its screenshots, lazily: fetch a screenshot when its row scrolls into view, never all of them at start. |
| First paint | The HTML shadow at once; the rendered page within 1 s after the runtime is cached, within 4 s on a cold 4G connection with the runtime served. Measure on a mid-range laptop and a phone, Chromium and Firefox. |
| Requests | One `catalog.json` (Brotli, ≤ 60 KB for a hundred apps; split per category only above that), the icons the first screen shows, nothing else before the first paint. |

## The picture rules

The catalog's pictures are produced once, at publish, by the admin backend,
never resized in the browser or in the store's code.

- **Formats.** Every raster picture is published as **AVIF** (quality 65)
  and **WebP** (quality 80) with the original kept as the fallback the
  catalog names today; the web store asks the browser which it decodes
  (`<picture>`-style choice in the loader: AVIF first, WebP second). Icons
  that exist as SVG (`media/appicon/*.svg`) are rasterised at publish: the
  runtime has no Vector plugin, and an inline SVG icon would cost the
  plugin in the module. Lossless WebP for icons and flat UI pictures,
  lossy for screenshots. JPEG XL waits for the browsers.
- **Sizes.** Icons at 64, 128 and 256 px (the card, the app page, the hero
  at 2×); screenshots at 560 and 1120 px wide (the design's 560 px column,
  at 1× and 2×), never above 1120; the hero picture at 1280 and 1920 px.
  The store picks by the device pixel ratio and the slot, never
  downscales a bigger file in the browser. Metadata stripped; sRGB; no
  alpha where the picture has none.
- **Budgets per file.** An icon ≤ 12 KB at 256 px, a screenshot ≤ 120 KB
  at 1120 px, the hero ≤ 180 KB at 1920 px; `ImportMedia` warns above
  them and publish lists the offenders.
- **Where it runs, and how the catalog names them.** The admin backend's
  `ImportMedia` (desktop, libvips, `WebpExportOptions` /
  `AvifExportOptions`) writes the variants beside the original into
  `media/<id>/`, and **the catalog carries them explicitly** (decided
  2026-11-14): beside `icon` and `screenshots`, which stay as they are for
  every store built so far, an app gets

  ```json
  "icon_variants": [
    {"format": "avif", "width": 64,  "url": "media/org.ultraos.ultravideo/icon-64.avif"},
    {"format": "webp", "width": 64,  "url": "media/org.ultraos.ultravideo/icon-64.webp"},
    {"format": "avif", "width": 256, "url": "media/org.ultraos.ultravideo/icon-256.avif"}
  ],
  "screenshot_variants": [
    [{"format": "avif", "width": 560, "url": "…/shot-1-560.avif"}, {"format": "webp", "width": 1120, "url": "…/shot-1-1120.webp"}]
  ]
  ```

  one list per screenshot, in the screenshots' order; `width` in pixels,
  `format` the file's (`avif`, `webp`, `png`, `jpeg`), `url` relative to
  the catalog like every media path. A store that does not know the keys
  ignores them, so the catalog stays schema 2; `Catalog.h` gains the two
  fields (`MediaVariant {format, width, url}`), `ParseCatalog` reads them
  leniently, the writer emits them when present, and the store's README,
  *Catalog*, lists them in the Media row with this shape. The web store
  picks the variant by format the browser decodes, then the smallest width
  at or above the slot times the device pixel ratio; without variants it
  falls back to `icon` / `screenshots`.
- **Decorative pictures of the site itself** (the hero's mock-up, the "why
  UltraStore" tiles) are drawn by UltraCanvas elements, as the design does
  with `div`s, not shipped as pictures.

## The quality rules

- The pages are the design's, to the number: 1280 px column, 32 px side
  padding, 20 px card radius, pill buttons 40/44/52 px, the colours and
  type of `design/README.md`; at phone width the header wraps, cards go
  to one column and the phone artboards are the reference.
- Text contrast 4.5:1 everywhere, 3:1 above 24 px; every control reaches
  by Tab; the search field is a real input with a label; icon-only
  buttons carry a tooltip that names them. The HTML shadow carries the
  same text, so a screen reader and a search engine read the store.
- Every text goes through `Tr`; the catalog's texts come in the browser's
  language (`navigator.language`), falling back to the app's source
  language, as the native store does. Categories are the fixed list's
  names in the person's language.
- No star ratings and no install counts: the catalog has none.
- A page is reachable by URL (`?page=app/org.ultraos.ultravideo`,
  `?page=category/video`), the back button works, a reload keeps the
  page: the front-page demo's History API pattern.
- Works in Chromium and Firefox; Safari is tried and its gaps listed.

## What done looks like

1. `Apps/UltraStore/web/` (or `Apps/UltraStoreWeb/`): the side module's
   sources, the site's `index.html` and loader as templates the admin
   backend's publish lays into the publish folder beside `catalog.json`
   (the site's document root is the catalog base), the runtime's place,
   the HTML shadow's output folder, and a README with the measured sizes.
2. `wasm-build.yml` builds it beside the demos and prints its sizes; the
   runtime test opens it in Chromium, loads the catalog from a local
   server, opens an app page, follows a category, and checks the budgets.
3. The admin backend publishes the picture variants, names them in the
   catalog (`icon_variants`, `screenshot_variants`) and writes the HTML
   shadow into the publish folder; `publish` warns on a picture over
   budget. `Catalog.h` reads and writes the two fields, with a test in
   `Tests/UltraStore` that a catalog without them still parses.
4. The store's README, *The window*, says the web store exists and what it
   does not do (install, buy, verify) and why; the design README's note on
   the phone artboards points at it.
5. A changelog entry for UltraStore and one for UltraStoreAdmin.

## Open questions for the owner

- Which host serves the site, and can it set headers and Brotli? (A host
  that cannot set headers is served the single-threaded build, which is
  the plan anyway; Brotli can be pre-compressed files if the host serves
  `.br`.)
- Does the operator want the extension path at all, or is the site-served
  runtime enough for the first release?

Answered on 2026-11-14: the catalog carries the picture variants
explicitly (the shape above); the signature is verified in the browser
because UltraCrypt builds under Emscripten; and **the catalog's URL is the
site's origin** - the site is served from the catalog base
(`catalog.base_url`, the same host and path as `catalog.json`, the media
and the free packages), so `catalog.json`, `catalog.json.sig` and every
picture are same-origin fetches with no CORS and no proxy. The publish
folder is therefore the site's document root: the admin backend's publish
writes the store's `index.html`, the loader, the runtime (or its
`data-runtime-version`) and the HTML shadow into it beside the catalog,
and `upload` sends them with everything else, `catalog.json` last.
