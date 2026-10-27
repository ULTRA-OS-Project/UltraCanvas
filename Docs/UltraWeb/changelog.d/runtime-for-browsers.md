- **UltraWeb apps run in Chrome too (prototype).** The UltraWeb runtime
  (`Apps/UltraWeb/browser`) is UltraCanvas compiled with Emscripten around
  UltraWeb's own element-ABI code, unchanged: it runs the same `app.wasm`
  UltraWeb runs, on the browser's WebAssembly engine, in an ordinary page.
  A Manifest V3 extension installs the runtime once, so a site sends only
  its app (the sample app is 4 KB); a site can also serve the runtime
  itself for visitors without the extension. `ultraweb-loader.js` picks
  whichever is there. Fetch goes through the page's `fetch()` under the
  same rules, storage into IndexedDB, the clipboard through the browser's.
  Tested end to end in Chromium (`Apps/UltraWeb/browser/test/runtime-test.mjs`,
  in the `wasm-build.yml` single-threaded variant). This answers question 5
  of the proposal: `Docs/UltraWeb/UltraWebInTheBrowser.md`.
- The UltraNet fetch service moved from `host/UltraWebFetch.cpp` to
  `host/UltraWebNetworkFetch.cpp`, so the fetch rules build without UltraNet.
