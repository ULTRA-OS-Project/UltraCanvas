// Plugins/Documents/UCDCryptoEnvelope.cpp
// Implementation of the UCD password-encryption envelope. See the header for
// the byte layout and the reasoning behind it.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "Plugins/Documents/UCDCryptoEnvelope.h"

#include "UltraCrypt/UltraCryptCore.h"

#include <cstring>

namespace UltraCanvas {
namespace UCDCrypto {
namespace {

constexpr size_t  kMagicSize    = 4;
constexpr size_t  kSaltSize     = 16;
constexpr size_t  kNonceSize    = 24;
constexpr size_t  kHeaderSize   = 56;
constexpr uint8_t kVersion      = 1;
constexpr uint8_t kKdfArgon2id  = 1;
constexpr uint8_t kAeadXChaCha  = 1;
const char kMagic[kMagicSize] = {'U', 'C', 'D', 'E'};

// Offsets within the envelope header.
constexpr size_t kOffVersion    = 4;
constexpr size_t kOffKdfId      = 5;
constexpr size_t kOffAeadId     = 6;
constexpr size_t kOffIterations = 8;
constexpr size_t kOffMemoryKiB  = 12;
constexpr size_t kOffSalt       = 16;
constexpr size_t kOffNonce      = 32;

// Guard rails on parsed cost parameters. A hostile file could otherwise ask for
// terabytes of memory; the tag check would reject it eventually, but only after
// the allocation attempt.
constexpr uint32_t kMaxMemoryKiB  = 4u * 1024u * 1024u;   // 4 GiB
constexpr uint32_t kMaxIterations = 64;

void PutUint32LE(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
}

uint32_t ReadUint32LE(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

} // namespace

size_t GetEnvelopeHeaderSize() {
    return kHeaderSize;
}

bool Seal(const std::vector<uint8_t>& plaintext,
          const std::string& password,
          const std::vector<uint8_t>& associatedData,
          std::vector<uint8_t>& output,
          std::string& error) {
    output.clear();
    error.clear();

    if (password.empty()) {
        error = "Cannot encrypt the document without a password.";
        return false;
    }
    if (!UltraCrypt_IsAvailable()) {
        error = "This build has no cryptography backend, so the document cannot "
                "be encrypted.";
        return false;
    }

    // Derive the key. The salt and cost parameters come back in `kdf` and are
    // written into the envelope so the document can be opened later.
    UltraCryptKdfParams kdf = UltraCrypt_RecommendedKdfParams();
    UltraCryptSecureBuffer passwordBuffer(password.data(), password.size());
    UltraCryptSecureBuffer key;
    auto derived = UltraCrypt_DeriveKeyFromPassword(passwordBuffer, kdf, key);
    if (!derived) {
        error = "Could not derive the encryption key: " + derived.message;
        return false;
    }

    // The header is authenticated, so it has to be final before sealing.
    UltraCryptAeadParams aead;    // defaults to XChaCha20-Poly1305
    aead.nonce.assign(kNonceSize, 0);
    auto nonceResult = UltraCrypt_RandomBytes(aead.nonce.data(), aead.nonce.size());
    if (!nonceResult) {
        error = "Could not generate a nonce: " + nonceResult.message;
        return false;
    }

    std::vector<uint8_t> header;
    header.reserve(kHeaderSize);
    header.insert(header.end(), kMagic, kMagic + kMagicSize);
    header.push_back(kVersion);
    header.push_back(kKdfArgon2id);
    header.push_back(kAeadXChaCha);
    header.push_back(0);                       // reserved
    PutUint32LE(header, kdf.iterations);
    PutUint32LE(header, kdf.memoryKiB);
    header.insert(header.end(), kdf.salt.begin(), kdf.salt.end());
    header.insert(header.end(), aead.nonce.begin(), aead.nonce.end());
    if (header.size() != kHeaderSize) {
        error = "Internal error building the encryption envelope.";
        return false;
    }

    // Authenticate the caller's header and our own alongside the body.
    aead.associatedData = associatedData;
    aead.associatedData.insert(aead.associatedData.end(),
                               header.begin(), header.end());

    std::vector<uint8_t> ciphertext;
    auto sealed = UltraCrypt_AeadSeal(key, aead, plaintext.data(),
                                      plaintext.size(), ciphertext);
    if (!sealed) {
        error = "Could not encrypt the document: " + sealed.message;
        return false;
    }

    output = std::move(header);
    output.insert(output.end(), ciphertext.begin(), ciphertext.end());
    return true;
}

bool Open(const std::vector<uint8_t>& envelope,
          const std::string& password,
          const std::vector<uint8_t>& associatedData,
          std::vector<uint8_t>& output,
          std::string& error) {
    output.clear();
    error.clear();

    if (password.empty()) {
        error = "This document is password-protected; a password is required.";
        return false;
    }
    if (!UltraCrypt_IsAvailable()) {
        error = "This build has no cryptography backend, so the document cannot "
                "be decrypted.";
        return false;
    }
    if (envelope.size() <= kHeaderSize) {
        error = "The document is corrupt (the encrypted body is truncated).";
        return false;
    }
    if (std::memcmp(envelope.data(), kMagic, kMagicSize) != 0) {
        error = "The document is corrupt (unrecognised encryption envelope).";
        return false;
    }
    if (envelope[kOffVersion] != kVersion) {
        error = "This document uses a newer encryption format than this version "
                "of UltraCanvas supports.";
        return false;
    }
    if (envelope[kOffKdfId] != kKdfArgon2id ||
        envelope[kOffAeadId] != kAeadXChaCha) {
        error = "This document uses an unsupported key-derivation or encryption "
                "algorithm.";
        return false;
    }

    UltraCryptKdfParams kdf;
    kdf.algorithm    = UltraCryptKdfAlgorithm::Argon2id;
    kdf.iterations   = ReadUint32LE(envelope.data() + kOffIterations);
    kdf.memoryKiB    = ReadUint32LE(envelope.data() + kOffMemoryKiB);
    kdf.parallelism  = 1;
    kdf.outputLength =
        UltraCrypt_GetKeySize(UltraCryptAeadAlgorithm::XChaCha20Poly1305);
    kdf.salt.assign(envelope.begin() + kOffSalt,
                    envelope.begin() + kOffSalt + kSaltSize);

    if (kdf.iterations == 0 || kdf.iterations > kMaxIterations ||
        kdf.memoryKiB == 0 || kdf.memoryKiB > kMaxMemoryKiB) {
        error = "The document declares unreasonable key-derivation parameters "
                "and was not opened.";
        return false;
    }

    UltraCryptSecureBuffer passwordBuffer(password.data(), password.size());
    UltraCryptSecureBuffer key;
    auto derived = UltraCrypt_DeriveKeyFromPassword(passwordBuffer, kdf, key);
    if (!derived) {
        error = "Could not derive the decryption key: " + derived.message;
        return false;
    }

    UltraCryptAeadParams aead;
    aead.algorithm = UltraCryptAeadAlgorithm::XChaCha20Poly1305;
    aead.nonce.assign(envelope.begin() + kOffNonce,
                      envelope.begin() + kOffNonce + kNonceSize);
    aead.associatedData = associatedData;
    aead.associatedData.insert(aead.associatedData.end(),
                               envelope.begin(), envelope.begin() + kHeaderSize);

    UltraCryptSecureBuffer plaintext;
    auto opened = UltraCrypt_AeadOpen(key, aead,
                                      envelope.data() + kHeaderSize,
                                      envelope.size() - kHeaderSize,
                                      plaintext);
    if (!opened) {
        // One undifferentiated message on purpose.
        error = "Could not decrypt the document — the password may be incorrect, "
                "or the file may have been altered.";
        return false;
    }

    output.assign(plaintext.Data(), plaintext.Data() + plaintext.GetSize());
    return true;
}

} // namespace UCDCrypto
} // namespace UltraCanvas
