- **UltraStore updates itself.** The store is installed like any app of its
  own catalog (`org.ultraos.ultrastore`) and updated by the same engine; the
  version a running store was started from is never removed from under it
  and goes at the next start. A catalog too new for the running store
  (`SchemaTooNew`) starts the update by itself, Settings has *Update
  UltraStore*, and after an update the window offers to restart into the
  new version. A store that was not installed into the store folder says
  so and is updated the way it came.
- **`ultrastore-setup`, the first installer.** A console program on the
  store's core: it fetches the store's own signed catalog, installs
  UltraStore with its runtime into the per-user store folder, writes the
  application-menu entry and starts the store. About 3 MB in a static
  Linux build.
- **The store's own catalog, `ultrastore.json`.** The store's entry and
  what it runs on, beside `catalog.json`, signed with the same keys, at a
  schema that stays at 2 so that every store reads it. `ultrastore
  self-update` installs from it, `ultrastore self-catalog` cuts it out of
  a catalog.
- `ultrastore update`, `remove` and `gc` run from an installed store leave
  the version they run from; removing the running store answers `InUse`
  and removes nothing.
