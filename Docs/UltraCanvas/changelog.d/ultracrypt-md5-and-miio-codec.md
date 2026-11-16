- **UltraCrypt: MD5, legacy-gated.** `UltraCryptHashAlgorithm::MD5`, vendored
  (RFC 1321) beside SHA-1 and checked against the RFC's test suite: a plain
  digest only with `UltraCryptHashOptions::allowLegacy`, refused outright by
  HMAC, HKDF and PBKDF2. For foreign protocols that fix it - Xiaomi miIO
  keys its AES with MD5 and checksums every packet with it.
- **SmartHome: the Xiaomi miIO codec** (`SmartHome/protocols/Xiaomi/miio`),
  the first step of the vendor-backend plan
  (`Docs/Research/SmartHomeVendorBackendsPlan.md`): `MiioPacket` frames,
  checksums and encrypts packets (AES-128-CBC keyed from the device token),
  `MiioRpc` builds and reads the JSON-RPC inside, MIoT-spec property and
  action calls included. `tests/MiioCodecTest.cpp` matches, byte for byte,
  a packet built independently with openssl and md5sum. No socket and no
  backend yet - those are steps 1.4 and 1.5 of the plan.
