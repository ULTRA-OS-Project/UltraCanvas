- **`wasm-build.yml` uploads the UltraStore web store it builds.** The
  dynamic variant compiled the store's side module, laid out `site/` and
  opened it in Chromium, and then kept none of it: the run's
  `ultrastore-web-dynamic` artifact held only the browser test's ten
  screenshots and console logs, so the archive with the store's name had no
  WebAssembly in it and a green run read as a build that produced nothing.
  The variant now stages a whole store right after the build - the test's
  fixture store with the build's `ultrastore-web.wasm`, loader, catalogues
  and runtime laid over it the way the admin backend's publish lays them
  (`Apps/UltraStore/web/test/stage-store.mjs`: the runtime under
  `runtime/<hash>/`, every page naming that folder and the build's runtime
  version) - and uploads that folder as `ultrastore-web-dynamic`, failing
  the job when it is empty or the build's `site/` lacks the module, the
  loader or a runtime file. It works as unzipped: its `README.txt` gives the
  one command that serves it. The screenshots and logs move to
  `ultrastore-web-smoke-dynamic`, named as `wasm-smoke-<variant>` is beside
  `wasm-demo-<variant>`.
  - The browser test (`webstore-test.mjs`) serves that one folder as a
    static host would (`UWS_STORE`), in place of the fixture with the
    build's files mapped in and the pages' runtime version rewritten by its
    own server - so the store a run's artifact holds is the store the run
    tested. Run by hand it stages into a temporary folder, as before from
    two build folders.
  - A test store, never a release: the catalog is the fixture's, and the
    module CI builds trusts the fixture's key beside the store's own.
