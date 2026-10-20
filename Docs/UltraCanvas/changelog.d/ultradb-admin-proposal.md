- **Investigation: UltraDB Admin, a phpMyAdmin-style manager for
  UltraDatabase** (`Docs/Research/UltraDBAdminDesignProposal.md`). Recommends
  a native client rather than a web page, on SQLite and PostgreSQL first and
  MySQL/MariaDB once its driver exists, with staged edits, previewed SQL,
  vault-held credentials and read-only access to ULTRA OS apps' own stores.
  Lists what UltraDatabase must gain first - schema introspection, column
  metadata, cursors, cancellation, identifier quoting, every result of a
  batch, exact `numeric` values - and two framework additions: in-place
  editing in `UltraCanvasListView`, and tabular export over a row source.
