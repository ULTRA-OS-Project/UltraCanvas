# The UltraStore web store

The shop of the design canvas ([`Docs/UltraStore/design/README.md`](../../../Docs/UltraStore/design/README.md):
`Main`, `AppDetail`, `Categories`, the phone artboards at phone width) as a
site any browser opens, rendered by UltraCanvas compiled to WebAssembly.
The brief it was built from, with what the build found different, is
[`Docs/UltraStore/WebstoreBrief.md`](../../../Docs/UltraStore/WebstoreBrief.md);
the store itself is [`Docs/UltraStore/README.md`](../../../Docs/UltraStore/README.md).

## What it is

- **An UltraCanvas application**, `WebStore.cpp`: Discover (the hero, the
  categories, the editor's picks, the newest releases, the free essentials,
  why UltraStore), the categories with the price filter and the order, an
  app's page (badges, the action, the facts, the screenshots, about,
  what's new, what it installs, the information card, more from the
  publisher), the search in the header. `StoreTheme.h` of the native window
  gives it the same colours, type and shapes.
- **On the UltraCanvas runtime**: under emcmake it is a *side module*,
  `ultrastore-web.wasm` - this code and the catalog's reader
  (`core/Catalog.cpp`, `core/UltraStoreTypes.cpp`), nothing of the
  framework (`cmake/UltraCanvasWasmApp.cmake`). A visitor's browser gets
  UltraCanvas once, from the UltraCanvas extension when it is installed,
  else from the site's `runtime/<hash>/`, and the page's loader
  (`UltraCanvas/OS/WASM/runtime/site/ultracanvas-loader.js`, unchanged)
  picks between the two.
- **The catalog from its own origin, verified in the browser**: the site
  is served from the catalog base, so `catalog.json` and `catalog.json.sig`
  are same-origin fetches. The signature is checked through the runtime's
  UltraCrypt on libsodium (the native store's `Verifier.cpp`, compiled
  into the module when the build has libsodium) and otherwise with
  WebCrypto's Ed25519, against the keys built in (`Apps/UltraStore/keys/*.pub`, the
  same as the native store's, through `cmake/UltraStoreKeys.cmake`) and
  the keys the last accepted catalog announced, kept in `localStorage`
  with the highest sequence seen, so a mirror cannot roll a visitor back.
  A catalog that does not verify is not shown; the page says so and offers
  the native store's download.
- **Pictures at the catalog's sizes**: the admin backend publishes every
  icon at 64, 128 and 256 px and every screenshot at 560 and 1120 px, as
  AVIF and WebP, and names them in the catalog (`icon_variants`,
  `screenshot_variants`). The store asks the browser which format it
  decodes and picks the smallest width at or above the slot times the
  device pixel ratio; nothing is resized in the browser. Screenshots are
  fetched on the app page only.
- **Installing, trying and buying stay with the native store**: the
  buttons open `ultrastore://app/<id>`; where that finds no store, the page
  offers the download named by the admin's setting
  `web.store_download_url`. No account, no cart, no payment call from the
  web.
- **The texts in the visitor's language**: the site carries the store's
  catalogue and the standard set per language (`locale/<lang>/`), and the
  module fetches the browser's first non-English language's before it
  draws (`LoadLocale`), so the chrome is translated along with the
  catalog's own texts. English needs no fetch.
- **Every page has an address**: `?page=app/<id>`, `?page=category/<id>`,
  `?page=categories` from the root, and the folders the admin backend
  publishes (`app/<id>/`, `category/<id>/`, `categories/`), each an HTML
  shadow of the page - the texts, the icon as a `<picture>`, the deep
  link, a canonical link and the preview tags - that search engines read,
  that shows while the module loads, and that a browser without
  WebAssembly keeps. The back button works (the History API).

## Files

