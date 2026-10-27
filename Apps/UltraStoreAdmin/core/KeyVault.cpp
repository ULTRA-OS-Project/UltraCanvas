// Apps/UltraStoreAdmin/core/KeyVault.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "KeyVault.h"

#include "Verifier.h"

#include "UltraCrypt/UltraCryptCore.h"

#include <cstring>

namespace UltraStoreAdmin {

namespace {

constexpr char kMagic[8] = {'U', 'S', 'A', 'S', 'E', 'E', 'D', '1'};
constexpr size_t kSaltSize = 16;
constexpr size_t kNonceSize = 24;
constexpr size_t kHeaderSize = 8 + 4 + 4 + kSaltSize + kNonceSize;

void PutU32(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xff));
}

uint32_t GetU32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

std::vector<uint8_t> Header(const SealedSeed& s) {
    std::vector<uint8_t> h(kMagic, kMagic + 8);
    PutU32(h, s.iterations);
    PutU32(h, s.memoryKiB);
    h.insert(h.end(), s.salt.begin(), s.salt.end());
    h.insert(h.end(), s.nonce.begin(), s.nonce.end());
    return h;
}

Result<UltraCryptSecureBuffer> DeriveKey(const std::string& passphrase, UltraCryptKdfParams& kdf) {
    using R = Result<UltraCryptSecureBuffer>;
    if (passphrase.empty()) return R::Failure(ErrorCode::InvalidArgument, "The key needs a passphrase");
    std::string copy = passphrase;
    const UltraCryptSecureBuffer password = UltraCryptSecureBuffer::AdoptString(copy);
    kdf.algorithm = UltraCryptKdfAlgorithm::Argon2id;
    kdf.parallelism = 1;
    kdf.outputLength = UltraCrypt_GetKeySize(UltraCryptAeadAlgorithm::XChaCha20Poly1305);
    UltraCryptSecureBuffer key;
    const UltraCryptResult derived = UltraCrypt_DeriveKeyFromPassword(password, kdf, key);
    if (!derived) return R::Failure(ErrorCode::Internal, "The key could not be derived from the passphrase", derived.message);
    key.LockPages();
    return R::Succeed(std::move(key));
}

} // namespace

std::vector<uint8_t> SealedSeed::Bytes() const {
    std::vector<uint8_t> out = Header(*this);
    out.insert(out.end(), cipher.begin(), cipher.end());
    return out;
}

bool SealedSeed::Parse(const std::vector<uint8_t>& bytes, SealedSeed& out) {
    if (bytes.size() < kHeaderSize + 16) return false;
    if (std::memcmp(bytes.data(), kMagic, 8) != 0) return false;
    out.iterations = GetU32(bytes.data() + 8);
    out.memoryKiB = GetU32(bytes.data() + 12);
    out.salt.assign(bytes.begin() + 16, bytes.begin() + 16 + kSaltSize);
    out.nonce.assign(bytes.begin() + 16 + kSaltSize, bytes.begin() + kHeaderSize);
    out.cipher.assign(bytes.begin() + kHeaderSize, bytes.end());
    return out.iterations > 0 && out.memoryKiB > 0;
}

Result<SealedSeed> SealSeed(const std::vector<uint8_t>& seed, const std::string& passphrase, KeyCost cost) {
    using R = Result<SealedSeed>;
    if (!UltraCrypt_IsAvailable()) return R::Failure(ErrorCode::CryptoUnavailable, "This build has no cryptography backend");
    if (seed.size() != 32) return R::Failure(ErrorCode::InvalidArgument, "A seed is 32 bytes");
    UltraCryptKdfParams kdf;
    kdf.iterations = cost.iterations;
    kdf.memoryKiB = cost.memoryKiB;
    kdf.salt.clear();   // a fresh salt
    auto key = DeriveKey(passphrase, kdf);
    if (!key) return R::Failure(key.error.code, key.error.message, key.error.detail);
    SealedSeed sealed;
    sealed.iterations = kdf.iterations;
    sealed.memoryKiB = kdf.memoryKiB;
    sealed.salt = kdf.salt;
    UltraCryptAeadParams params;
    params.algorithm = UltraCryptAeadAlgorithm::XChaCha20Poly1305;
    const UltraCryptResult nonce = UltraCrypt_RandomBytes(params.nonce, kNonceSize);
    if (!nonce) return R::Failure(ErrorCode::Internal, "No random bytes for the nonce", nonce.message);
    sealed.nonce = params.nonce;
    params.associatedData = Header(sealed);
    const UltraCryptResult r = UltraCrypt_AeadSeal(key.Value(), params, seed.data(), seed.size(), sealed.cipher);
    if (!r) return R::Failure(ErrorCode::Internal, "The seed could not be sealed", r.message);
    return R::Succeed(std::move(sealed));
}

Result<std::vector<uint8_t>> OpenSeed(const SealedSeed& sealed, const std::string& passphrase) {
    using R = Result<std::vector<uint8_t>>;
    if (!UltraCrypt_IsAvailable()) return R::Failure(ErrorCode::CryptoUnavailable, "This build has no cryptography backend");
    if (sealed.salt.size() != kSaltSize || sealed.nonce.size() != kNonceSize) return R::Failure(ErrorCode::ParseError, "The sealed seed is malformed");
    UltraCryptKdfParams kdf;
    kdf.iterations = sealed.iterations;
    kdf.memoryKiB = sealed.memoryKiB;
    kdf.salt = sealed.salt;
    auto key = DeriveKey(passphrase, kdf);
    if (!key) return R::Failure(key.error.code, key.error.message, key.error.detail);
    UltraCryptAeadParams params;
    params.algorithm = UltraCryptAeadAlgorithm::XChaCha20Poly1305;
    params.nonce = sealed.nonce;
    params.associatedData = Header(sealed);
    UltraCryptSecureBuffer plain;
    const UltraCryptResult r = UltraCrypt_AeadOpen(key.Value(), params, sealed.cipher.data(), sealed.cipher.size(), plain);
    if (!r) return R::Failure(ErrorCode::SignatureInvalid, "The passphrase is wrong, or the sealed seed was altered");
    std::vector<uint8_t> seed(plain.Data(), plain.Data() + plain.GetSize());
    if (seed.size() != 32) return R::Failure(ErrorCode::ParseError, "The sealed seed is not 32 bytes");
    return R::Succeed(std::move(seed));
}

Result<NewKey> NewSigningKey(const std::string& passphrase, KeyCost cost) {
    using R = Result<NewKey>;
    std::vector<uint8_t> seed, pk;
    const auto generated = UltraStore::GenerateSigningKey(seed, pk);
    if (!generated) return R::Failure(generated.error.code, generated.error.message, generated.error.detail);
    auto sealed = SealSeed(seed, passphrase, cost);
    UltraCrypt_SecureZero(seed.data(), seed.size());
    if (!sealed) return R::Failure(sealed.error.code, sealed.error.message, sealed.error.detail);
    NewKey key;
    key.publicKeyHex = UltraStore::ToHex(pk);
    key.sealed = std::move(sealed.Value());
    return R::Succeed(std::move(key));
}

} // namespace UltraStoreAdmin
