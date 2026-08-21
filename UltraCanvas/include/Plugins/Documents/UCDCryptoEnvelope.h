// include/Plugins/Documents/UCDCryptoEnvelope.h
// Password-based encryption envelope for the UCD v1 document format.
//
// Split out of UltraCanvasDocument so the cryptographic path can be compiled
// and tested on its own: it depends only on UltraCrypt and the standard
// library, never on the document object model.
//
// Cryptography goes through UltraCrypt, never a vendored library directly, and
// there is no unauthenticated or "no backend" fallback path — a caller either
// gets a sealed envelope or an error.
//
// Envelope layout (all integers little-endian):
//
//   offset size field
//   0      4    magic "UCDE"
//   4      1    envelope version (1)
//   5      1    KDF id       (1 = Argon2id)
//   6      1    AEAD id      (1 = XChaCha20-Poly1305)
//   7      1    reserved (0)
//   8      4    Argon2id passes
//   12     4    Argon2id memory cost, KiB
//   16     16   Argon2id salt
//   32     24   AEAD nonce
//   56     ...  ciphertext || 16-byte tag
//
// Cost parameters travel with the data rather than being assumed, so raising
// the recommended Argon2id costs later cannot orphan existing documents. The
// entire 56-byte header is authenticated as associated data together with the
// caller's own header bytes, so tampering with the declared costs, the salt or
// the nonce is detected rather than obeyed.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once
#ifndef UCDCRYPTOENVELOPE_H
#define UCDCRYPTOENVELOPE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace UltraCanvas {
namespace UCDCrypto {

// Size of the fixed envelope header that precedes the ciphertext.
size_t GetEnvelopeHeaderSize();

// Seals `plaintext` under a key derived from `password`. `associatedData` is
// authenticated but not encrypted — pass the document's file header so editing
// it invalidates the tag. On failure returns false and sets `error`.
bool Seal(const std::vector<uint8_t>& plaintext,
          const std::string& password,
          const std::vector<uint8_t>& associatedData,
          std::vector<uint8_t>& output,
          std::string& error);

// Opens an envelope produced by Seal. Returns false and sets `error` if the
// password is wrong, the ciphertext or associated data was altered, the
// envelope is malformed, or the declared cost parameters are unreasonable.
//
// A wrong password and a modified file deliberately produce the same message:
// distinguishing them would tell an attacker which of the two they achieved.
bool Open(const std::vector<uint8_t>& envelope,
          const std::string& password,
          const std::vector<uint8_t>& associatedData,
          std::vector<uint8_t>& output,
          std::string& error);

} // namespace UCDCrypto
} // namespace UltraCanvas

#endif // UCDCRYPTOENVELOPE_H
