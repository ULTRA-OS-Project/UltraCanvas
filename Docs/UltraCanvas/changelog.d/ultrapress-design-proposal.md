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
