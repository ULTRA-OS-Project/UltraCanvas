- **Password-protected OpenDocument files open: `.odt`, `.ods` and `.odp`.**
  `UltraCanvasOdfEncryption.h` (new) reads what LibreOffice and
  OpenOffice.org 3.2+ write with *Save with password*: each package entry
  deflated, then AES-256-CBC keyed by PBKDF2-HMAC-SHA-1 (100 000 rounds) from
  the SHA-256 of the UTF-8 password, checked by the SHA-256 of its first
  kilobyte. `DecryptOdfPackage` / `DecryptOdfPackageFile` rebuild an ordinary
  package in memory; `CheckOdfPassword` checks a password with one key
  derivation; the statuses are `OfficeDecryptStatus`. Blowfish (OpenOffice.org
  1 to 3.1), AES-GCM, Argon2id and ODF 1.4's whole-package encryption are
  refused, each by name. There is no writer.
  - `UCWordDocumentIO::Load` / `LoadOdt`, `UltraCanvasSpreadsheet::LoadFromFile`
    / `LoadODS` and `ReadOdpPresentation` / `ReadOdpPresentationFromMemory`
    take the password; `UltraCanvasFileLoader::LoadTextDocument` and
    `ExtractText` pass it on. `CheckOfficePassword` answers for an
    OpenDocument file, so `RequestDocumentPassword` and `OpenTextDocument`
    prompt for a protected `.odt` or `.odp` as for a `.docx`.
  - UltraFiler's preview leaves a protected `.ods` its type glyph instead of a
    page of its ciphertext.
  - UltraCrypt: `UltraCrypt_DeriveKeyPbkdf2` takes `UltraCryptHashOptions`,
    and PBKDF2-HMAC-SHA-1 with `allowLegacy` (ODF names no other PRF), on the
    module's own SHA-1, each round two compressions of a block padded once.
    Checked against RFC 6070.
  - `UCZipInflateRaw` (`UltraCanvasZipPackage.h`): a raw deflate stream.
  - `Tests/OdfEncryptionTest.cpp` reads an `.odt`, an `.ods` and an `.odp` that
    LibreOffice 24.2 saved with a non-ASCII password
    (`make-odf-encrypted-fixtures.py`, through its UNO API): no, wrong and
    right passwords, each reader against the unencrypted original, a damaged
    byte, and the refused ciphers.
