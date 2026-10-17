- **UltraCrypt 0.3.0: AES-CBC, for formats defined elsewhere.** Encrypted
  Office files, PDF's AESV2/AESV3 and OpenSSL's `enc` output are AES in CBC
  mode, and libsodium has no AES block cipher, so UltraCrypt now carries its
  own: `UltraCrypt_AesCbcEncrypt` / `UltraCrypt_AesCbcDecrypt` with a 128-,
  192- or 256-bit key, PKCS #7 or no padding, and a random IV generated when
  none is given (`core/UltraCrypt/UltraCryptAes.cpp`).
  - Constant-time: bitsliced, with the Boyar-Peralta S-box circuit, so no
    table is indexed by a secret byte. Portable software, independent of
    libsodium and of AES instructions.
  - Refused padding is the new `UltraCryptResultCode::DecryptionFailed`, with
    an empty output; the check does not branch on the plaintext.
  - Interop only. CBC has no integrity, so UltraCanvas's own data stays on
    XChaCha20-Poly1305, and a format that carries a MAC must have it checked
    before decrypting (`Docs/Modules/UltraCrypt/README.md` §5.9).
  - `UltraCryptTests` checks FIPS 197 App. C and SP 800-38A F.2 for all three
    key sizes, PKCS #7 lengths 0-80, and refused padding.
