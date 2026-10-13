- **UltraCrypt 0.2.0: Ed25519 signatures.** `UltraCrypt_Sign` and
  `UltraCrypt_Verify` (and `UltraCrypt_SignFile` / `UltraCrypt_VerifyFile`
  over a file's bytes) give the framework its first public-key operation:
  deterministic 64-byte detached signatures under RFC 8032, checked with a
  32-byte public key and nothing else, so a signed catalog or update can be
  verified by a client that holds no secret. `UltraCrypt_GenerateSignatureKeyPair`
  draws a fresh pair, `UltraCrypt_SignatureKeyPairFromSeed` rebuilds one from
  a 32-byte seed (a seed in a vault is the whole signing identity) and
  `UltraCrypt_GetSignaturePublicKey` reads the public half out of a secret
  key. A wrong-length key or signature is refused with `InvalidKeySize` /
  `InvalidArgument` before anything is checked; a mismatch is the new
  `UltraCryptResultCode::SignatureInvalid`, one code whether the message, the
  signature or the key is off. Tested against the RFC 8032 §7.1 vectors plus
  a flipped message bit, a flipped signature bit, a wrong key and a file with
  a byte appended. The first consumer is UltraStore's signed catalog.
