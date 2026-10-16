- **UCD documents can be password-protected (UCD 2.2).** The container
  (`UltraCanvasUCDContainer.h`) now does what the spec's key source `1`
  describes, through UltraCrypt: `Writer::SetPassword` derives a key with
  Argon2id (a fresh salt each write), puts a `SECU` record after the `INDX`
  and seals every other section with XChaCha20-Poly1305 - a fresh nonce,
  the section's header as associated data - with `hidePreview` marking the
  document `PRIVATE` and leaving the thumbnail out. `Reader::NeedsPassword`
  / `Unlock` open it: the key is checked against the `SECU`'s check tag, so
  a wrong password (`UCD::kWrongPassword`) is told apart from a damaged
  file, and costs over 64 passes or 4 GiB are refused before anything is
  derived. Without libsodium it fails closed (`UCD::EncryptionAvailable()`).
  The spec gains the `SECU` layout and the encrypted-section layout (4.3.1,
  4.3.2; *Changes in 2.2*), and writers put down version 2.2.
  - `UCRasterDocument` carries a password for its `.ucd`:
    `SetUCDPassword(password, hidePreview)`, `HasUCDPassword`,
    `GetUCDHidesPreview`, `LoadUCD(path, error, password)` and
    `IsPasswordProtected(path)`. A document opened with its password saves
    protected again; loading another file clears it.