```
Apps/UltraStore/web/
  WebStore.h/.cpp        the pages and the catalog behind them
  WebPlatform.h/.cpp     the browser (fetch, WebCrypto, localStorage, the address bar, the
                         browser-decoded pictures) and the desktop (a publish folder, UltraCrypt)
  main.cpp               the entry: the runtime calls it in the browser; a program natively
  CMakeLists.txt         ultrastore-web.wasm and site/ under emcmake; ultrastore-web natively
  site.json.in           the runtime's version, read by publish for data-runtime-version
  test/webstore-test.mjs the browser test (below)
  test/stage-store.mjs   lays a build's site/ over test/fixture: a whole store in one folder,
                         the one the test opens and CI uploads
  test/make-fixture.sh   makes test/fixture with the admin backend
  test/fixture/          a store published by the admin backend: the signed catalog, the
                         pictures with their variants, the HTML pages, the test key's public half
```

What the build leaves under emcmake, `site/`, is what the admin backend's
`publish` lays beside `catalog.json` when the setting `web.site_dir` names
it: `ultrastore-web.wasm`, `ultracanvas-loader.js`, `ultrastore.png`,
`site.json`, `locale/<lang>/{UltraStore,Standard}.json`,
`runtime/ultracanvas-runtime.{js,wasm,data}`. Publish copies
the runtime to `runtime/<first 12 hex digits of the .wasm's SHA-256>/`, so
the host serves it with `Cache-Control: public, max-age=31536000,
immutable` and a new runtime is a new folder; Brotli is the host's
(pre-compressed `.br` files work with a static host).

## Building

Natively, with the demos, where the store's core is built:

```
cmake -S . -B build -DBUILD_WASM_DEMOS=ON
cmake --build build --target ultrastore-web ultrastoreadmin
Apps/UltraStore/web/test/make-fixture.sh build          # a publish folder to look at
build/ultrastore-web Apps/UltraStore/web/test/fixture [app/org.ultraos.ultravideo] \
    --download-url https://ultraos.eu/ultrastore
```

The native program trusts the keys of `Apps/UltraStore/keys/`; to look at
the fixture, configure with
`-DULTRASTORE_WEB_TEST_KEYS=$(cat Apps/UltraStore/web/test/fixture/trusted-key.hex)`.

In the browser, the dynamic WASM build (`UltraCanvas/OS/WASM/README.md`,
`Docs/UltraCanvas/UltraCanvasWasmRuntime.md`):

```
emcmake cmake -B build-wasm -S . -DCMAKE_BUILD_TYPE=Release \
    -DULTRACANVAS_WASM_THREADS=OFF -DULTRACANVAS_WASM_DYNAMIC=ON -DBUILD_WASM_DEMOS=ON
cmake --build build-wasm --target ultrastore-web-site
```

leaves `build-wasm/Apps/UltraStore/web/site/`. `wasm-build.yml`'s dynamic
variant builds it on every change to `Apps/UltraStore/**`, prints the
module's size in its *Demo sizes* table, uploads the store and runs the
browser test.

### The store a CI run built

`site/` alone opens nothing: it has no page and no catalog, which are the
admin backend's to publish. So the dynamic variant stages a whole store
right after the build, before any test runs:

```
node Apps/UltraStore/web/test/stage-store.mjs build-wasm/Apps/UltraStore/web <new folder>
```

That is the fixture (`test/fixture`) with the build's module, loader, mark
and catalogues laid over it and the runtime under `runtime/<hash>/`, every
page naming that folder and the build's runtime version, as publish does it
(`Apps/UltraStoreAdmin/core/WebSite.cpp`). The workflow uploads the folder
as the run's **`ultrastore-web-dynamic`** artifact. Download it from the
run's page, unzip it, and in the folder

```
python3 -m http.server --bind 127.0.0.1 8080
```

shows the store at `http://127.0.0.1:8080/` (its `README.txt` says the
same). It holds `ultrastore-web.wasm`, `ultracanvas-loader.js`,
`runtime/<hash>/ultracanvas-runtime.{js,wasm,data}`, `locale/`, the catalog
with its signature, the pages and the pictures. A test store, never a
release: the catalog is the fixture's and the module trusts the fixture's
key (see *Testing*). The test's screenshots and console logs are a second
artifact, `ultrastore-web-smoke-dynamic`.

## Testing

```
npx playwright install chromium
node Apps/UltraStore/web/test/webstore-test.mjs build-wasm/Apps/UltraStore/web build-wasm/UltraCanvas/OS/WASM/runtime
```

