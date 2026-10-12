- **WebAssembly: the ultraos.eu front page as a demo app.**
  `UltraCanvas/OS/WASM/demo/ultraos-frontpage.cpp` builds the
  https://ultraos.eu front page out of UltraCanvas elements - a header with
  the navigation and the logo, a hero with two call-to-action buttons, the
  "For desktops" / "For mobile" cards, the reasons for ULTRA OS, the three
  ways to support the project, the status timeline, a newsletter form and a
  footer - in one scrollable page that fills the browser viewport. It is the
  second browser validation app beside `wasm-demo` and the first that
  exercises what a real application does in the browser: the layout engine
  (Block for the stacked sections, so wrapped text gets its real height;
  Flex rows that wrap for the cards and the navigation), hover states, an
  image preloaded into the virtual FS, a text input with Enter, wheel
  scrolling, and `OpenURL()` on every link, which opens the real page in a
  new tab. The texts are tables at the top of the file.
  - Both demos build as desktop applications too: `-DBUILD_WASM_DEMOS=ON`
    at the repository root adds `wasm-demo` and `ultraos-frontpage` as
    native targets, so a page can be looked at before the hour-long wasm
    pipeline runs. The demo's `CMakeLists.txt` serves both ways: standalone
    under `emcmake` it adds the library itself, as a subdirectory it uses the
    one already configured.
  - The `WebAssembly build` workflow builds both pages and uploads them with
    `serve.py` in the `wasm-demo` artifact, so the download runs as it is:
    unzip, `python3 serve.py 8080`, open either page.
