#### 2026-10-28 *0.1.4*
- **`sale sync`** and the window's *Sync sales*: the paid orders pulled
  from the payment service and recorded once each in the ledger, with the
  partners' credits; the `payment.url`, `payment.licence_key` and
  `payment.token` settings, the first two published in the catalog so
  every store finds the service and its licence key.
- **Publish warns about a restricted package on an app with a trial**:
  *Try* installs the same package and would fail without a licence, so
  the package of an app with a trial stays public.

#### 2026-10-27 *0.1.3*
- The buttons that open a dialog say what they do without a trailing
  ellipsis: *Choose*, *Export .pub*, *Add release*, *Add package*, *Add
  screenshot*, *QR*, *Join QR*.

#### 2026-10-27 *0.1.2*
- **Affiliates and codes.** Partners with a share of each sale made with
  their codes; two tiers and no more: a partner recruited by another names
  the recruiter, who earns a recruit share of the recruit's sales, once,
  one level up. Codes (a partner's, or a plain discount) with what they take
  off, the apps and the validity, in the catalog with the next publish and
  a landing page each under `go/`; QR codes as SVG for a code's link and a
  partner's join link. A sales ledger recorded by hand until the payment
  service reports sales, with what each sale credits, and a report of what
  every partner is owed. `affiliate`, `code` and `sale` on the command
  line; the *Affiliates and codes* page in the window.
- `app import` reads the backend's own export again: a package that has no
  URL yet (its file is in the package store) no longer refuses the import.

#### 2026-10-27 *0.1.1*
- **The window.** `ultrastoreadmin` started plainly (or with `--store <dir>`
  alone) opens the admin window: the store's pages and every app, runtime
  and library in a sidebar; the form of one with every setting the catalog
  carries - identity, visibility and lifecycle, age rating, the monetary
  model with price, currency and trial, the free update period, the texts
  per language with *Translate the missing languages* through UltraAI and
  the machine ones marked, the icon and screenshots copied into the media
  folder, the releases newest first with their packages and inline editors
  for a new release and a new package (the build into the package store,
  the entry suggested from the archive); Publish with *Check* listing the
  warnings and the report; the keys with export and retire; the settings
  and the publishers. The commands moved to `cli/Commands.cpp`
  (`Cli::Run`), the store's folders and the media import to the core
  (`AdminStore`, `ImportMedia`, `DefaultAdminStoreDir`). One copy per
  user; the app icon and a desktop file.
- **The catalog announces the active keys.** Every publish writes a schema 2
  catalog with `trusted_keys`, every key not retired: `key new`, one publish
  with the old key, then publishing with the new one rotates the signing
  key without a new store binary; `key retire` drops a key from the list.
- **Upload to the host.** `ultrastoreadmin upload <outdir>` and the
  Publish page's *Upload* send the publish folder to the server over what
  UltraNet speaks - SFTP with a password or a key file, FTPS, FTP, or
  HTTPS with one PUT per file - what changed since the last upload to that
  server only (`upload-state.json` keeps each file's hash per target), the
  packages and the media first, the signature, and `catalog.json` last, so
  a store fetching in between sees the old catalog whole or the new one
  whole; an upload that broke off goes on from where it stopped. Settings
  `upload.url`, `upload.user`, `upload.key_file`; the password is never
  kept.

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
