- **The web store's `site/` never carries a test key.** A build configured
  with `-DULTRASTORE_WEB_TEST_KEYS` - every CI build, since the browser
  test's fixture catalog is signed with a key made for it - compiled that
  key into the one module there was, so no run left a `site/` a store could
  be published with. Under emcmake the option now builds a second module
  from the same sources, `ultrastore-web-test.wasm`, that trusts the store's
  keys and the test keys; it stays beside `site/`, and `site/`'s
  `ultrastore-web.wasm` trusts `Apps/UltraStore/keys/*.pub` and nothing
  else, whatever the build was configured with. No new keys were needed:
  the production key is the one already there, the native store's.
  Natively the option still adds the keys to the one program, which is a
  tool to look at a publish folder.
  - The browser test holds the build to it: the store's module's bytes hold
    the store's keys and not the fixture's (the test module, which holds
    both, is the control), it is the module the 150 KB Brotli budget is
    measured on, and served in the test module's place it runs and refuses
    the fixture's catalog.
  - `test/stage-store.mjs` stages the store the test opens - the fixture
    with the test module, the loader, the catalogues and the runtime laid
    over it as the admin backend's publish lays a `site/` - and
    `wasm-build.yml` uploads it as `preview/` beside `site/` in the run's
    `ultrastore-web-dynamic` artifact (the framework's changelog has the
    workflow's side of it).
