- **UltraStore joins the tree.** `Apps/UltraStore` (0.1.0): the store's core
  as a static library and the `ultrastore` command line, built with
  `BUILD_ULTRASTORE` (on by default; the target requires UltraCrypt with
  libsodium and UltraDatabase with SQLite and stops the configure without
  them), tested with `ULTRACANVAS_BUILD_ULTRASTORE_TESTS` on the Linux CI
  legs. Design in `Docs/UltraStore/README.md`.
