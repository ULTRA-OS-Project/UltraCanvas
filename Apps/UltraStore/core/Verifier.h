// Apps/UltraStore/core/Verifier.h
// What the store trusts: a catalog whose detached Ed25519 signature checks
// against one of the public keys built into the store (an active key and a
// spare, so a key can be rotated without stranding old clients), and a
// package whose SHA-256 is the catalog's. All of it through UltraCrypt.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraStoreTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace UltraStore {

// The 32-byte Ed25519 public keys a catalog may be signed with.
using PublicKeys = std::vector<std::vector<uint8_t>>;

// The exact bytes of catalog.json against the bytes of catalog.json.sig (a
// 64-byte signature, raw or as 128 hex characters). Success names nothing;
// SignatureInvalid when no key accepts it, InvalidArgument for a signature or
// key of the wrong size, CryptoUnavailable without a backend.
Result<void> VerifyCatalogSignature(const std::string& catalogBytes,
                                    const std::vector<uint8_t>& signature,
                                    const PublicKeys& keys);

// A signature file's bytes: raw 64 bytes, or hex (whitespace around it
// ignored). false when it is neither.
bool ParseSignatureFile(const std::string& fileBytes, std::vector<uint8_t>& outSignature);

// Lower-case hex SHA-256 of bytes, and of a file (UTF-8 path) read in
// pieces; "" when the file cannot be read or UltraCrypt has no backend.
std::string Sha256Hex(const std::string& bytes);
std::string Sha256HexOfFile(const std::string& path);

// A downloaded package against its catalog entry: equal hashes, in constant
// time on the hex strings' bytes.
bool PackageHashMatches(const std::string& fileSha256Hex, const std::string& catalogSha256Hex);

// ===== SIGNING - the admin application and the tests =====
// A fresh signing identity: the 32-byte seed (what the admin keeps, in the
// vault) and its public key. The seed alone rebuilds the pair.
Result<void> GenerateSigningKey(std::vector<uint8_t>& outSeed, std::vector<uint8_t>& outPublicKey);

// The public key of a seed.
Result<std::vector<uint8_t>> PublicKeyOfSeed(const std::vector<uint8_t>& seed);

// The detached signature over the exact bytes of a catalog, 64 bytes.
Result<std::vector<uint8_t>> SignCatalog(const std::string& catalogBytes, const std::vector<uint8_t>& seed);

} // namespace UltraStore
