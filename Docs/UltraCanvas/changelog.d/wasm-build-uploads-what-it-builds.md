- **`wasm-build.yml` uploads what it builds, not only pictures of it.**
  Three of a run's artifacts were named after a product and held none of
  it: `ultrastore-web-dynamic`, `ultracanvas-runtime-dynamic` and
  `ultraweb-runtime-<variant>` carried the browser tests' screenshots and
  console logs, so the archive with the UltraStore web store's name had no
  WebAssembly in it and a green run read as a build that produced nothing.
  Each name now holds the product, collected right after the build and
  uploaded before any test runs - a build that fails a test is the one
  somebody wants to download - with a `README.txt` and usable as unzipped;
  an empty folder or a missing `.wasm` fails the job. The screenshots and
  logs move to `*-smoke-<variant>`, named as `wasm-smoke-<variant>` is
  beside `wasm-demo-<variant>`.
  - `ultraweb-runtime-<variant>` (the single-threaded variants) and
    `ultracanvas-runtime-dynamic`: each runtime as its build leaves it -
    `site/`, the sample site with the loader, the sample app and the runtime
    under `runtime/`, which any static host serves, and the browser
    extension's folder. The runtime tests open the uploaded folder.
  - `ultrastore-web-dynamic`: `site/`, the folder the admin backend's
    publish takes (`web.site_dir`), and `preview/`, a whole store to look
    at: the test's fixture store with the build laid over it the way publish
    lays it (`Apps/UltraStore/web/test/stage-store.mjs`: the runtime under
    `runtime/<hash>/`, every page naming that folder and the build's runtime
    version). The browser test (`webstore-test.mjs`, `UWS_STORE`) serves
    `preview/` as a static host would, in place of the fixture with the
    build's files mapped in and the pages rewritten by its own server, so
    the store an artifact holds is the store the run tested.
  - `site/`'s module trusts the store's keys alone: the fixture's test key
    is in a second module now, which only `preview/` carries (UltraStore's
    changelog has the build's side of it).