stages the store (`stage-store.mjs`, into a temporary folder, or the one
`UWS_STORE` names - a folder that already holds a staged store is served as
it is, which is how CI tests the very folder it uploaded), serves it as a
static host would and opens it in Chromium: the runtime from the site (Discover paints,
the catalog verifies and is accepted, an app page and a category open by
address, `?page=` works, the back button returns, the icons come as the
catalog's WebP variants), a tampered catalog refused, and the runtime from
the extension (the site sends no runtime). It checks the budgets of the
brief: the module at most 150 KB Brotli, Discover's pictures at most
300 KB, an app page's screenshots at most 500 KB, one `catalog.json`.
Screenshots and the console logs land in `uws-test-out/` (`UWS_OUT`).

The fixture is signed with a key made for it; its public half is
`test/fixture/trusted-key.hex`, which the CI build compiles into the test
module with `-DULTRASTORE_WEB_TEST_KEYS`, never into a release. Run
`make-fixture.sh` again to change the fixture (a new key each time) and
commit the result.

## Measured sizes

The *Demo sizes* table of the dynamic variant of `wasm-build.yml` is the
record: every page's raw, gzip and Brotli sizes, the store's module and
the runtime's files among them. The table of the run that merged the
store (2026-11-14, emsdk 6.0.11, `-DCMAKE_BUILD_TYPE=Release
-DULTRACANVAS_WASM_THREADS=OFF -DULTRACANVAS_WASM_DYNAMIC=ON`):

| File | Raw | gzip -9 | brotli -q 11 |
|---|---:|---:|---:|
| UltraCanvasDemo.wasm | 25.18 MB | 8.24 MB | 5.57 MB |
| UltraCanvasDemo.js | 0.29 MB | 0.06 MB | 0.05 MB |
| UltraCanvasDemo.data | 51.92 MB | 26.37 MB | 22.16 MB |
| **UltraCanvasDemo, all files** | **77.38 MB** | | |
| wasm-demo.wasm | 4.32 MB | 1.69 MB | 1.30 MB |
| wasm-demo.js | 0.13 MB | 0.03 MB | 0.03 MB |
| wasm-demo.data | 5.18 MB | 2.58 MB | 1.80 MB |
| **wasm-demo, all files** | **9.63 MB** | | |
| ultraos-frontpage.wasm | 5.20 MB | 2.00 MB | 1.52 MB |
| ultraos-frontpage.js | 0.13 MB | 0.03 MB | 0.03 MB |
| ultraos-frontpage.data | 5.57 MB | 2.97 MB | 2.19 MB |
| **ultraos-frontpage, all files** | **10.91 MB** | | |
| ultrastore-web.wasm (the web store, a side module) | 0.46 MB | 0.13 MB | **0.10 MB** (the brief's rule: 0.15 MB) |

That run measured the runtime raw only, through the browser test: the
`.wasm` 24.2 MB, the `.js` 556 KB, the `.data` 377 KB (the two subset
fonts). From the next run on, the table carries the runtime's three
files with their compressed sizes as well.

Discover's first screen asked for 27 files besides the runtime and the
module, 97 KB of pictures (the rule: 300 KB); UltraVideo's page fetched
its two screenshots at 560 px, 12 KB (the rule: 500 KB). With the
extension installed the site sent 546 KB in all, the module and the page
included.

The runtime's fonts (`UCR_FONTS=subset`, `UltraCanvas/OS/WASM/runtime/CMakeLists.txt`):
Manrope and Inter cut to Latin, Greek and Cyrillic, Inter's optical size
pinned at 14, measured with fonttools 4.68:

| File | Whole | Subset |
|---|---:|---:|
| `Inter-Variable.ttf` | 856 KB | 216 KB |
| `Manrope-Variable.ttf` | 161 KB | 116 KB |
| both | 1017 KB | **332 KB** (the brief's rule: 400 KB) |

## What it does not do

Install, buy or verify a package (the native store's), show ratings or
install counts (the catalog has none), or run without WebAssembly (the
HTML shadow stays, with the deep links and the store's download).
