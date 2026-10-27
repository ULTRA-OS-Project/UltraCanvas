// Apps/UltraStoreAdmin/core/KeyVault.h
// The signing key's seed, sealed with a passphrase: Argon2id derives the
// key, XChaCha20-Poly1305 seals the seed with the parameters in the
// authenticated header (UltraPassword's vault scheme, through UltraCrypt).
// The sealed bytes go into the admin database; the seed exists in memory
// only for the moment of signing. A second machine means a second key, so
// nothing here copies a seed anywhere.
//
// Layout of the sealed bytes, all authenticated:
//   "USASEED1" | iterations u32 LE | memoryKiB u32 LE | salt 16 | nonce 24 | ciphertext (32 + 16 tag)
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraStoreTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace UltraStoreAdmin {

using UltraStore::ErrorCode;
using UltraStore::Result;

// Argon2id cost. The default is UltraCrypt's recommendation (3 passes,
// 64 MiB); a test uses the smallest the backend accepts.
struct KeyCost {
    uint32_t iterations = 3;
    uint32_t memoryKiB = 64 * 1024;
    static KeyCost Recommended() { return {}; }
    static KeyCost Fast() { return {1, 8 * 1024}; }
};

struct SealedSeed {
    uint32_t iterations = 0;
    uint32_t memoryKiB = 0;
    std::vector<uint8_t> salt;
    std::vector<uint8_t> nonce;
    std::vector<uint8_t> cipher;

    std::vector<uint8_t> Bytes() const;
    static bool Parse(const std::vector<uint8_t>& bytes, SealedSeed& out);
};

// A fresh Ed25519 pair, the seed sealed. CryptoUnavailable without a backend.
struct NewKey {
    std::string publicKeyHex;
    SealedSeed sealed;
};
Result<NewKey> NewSigningKey(const std::string& passphrase, KeyCost cost = KeyCost::Recommended());

Result<SealedSeed> SealSeed(const std::vector<uint8_t>& seed, const std::string& passphrase, KeyCost cost = KeyCost::Recommended());

// The seed again, or SignatureInvalid when the passphrase is wrong or the
// bytes were altered (one answer for both, as the AEAD gives).
Result<std::vector<uint8_t>> OpenSeed(const SealedSeed& sealed, const std::string& passphrase);

} // namespace UltraStoreAdmin
