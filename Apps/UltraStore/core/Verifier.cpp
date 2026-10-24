// Apps/UltraStore/core/Verifier.cpp
// Signatures and hashes (Verifier.h), on UltraCrypt.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "Verifier.h"

#include "UltraCrypt/UltraCryptCore.h"

#include <cctype>

namespace UltraStore {

namespace {

const auto kAlgorithm = UltraCryptSignatureAlgorithm::Ed25519;

std::string Trim(const std::string& s) {
    const size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

Result<void> CryptoResult(const UltraCryptResult& r, ErrorCode onFailure, const std::string& message) {
    if (r) return Result<void>::Success();
    if (r.code == UltraCryptResultCode::BackendUnavailable) {
        return Result<void>::Failure(ErrorCode::CryptoUnavailable,
                                     "UltraCrypt has no cryptographic backend in this build", r.message);
    }
    return Result<void>::Failure(onFailure, message, r.message);
}

} // namespace

Result<void> VerifyCatalogSignature(const std::string& catalogBytes,
                                    const std::vector<uint8_t>& signature,
                                    const PublicKeys& keys) {
    if (!UltraCrypt_IsAvailable()) {
        return Result<void>::Failure(ErrorCode::CryptoUnavailable,
                                     "UltraCrypt has no cryptographic backend in this build");
    }
    if (keys.empty()) {
        return Result<void>::Failure(ErrorCode::InvalidArgument, "No public key to check the catalog against");
    }
    if (signature.size() != UltraCrypt_GetSignatureSize(kAlgorithm)) {
        return Result<void>::Failure(ErrorCode::InvalidArgument, "The catalog signature has the wrong size",
                                     std::to_string(signature.size()) + " bytes");
    }
    std::string lastDetail;
    for (const auto& key : keys) {
        if (key.size() != UltraCrypt_GetSignaturePublicKeySize(kAlgorithm)) {
            return Result<void>::Failure(ErrorCode::InvalidArgument, "A public key has the wrong size",
                                         std::to_string(key.size()) + " bytes");
        }
        const UltraCryptResult r = UltraCrypt_Verify(kAlgorithm, key, catalogBytes.data(),
                                                     catalogBytes.size(), signature);
        if (r) return Result<void>::Success();
        if (r.code == UltraCryptResultCode::BackendUnavailable) return CryptoResult(r, ErrorCode::Internal, "");
        lastDetail = r.message;
    }
    return Result<void>::Failure(ErrorCode::SignatureInvalid,
                                 "The catalog's signature does not match any key this UltraStore trusts",
                                 lastDetail);
}

bool ParseSignatureFile(const std::string& fileBytes, std::vector<uint8_t>& outSignature) {
    const size_t raw = UltraCrypt_GetSignatureSize(kAlgorithm);
    if (fileBytes.size() == raw) {
        outSignature.assign(fileBytes.begin(), fileBytes.end());
        return true;
    }
    const std::string trimmed = Trim(fileBytes);
    std::vector<uint8_t> bytes;
    if (trimmed.size() == raw * 2 && FromHex(trimmed, bytes)) {
        outSignature = std::move(bytes);
        return true;
    }
    return false;
}

std::string Sha256Hex(const std::string& bytes) {
    std::vector<uint8_t> digest;
    if (!UltraCrypt_Hash(UltraCryptHashAlgorithm::SHA256, bytes.data(), bytes.size(), digest)) return {};
    return UltraCrypt_ToHex(digest);
}

std::string Sha256HexOfFile(const std::string& path) {
    std::vector<uint8_t> digest;
    if (!UltraCrypt_HashFile(UltraCryptHashAlgorithm::SHA256, path, digest)) return {};
    return UltraCrypt_ToHex(digest);
}

bool PackageHashMatches(const std::string& fileSha256Hex, const std::string& catalogSha256Hex) {
    std::string a, b;
    for (char c : fileSha256Hex) a.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    for (char c : catalogSha256Hex) b.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    if (a.size() != 64 || b.size() != 64) return false;
    return UltraCrypt_ConstantTimeEquals(a.data(), a.size(), b.data(), b.size());
}

Result<void> GenerateSigningKey(std::vector<uint8_t>& outSeed, std::vector<uint8_t>& outPublicKey) {
    UltraCryptSecureBuffer seed;
    UltraCryptResult r = UltraCrypt_RandomSecureBuffer(UltraCrypt_GetSignatureSeedSize(kAlgorithm), seed);
    if (!r) return CryptoResult(r, ErrorCode::Internal, "No random bytes for a new key");
    std::vector<uint8_t> pk;
    UltraCryptSecureBuffer sk;
    r = UltraCrypt_SignatureKeyPairFromSeed(kAlgorithm, seed, pk, sk);
    if (!r) return CryptoResult(r, ErrorCode::Internal, "The key pair could not be made");
    outSeed.assign(seed.Data(), seed.Data() + seed.GetSize());
    outPublicKey = std::move(pk);
    return Result<void>::Success();
}

Result<std::vector<uint8_t>> PublicKeyOfSeed(const std::vector<uint8_t>& seed) {
    if (seed.size() != UltraCrypt_GetSignatureSeedSize(kAlgorithm)) {
        return Result<std::vector<uint8_t>>::Failure(ErrorCode::InvalidArgument, "The seed must be 32 bytes");
    }
    UltraCryptSecureBuffer seedBuffer(seed.data(), seed.size());
    std::vector<uint8_t> pk;
    UltraCryptSecureBuffer sk;
    const UltraCryptResult r = UltraCrypt_SignatureKeyPairFromSeed(kAlgorithm, seedBuffer, pk, sk);
    if (!r) {
        const Result<void> c = CryptoResult(r, ErrorCode::Internal, "The key pair could not be made");
        return Result<std::vector<uint8_t>>::Failure(c.error.code, c.error.message, c.error.detail);
    }
    return Result<std::vector<uint8_t>>::Success(std::move(pk));
}

Result<std::vector<uint8_t>> SignCatalog(const std::string& catalogBytes, const std::vector<uint8_t>& seed) {
    if (seed.size() != UltraCrypt_GetSignatureSeedSize(kAlgorithm)) {
        return Result<std::vector<uint8_t>>::Failure(ErrorCode::InvalidArgument, "The seed must be 32 bytes");
    }
    UltraCryptSecureBuffer seedBuffer(seed.data(), seed.size());
    std::vector<uint8_t> pk;
    UltraCryptSecureBuffer sk;
    UltraCryptResult r = UltraCrypt_SignatureKeyPairFromSeed(kAlgorithm, seedBuffer, pk, sk);
    if (!r) {
        const Result<void> c = CryptoResult(r, ErrorCode::Internal, "The key pair could not be made");
        return Result<std::vector<uint8_t>>::Failure(c.error.code, c.error.message, c.error.detail);
    }
    std::vector<uint8_t> signature;
    r = UltraCrypt_Sign(kAlgorithm, sk, catalogBytes.data(), catalogBytes.size(), signature);
    if (!r) {
        const Result<void> c = CryptoResult(r, ErrorCode::Internal, "The catalog could not be signed");
        return Result<std::vector<uint8_t>>::Failure(c.error.code, c.error.message, c.error.detail);
    }
    return Result<std::vector<uint8_t>>::Success(std::move(signature));
}

} // namespace UltraStore
