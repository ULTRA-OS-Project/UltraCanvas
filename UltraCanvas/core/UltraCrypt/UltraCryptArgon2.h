// core/UltraCrypt/UltraCryptArgon2.h
// Internal to UltraCrypt: Argon2id (RFC 9106) with any number of lanes, for
// UltraCrypt_DeriveKeyFromPassword when the parameters ask for more than one -
// libsodium's crypto_pwhash fixes parallelism at 1. Not a public header.
// Version: 1.0.0
// Last Modified: 2026-11-18
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstddef>
#include <cstdint>

namespace UltraCryptInternal {

// Argon2id, version 0x13, no secret and no associated data - what libargon2's
// argon2id_hash_raw computes. `memoryKiB` is m (1 KiB blocks), `passes` t,
// `lanes` p; out gets `outLength` bytes (4 or more). False when the memory
// cannot be allocated or a parameter is out of the RFC's range. Needs
// libsodium (BLAKE2b); false without it. Lanes are filled one after the
// other: Argon2's lanes are independent within a slice, so the result is the
// same as a parallel run's.
bool Argon2idRaw(const uint8_t* password, size_t passwordLength,
                 const uint8_t* salt, size_t saltLength,
                 uint32_t passes, uint32_t memoryKiB, uint32_t lanes,
                 uint8_t* out, size_t outLength);

} // namespace UltraCryptInternal
