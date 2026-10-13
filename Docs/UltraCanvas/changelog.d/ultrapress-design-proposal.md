- **New design proposal: UltraPress, a website and shop builder that
  publishes WebAssembly** (`Docs/Research/UltraPressDesignProposal.md`).
  The investigation decides what "generate WebAssembly instead of HTML and
  JavaScript" can mean and picks one: a site is data, edited on the desktop
  and rendered by one framework renderer built per release with the
  Emscripten port - the ultraos.eu front page demo generalised - with a
  plain HTML shadow of every page for crawlers, mail clients and screen
  readers, and a native UltraWeb app as the second output. It maps the
  brief's twelve features onto the modules the tree has (HTMLReader,
  RichTextEdit, UltraDatabase, UltraNet's SMTP and OAuth2, UltraCloud's
  FTP / SFTP / WebDAV publishing, UltraVault, UltraAI, VirtualFS packages,
  UltraFIBU invoices), names the two missing framework pieces - an HTTP
  server in UltraNet and a site theme model - and lays out a measure-first
  Phase 0, five phases, the risks and nine questions for the user.
- **WebAssembly backend: the History API.** `WASMURL::PushUrl` and
  `ReplaceUrl` change the address bar without leaving the page, `GetPath`
  reads it, and `SetOnNavigate` hears the back and forward buttons (every
  `popstate`) with the new URL - so an application that shows several pages
  from one module is no longer one URL (`UltraCanvasWASMSupport.h`,
  `Docs/UltraCanvas/UltraCanvasWebAssembly.md`). The ultraos.eu front page
  demo uses it: a link into the site opens a page of the demo under
  `?page=<site path>` instead of leaving for the real site, the back button
  returns, and a reload keeps the page; the demo's CMake adds the
  `UltraCanvas/` directory to its include path for the header.
- **`wasm-build.yml` records what a visitor downloads:** a *Demo sizes*
  step writes each demo's `.wasm`, `.js`, `.data` and worker file into the
  job summary, raw, gzip-compressed and Brotli-compressed, the way the
  packaging jobs report the desktop package sizes. Measurement only; it
  never fails the job. First run (emsdk 6.0.11): the ultraos.eu front page
  demo is 11.24 MB raw and 3.9 MB over Brotli, 6.52 MB of it the module
  and 4.57 MB the fonts and pictures; the minimal demo 10.01 MB raw. The
  proposal's §4.3 carries the table. The artifact list no longer names the
  `<demo>.ww.js` worker files, which emsdk 6.0.11 does not emit. The
  workflow then grew into five variants, one job each, for the proposal's
  Phase 0: `baseline`, `min-size` (`-Os`), `no-vips`, `four-fonts` and
  `single-thread`, each with its own size table and `wasm-demo-<variant>`
  artifact; the last three never fail the run.
- **WebAssembly backend: build options for the size measurements.**
  `ULTRACANVAS_WASM_THREADS=OFF` builds the library without `-pthread`, and
  `WASM_THREADS=0` builds `build-wasm-sysroot.sh`'s sysroot the same way,
  for a single-threaded program a static host can serve without isolation
  headers; the demo's `UC_WASM_FONTS` preloads a `;`-list of the bundled
  fonts instead of all fourteen. The window logs its first presented frame
  to the browser console with its time since navigation start, so a tester
  reads the port's time to first paint on any browser.
- `Masterfile_modules.md` §6 said UltraDatabase was "Concept / design only";
  it has had the SQLite core, migrations and the PostgreSQL driver since
  UltraFIBU's multi-user mode was built on them. The registry line now says
  what `Docs/Modules/UltraDatabase/README.md` says.
