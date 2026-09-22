#### 2026-09-20 *0.1.1*
- **Account credentials are encrypted at rest.** UltraSocial's vault was a copy
  of UltraMail's original 0.1 format — secrets XOR-ed against a `vault.key`
  beside them, "better than plaintext" — and it stayed there when UltraMail
  moved to UltraVault. It is now a profile of the framework's
  `UltraVault::DeviceKeyVault` (framework 0.9.21): `ultrasocial.vault`,
  Argon2id-derived key and XChaCha20-Poly1305 via UltraCrypt, unlocked at
  start-up by a random passphrase in owner-only `device.key` so nothing
  prompts. An existing `vault.key` + `creds.dat` is carried into the new vault
  on the first start and the weak files are removed. Keys are
  `social.ultrasocial.<accountId>`. When the vault cannot be opened (a build
  without libsodium) the app says so on stderr and a sign-in reports that the
  credentials could not be saved, instead of writing them weakly.
  `UltraSocialCredentialVault.cpp` is gone; the header names the profile.
- The engine tests unlock the vault first and cover the migration of the old
  format (`Tests/UltraSocial/test_store_vault.cpp`).

#### 2026-08-31 *0.1.0*
- **UltraSocial keeps its own changelog from here.** Everything up to and
  including this version shipped as part of a framework release and is recorded
  in [`Docs/UltraCanvas/CHANGELOG.md`](../UltraCanvas/CHANGELOG.md) — nothing
  was rewritten or moved, so that history stays where it was published. From
  now on a change to the social-posting client (`Apps/UltraSocial`) is
  described here and carries this file's version, and UltraSocial no longer
  moves when the framework releases.
- A framework change UltraSocial needs still belongs in the framework
  changelog. Cross-reference it from here when a release depends on it; never
  describe one change in two files under two version numbers.

<!--
Version source of truth: the first line of this file, format
`#### YYYY-MM-DD *x.y.z*`, read by cmake/UltraCanvasVersion.cmake.
-->
