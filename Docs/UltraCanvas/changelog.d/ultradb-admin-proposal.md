- **Investigation: UltraDBAdmin, a phpMyAdmin-style manager for
  UltraDatabase** (`Docs/Research/UltraDBAdminDesignProposal.md`). Recommends
  a native client rather than a web page, on SQLite and PostgreSQL first and
  MySQL/MariaDB after editing, with staged edits, previewed SQL, vault-held
  credentials and read-only access to ULTRA OS apps' own stores - writable
  only once the app is closed and a backup is taken. A browser build is the
  same app compiled to WebAssembly against an authenticated relay that
  exposes only the server's configured connections. Lists what UltraDatabase
  must gain first - schema introspection, column metadata, cursors,
  cancellation, identifier quoting, every result of a batch, exact `numeric`
  values - two framework additions (in-place editing in
  `UltraCanvasListView`, tabular export over a row source), and what
  libmariadb's LGPL-2.1 obliges a distributor to do.
