- **UltraCrypt: X25519 key agreement.** `UltraCrypt_KeyExchange` derives the
  secret two parties share from one's secret and the other's public key,
  refusing a low-order public key; `UltraCrypt_SignaturePublicKeyToKeyExchange`
  and `UltraCrypt_SignatureSecretKeyToKeyExchange` turn an Ed25519 identity
  into its X25519 form, so one key pair signs and encrypts;
  `UltraCrypt_GenerateKeyExchangeKeyPair` makes an ephemeral pair. The
  module had signatures and AEAD but nothing between them, which end-to-end
  encryption needs (ChatCube Direct). Tested against the RFC 7748 vector.
