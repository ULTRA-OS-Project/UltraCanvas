#### 2026-10-27 *0.1.0*
- **UltraStoreAdmin: the store's admin backend, core and command line.** The
  apps, runtimes and libraries with every setting the catalog carries
  (`ultrastoreadmin app import <component.json>`, `app set`, `app text`,
  `release add`, `package add`), kept in an UltraDatabase file as the
  catalog's own component objects, so what the backend keeps is by
  construction what the store reads; the publishers; the files behind the
  packages copied into a package store under their SHA-256, size and hash
  measured, the entry to start suggested from the archive's listing; the
  signing key generated on the machine and its seed sealed with a passphrase
  (Argon2id, XChaCha20-Poly1305) in the database, the public half exported
  for `Apps/UltraStore/keys/`; the texts translated with UltraAI into every
  official language and marked `machine_translated`; the catalog built with
  the next sequence, the packages laid out as `packages/` (free) and
  `restricted/` (paid, the access scheme), the media beside it, signed, and
  verified with the store's own verifier before it is called published.
  Design: `Docs/UltraStoreAdmin/README.md`.
