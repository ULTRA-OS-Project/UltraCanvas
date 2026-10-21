- **UltraCrypt: AES-CTR and PBKDF2, for formats defined elsewhere.**
  `UltraCrypt_AesCtrEncrypt` / `UltraCrypt_AesCtrDecrypt` (AES-128/192/256,
  a 128-bit big-endian counter from the IV, any length, on the bitsliced
  constant-time AES the CBC mode uses) and `UltraCrypt_DeriveKeyPbkdf2`
  (PBKDF2-HMAC-SHA-256/512, the password's HMAC state keyed once). Like
  AES-CBC they are interop only - nothing UltraCanvas defines uses them -
  and are here for Matrix's room-key export files and attachments. Tested
  against SP 800-38A F.5, RFC 7914 and SHA-512 values.
