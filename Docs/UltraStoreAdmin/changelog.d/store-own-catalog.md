- **Publish writes the store's own catalog.** When the catalog lists
  UltraStore itself, `ultrastore.json` and its signature are written beside
  `catalog.json`: the store's entry and the components it depends on, at
  schema 2, signed with the same key. The setup program installs the store
  from it and an older store updates itself from it. The upload sends it
  after the packages and before the catalog; a catalog without the store
  is warned about.
