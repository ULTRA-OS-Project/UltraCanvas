- **ODF 1.4's password encryption opens: the whole package in one entry,
  AES-256-GCM, Argon2id with four lanes** - what LibreOffice 24.2 writes when it
  saves ODF 1.4 in experimental mode. `UltraCanvasOdfEncryption` decrypts the
  `encrypted-package` (the 12-byte IV in front, the tag at the end) and hands
  back the package inside; every reader that takes a password - `.odt`, `.ods`,
  `.odp` - opens it. A wrong password, and a changed byte, fail the GCM tag. An
  Argon2id cost past 1 GiB, 64 passes or 64 lanes is refused before any memory
  is taken.
  - UltraCrypt: `UltraCrypt_AesGcmDecrypt`, AES-GCM on the module's own
    constant-time AES with a masked GHASH, so it works on CPUs without AES
    instructions (libsodium's AES-GCM needs them); checked against the GCM
    specification's test cases and against what libsodium seals.
  - UltraCrypt: Argon2id with more than one lane (`UltraCryptKdfParams::parallelism`),
    an RFC 9106 implementation on libsodium's BLAKE2b (libsodium computes one
    lane), checked against libargon2 for 2, 3 and 4 lanes and LibreOffice's
    64 MiB. Before, any parallelism but 1 was refused.
  - `Tests/fixtures/make-odf-encrypted-fixtures.py` writes `odf14-encrypted.odt`,
    `.ods` and `.odp` from a LibreOffice profile set to ODF 1.4 and experimental
    mode before it starts; `OdfEncryptionTest` reads them.
