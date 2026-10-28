- **The relay's sign-in checks the ID token through UltraCrypt.** Its RS256 /
  ES256 signature (`UltraCryptPublicKey.h`) and the sign-in's random state
  and nonce (`UltraCrypt_RandomBytes`) are UltraCrypt's; the relay calls no
  OpenSSL itself and is built with its sign-in where UltraCrypt has its
  OpenSSL 3 backend. The test still signs its tokens with OpenSSL, a signer
  independent of the code it checks.
