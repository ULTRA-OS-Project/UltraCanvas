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
  `Docs/UltraCanvas/UltraCanvasWebAssembly.md`).
- **`wasm-build.yml` records what a visitor downloads:** a *Demo sizes*
  step writes each demo's `.wasm`, `.js`, `.data` and worker file into the
  job summary, raw, gzip-compressed and Brotli-compressed, the way the
  packaging jobs report the desktop package sizes. Measurement only; it
  never fails the job.
- `Masterfile_modules.md` §6 said UltraDatabase was "Concept / design only";
  it has had the SQLite core, migrations and the PostgreSQL driver since
  UltraFIBU's multi-user mode was built on them. The registry line now says
  what `Docs/Modules/UltraDatabase/README.md` says.
