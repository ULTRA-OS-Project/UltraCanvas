- **UltraCrypt: SHA-1 as a plain digest is refused in all three ways to one,
  not in one of them.** `UltraCrypt_Hash` has always refused
  `UltraCryptHashAlgorithm::SHA1` unless `UltraCryptHashOptions::allowLegacy`
  is set - SHA-1 is collision-broken and is there for HMAC-SHA-1 (TOTP) and
  two foreign file formats. `UltraCrypt_HashFile` and the streaming
  `UltraCryptHasher` took no options and computed it for anyone, so the
  refusal was passed by hashing a file or feeding the same bytes in pieces.
  - Both take `UltraCryptHashOptions` now (last parameter, defaulted - code
    that hashes with SHA-256 or SHA-512 is unchanged). `UltraCrypt_HashFile`
    returns `NotSupported` for SHA-1 without `allowLegacy`, before the file
    is opened; a `UltraCryptHasher` made that way reports `IsValid() ==
    false`, takes no data and returns an empty digest from `Final()`.
  - Nothing in the tree hashed a file or a stream with SHA-1; HMAC-SHA-1,
    PBKDF2-HMAC-SHA-1 with `allowLegacy` and the Office and OpenDocument
    readers are as they were. Code of yours that did must pass
    `allowLegacy`. `UltraCryptTests` covers the refusal and the digests in
    all three (`UltraCrypt_HashFile` had no test before).
