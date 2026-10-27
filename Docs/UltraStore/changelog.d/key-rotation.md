- **Key rotation through the catalog (schema 2).** The catalog carries
  `trusted_keys`, the public keys that may sign the catalogs after it;
  `CatalogClient` keeps the last accepted catalog's list in its cache and
  verifies the next one against the compiled-in keys, the ones added in the
  settings and that list (`KeysTrustedNow()`). A new key is announced by a
  catalog the old key signed and accepted from then on, before the store
  binary that carries it ships; a key no accepted catalog announced is
  refused whatever the catalog it signs claims, a catalog cannot drop a
  compiled-in key, and a schema 1 catalog announces nothing. The Settings
  page says how many keys the catalog announces.
