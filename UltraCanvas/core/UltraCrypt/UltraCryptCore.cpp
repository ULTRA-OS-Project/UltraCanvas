// core/UltraCrypt/UltraCryptCore.cpp
// libsodium-backed implementation of the UltraCrypt surface.
//
// When ULTRACRYPT_HAVE_SODIUM is not defined the module still compiles, but
// every operation returns BackendUnavailable. That is deliberate: the failure
// mode this module exists to prevent is security code that silently does
// nothing (see UltraCanvasDocument's dormant #ifdef ULTRACANVAS_USE_OPENSSL
// fallback, which returned success while passing plaintext through). Nothing
// here ever reports success without doing the work.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraCrypt/UltraCryptCore.h"

#include <cstring>
#include <fstream>
#include <mutex>
#include <new>

#ifdef ULTRACRYPT_HAVE_SODIUM
#include <sodium.h>
#endif

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#endif

// ============================================================================
// Result helpers
// ============================================================================
UltraCryptResult UltraCryptResult::Ok() {
    UltraCryptResult r;
    r.code = UltraCryptResultCode::Success;
    r.success = true;
    return r;
}

UltraCryptResult UltraCryptResult::Error(UltraCryptResultCode code,
                                         const std::string& message) {
    UltraCryptResult r;
    r.code = code;
    r.success = false;
    r.message = message;
    return r;
}

namespace {

UltraCryptResult NoBackend() {
    return UltraCryptResult::Error(
        UltraCryptResultCode::BackendUnavailable,
        "UltraCrypt was built without a crypto backend (libsodium). "
        "No cryptographic operation can be performed.");
}

#ifdef ULTRACRYPT_HAVE_SODIUM
std::once_flag g_initOnce;
bool           g_initOk = false;

void InitOnce() {
    std::call_once(g_initOnce, []() {
        // sodium_init() returns 0 on success, 1 if already initialised.
        g_initOk = (sodium_init() >= 0);
    });
}
#endif

bool EnsureReady() {
#ifdef ULTRACRYPT_HAVE_SODIUM
    InitOnce();
    return g_initOk;
#else
    return false;
#endif
}

} // namespace

// ============================================================================
// Lifecycle
// ============================================================================
UltraCryptResult UltraCrypt_Initialize() {
#ifdef ULTRACRYPT_HAVE_SODIUM
    if (!EnsureReady()) {
        return UltraCryptResult::Error(UltraCryptResultCode::InternalError,
                                       "libsodium initialisation failed");
    }
    return UltraCryptResult::Ok();
#else
    return NoBackend();
#endif
}

void UltraCrypt_Shutdown() {
    // libsodium needs no teardown; kept for API symmetry with UltraNet.
}

bool UltraCrypt_IsAvailable() {
    return EnsureReady();
}

std::string UltraCrypt_GetBackendName() {
#ifdef ULTRACRYPT_HAVE_SODIUM
    if (!EnsureReady()) return "libsodium (initialisation failed)";
    return std::string("libsodium ") + sodium_version_string();
#else
    return "none";
#endif
}

// ============================================================================
// Secure memory
// ============================================================================
void UltraCrypt_SecureZero(void* data, size_t size) {
    if (!data || size == 0) return;
#ifdef ULTRACRYPT_HAVE_SODIUM
    if (EnsureReady()) {
        sodium_memzero(data, size);
        return;
    }
#endif
#if defined(_WIN32)
    SecureZeroMemory(data, size);
#else
    // volatile write barrier: prevents the store from being optimised away.
    volatile unsigned char* p = static_cast<volatile unsigned char*>(data);
    while (size--) *p++ = 0;
#endif
}

bool UltraCrypt_ConstantTimeEquals(const void* a, size_t aSize,
                                   const void* b, size_t bSize) {
    if (aSize != bSize) return false;
    if (aSize == 0) return true;
    if (!a || !b) return false;
#ifdef ULTRACRYPT_HAVE_SODIUM
    if (EnsureReady()) return sodium_memcmp(a, b, aSize) == 0;
#endif
    const auto* pa = static_cast<const unsigned char*>(a);
    const auto* pb = static_cast<const unsigned char*>(b);
    unsigned char diff = 0;
    for (size_t i = 0; i < aSize; ++i) diff |= static_cast<unsigned char>(pa[i] ^ pb[i]);
    return diff == 0;
}

UltraCryptSecureBuffer::UltraCryptSecureBuffer(size_t size) {
    Resize(size);
}

UltraCryptSecureBuffer::UltraCryptSecureBuffer(const void* data, size_t size) {
    Resize(size);
    if (data && size > 0 && data_) std::memcpy(data_, data, size);
}

UltraCryptSecureBuffer::~UltraCryptSecureBuffer() {
    Release();
}

UltraCryptSecureBuffer::UltraCryptSecureBuffer(UltraCryptSecureBuffer&& other) noexcept
    : data_(other.data_), size_(other.size_), locked_(other.locked_) {
    other.data_ = nullptr;
    other.size_ = 0;
    other.locked_ = false;
}

UltraCryptSecureBuffer&
UltraCryptSecureBuffer::operator=(UltraCryptSecureBuffer&& other) noexcept {
    if (this != &other) {
        Release();
        data_ = other.data_;
        size_ = other.size_;
        locked_ = other.locked_;
        other.data_ = nullptr;
        other.size_ = 0;
        other.locked_ = false;
    }
    return *this;
}

UltraCryptSecureBuffer UltraCryptSecureBuffer::Clone() const {
    return UltraCryptSecureBuffer(data_, size_);
}

void UltraCryptSecureBuffer::Release() {
    if (data_) {
        UltraCrypt_SecureZero(data_, size_);
        if (locked_) {
#if defined(_WIN32)
            VirtualUnlock(data_, size_);
#else
            munlock(data_, size_);
#endif
        }
        ::operator delete(data_, std::nothrow);
    }
    data_ = nullptr;
    size_ = 0;
    locked_ = false;
}

void UltraCryptSecureBuffer::Resize(size_t size) {
    if (size == size_) return;
    if (size == 0) { Release(); return; }

    auto* fresh = static_cast<uint8_t*>(::operator new(size, std::nothrow));
    if (!fresh) { Release(); return; }
    std::memset(fresh, 0, size);
    if (data_ && size_ > 0) {
        std::memcpy(fresh, data_, size < size_ ? size : size_);
    }
    const bool wasLocked = locked_;
    Release();                 // wipes the old bytes, including any truncated tail
    data_ = fresh;
    size_ = size;
    if (wasLocked) LockPages();
}

void UltraCryptSecureBuffer::Clear() {
    Release();
}

bool UltraCryptSecureBuffer::LockPages() {
    if (!data_ || size_ == 0) return false;
    if (locked_) return true;
#if defined(_WIN32)
    locked_ = VirtualLock(data_, size_) != 0;
#else
    locked_ = mlock(data_, size_) == 0;
#endif
    return locked_;
}

UltraCryptSecureBuffer UltraCryptSecureBuffer::AdoptString(std::string& text) {
    UltraCryptSecureBuffer buffer(text.data(), text.size());
    if (!text.empty()) UltraCrypt_SecureZero(text.data(), text.size());
    text.clear();
    return buffer;
}

// ============================================================================
// Hashing
// ============================================================================
size_t UltraCrypt_GetDigestSize(UltraCryptHashAlgorithm algorithm) {
    switch (algorithm) {
        case UltraCryptHashAlgorithm::SHA256: return 32;
        case UltraCryptHashAlgorithm::SHA512: return 64;
    }
    return 0;
}

bool UltraCrypt_IsHashAvailable(UltraCryptHashAlgorithm algorithm) {
    (void)algorithm;
    return EnsureReady();
}

UltraCryptResult UltraCrypt_Hash(UltraCryptHashAlgorithm algorithm,
                                 const void* data, size_t size,
                                 std::vector<uint8_t>& outDigest) {
    if (!EnsureReady()) return NoBackend();
    if (size > 0 && !data) {
        return UltraCryptResult::Error(UltraCryptResultCode::InvalidArgument,
                                       "null data with non-zero size");
    }
#ifdef ULTRACRYPT_HAVE_SODIUM
    outDigest.assign(UltraCrypt_GetDigestSize(algorithm), 0);
    const auto* in = static_cast<const unsigned char*>(data);
    const unsigned char empty = 0;
    if (!in) in = &empty;
    int rc = -1;
    switch (algorithm) {
        case UltraCryptHashAlgorithm::SHA256:
            rc = crypto_hash_sha256(outDigest.data(), in, size);
            break;
        case UltraCryptHashAlgorithm::SHA512:
            rc = crypto_hash_sha512(outDigest.data(), in, size);
            break;
    }
    if (rc != 0) {
        outDigest.clear();
        return UltraCryptResult::Error(UltraCryptResultCode::InternalError,
                                       "hash computation failed");
    }
    return UltraCryptResult::Ok();
#else
    (void)algorithm; (void)outDigest;
    return NoBackend();
#endif
}

UltraCryptResult UltraCrypt_HashFile(UltraCryptHashAlgorithm algorithm,
                                     const std::string& filePath,
                                     std::vector<uint8_t>& outDigest) {
    if (!EnsureReady()) return NoBackend();

    std::ifstream file(filePath, std::ios::binary);
    if (!file) {
        return UltraCryptResult::Error(UltraCryptResultCode::InvalidArgument,
                                       "could not open file: " + filePath);
    }
    UltraCryptHasher hasher(algorithm);
    if (!hasher.IsValid()) return NoBackend();

    std::vector<char> chunk(64 * 1024);
    while (file.read(chunk.data(), static_cast<std::streamsize>(chunk.size())) ||
           file.gcount() > 0) {
        hasher.Update(chunk.data(), static_cast<size_t>(file.gcount()));
        if (file.eof()) break;
    }
    outDigest = hasher.Final();
    return UltraCryptResult::Ok();
}

std::string UltraCrypt_ToHex(const std::vector<uint8_t>& bytes) {
    static const char* kDigits = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (uint8_t b : bytes) {
        out.push_back(kDigits[b >> 4]);
        out.push_back(kDigits[b & 0x0F]);
    }
    return out;
}

bool UltraCrypt_FromHex(const std::string& hex, std::vector<uint8_t>& outBytes) {
    if (hex.size() % 2 != 0) return false;
    outBytes.clear();
    outBytes.reserve(hex.size() / 2);
    auto nibble = [](char c, int& value) {
        if (c >= '0' && c <= '9') { value = c - '0'; return true; }
        if (c >= 'a' && c <= 'f') { value = c - 'a' + 10; return true; }
        if (c >= 'A' && c <= 'F') { value = c - 'A' + 10; return true; }
        return false;
    };
    for (size_t i = 0; i < hex.size(); i += 2) {
        int hi = 0, lo = 0;
        if (!nibble(hex[i], hi) || !nibble(hex[i + 1], lo)) {
            outBytes.clear();
            return false;
        }
        outBytes.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    return true;
}

#ifdef ULTRACRYPT_HAVE_SODIUM
namespace {
struct HasherState {
    UltraCryptHashAlgorithm algorithm;
    crypto_hash_sha256_state sha256;
    crypto_hash_sha512_state sha512;
};
} // namespace
#endif

UltraCryptHasher::UltraCryptHasher(UltraCryptHashAlgorithm algorithm)
    : algorithm_(algorithm) {
#ifdef ULTRACRYPT_HAVE_SODIUM
    if (!EnsureReady()) return;
    auto* s = new (std::nothrow) HasherState();
    if (!s) return;
    s->algorithm = algorithm;
    state_ = s;
    Reset();
#endif
}

UltraCryptHasher::~UltraCryptHasher() {
#ifdef ULTRACRYPT_HAVE_SODIUM
    auto* s = static_cast<HasherState*>(state_);
    if (s) {
        UltraCrypt_SecureZero(s, sizeof(*s));
        delete s;
    }
#endif
    state_ = nullptr;
}

void UltraCryptHasher::Reset() {
#ifdef ULTRACRYPT_HAVE_SODIUM
    auto* s = static_cast<HasherState*>(state_);
    if (!s) return;
    switch (algorithm_) {
        case UltraCryptHashAlgorithm::SHA256: crypto_hash_sha256_init(&s->sha256); break;
        case UltraCryptHashAlgorithm::SHA512: crypto_hash_sha512_init(&s->sha512); break;
    }
#endif
}

void UltraCryptHasher::Update(const void* data, size_t size) {
#ifdef ULTRACRYPT_HAVE_SODIUM
    auto* s = static_cast<HasherState*>(state_);
    if (!s || size == 0 || !data) return;
    const auto* in = static_cast<const unsigned char*>(data);
    switch (algorithm_) {
        case UltraCryptHashAlgorithm::SHA256: crypto_hash_sha256_update(&s->sha256, in, size); break;
        case UltraCryptHashAlgorithm::SHA512: crypto_hash_sha512_update(&s->sha512, in, size); break;
    }
#else
    (void)data; (void)size;
#endif
}

std::vector<uint8_t> UltraCryptHasher::Final() {
    std::vector<uint8_t> digest;
#ifdef ULTRACRYPT_HAVE_SODIUM
    auto* s = static_cast<HasherState*>(state_);
    if (!s) return digest;
    digest.assign(GetDigestSize(), 0);
    switch (algorithm_) {
        case UltraCryptHashAlgorithm::SHA256: crypto_hash_sha256_final(&s->sha256, digest.data()); break;
        case UltraCryptHashAlgorithm::SHA512: crypto_hash_sha512_final(&s->sha512, digest.data()); break;
    }
#endif
    return digest;
}

size_t UltraCryptHasher::GetDigestSize() const {
    return UltraCrypt_GetDigestSize(algorithm_);
}

// ============================================================================
// HMAC
// ============================================================================
UltraCryptResult UltraCrypt_Hmac(UltraCryptHashAlgorithm algorithm,
                                 const UltraCryptSecureBuffer& key,
                                 const void* data, size_t size,
                                 std::vector<uint8_t>& outMac) {
    if (!EnsureReady()) return NoBackend();
    if (size > 0 && !data) {
        return UltraCryptResult::Error(UltraCryptResultCode::InvalidArgument,
                                       "null data with non-zero size");
    }
#ifdef ULTRACRYPT_HAVE_SODIUM
    // The *_init variants accept an arbitrary key length and apply the standard
    // HMAC key schedule (hash keys longer than the block size, zero-pad
    // shorter ones), so callers are not constrained to a fixed key size.
    const auto* in = static_cast<const unsigned char*>(data);
    const unsigned char empty = 0;
    if (!in) in = &empty;

    switch (algorithm) {
        case UltraCryptHashAlgorithm::SHA256: {
            crypto_auth_hmacsha256_state state;
            if (crypto_auth_hmacsha256_init(&state, key.Data(), key.GetSize()) != 0) {
                return UltraCryptResult::Error(UltraCryptResultCode::InternalError,
                                               "HMAC init failed");
            }
            crypto_auth_hmacsha256_update(&state, in, size);
            outMac.assign(crypto_auth_hmacsha256_BYTES, 0);
            crypto_auth_hmacsha256_final(&state, outMac.data());
            UltraCrypt_SecureZero(&state, sizeof(state));
            return UltraCryptResult::Ok();
        }
        case UltraCryptHashAlgorithm::SHA512: {
            crypto_auth_hmacsha512_state state;
            if (crypto_auth_hmacsha512_init(&state, key.Data(), key.GetSize()) != 0) {
                return UltraCryptResult::Error(UltraCryptResultCode::InternalError,
                                               "HMAC init failed");
            }
            crypto_auth_hmacsha512_update(&state, in, size);
            outMac.assign(crypto_auth_hmacsha512_BYTES, 0);
            crypto_auth_hmacsha512_final(&state, outMac.data());
            UltraCrypt_SecureZero(&state, sizeof(state));
            return UltraCryptResult::Ok();
        }
    }
    return UltraCryptResult::Error(UltraCryptResultCode::NotSupported,
                                   "unknown hash algorithm");
#else
    (void)algorithm; (void)key; (void)outMac;
    return NoBackend();
#endif
}

// ============================================================================
// AEAD
// ============================================================================
size_t UltraCrypt_GetKeySize(UltraCryptAeadAlgorithm algorithm) {
    (void)algorithm;
    return 32;   // both algorithms use a 256-bit key
}

size_t UltraCrypt_GetNonceSize(UltraCryptAeadAlgorithm algorithm) {
    switch (algorithm) {
        case UltraCryptAeadAlgorithm::XChaCha20Poly1305: return 24;
        case UltraCryptAeadAlgorithm::Aes256Gcm:         return 12;
    }
    return 0;
}

size_t UltraCrypt_GetTagSize(UltraCryptAeadAlgorithm algorithm) {
    (void)algorithm;
    return 16;
}

bool UltraCrypt_IsAeadAvailable(UltraCryptAeadAlgorithm algorithm) {
    if (!EnsureReady()) return false;
#ifdef ULTRACRYPT_HAVE_SODIUM
    switch (algorithm) {
        case UltraCryptAeadAlgorithm::XChaCha20Poly1305:
            return true;
        case UltraCryptAeadAlgorithm::Aes256Gcm:
            // Hardware-gated: libsodium ships no software AES-GCM.
            return crypto_aead_aes256gcm_is_available() != 0;
    }
#endif
    (void)algorithm;
    return false;
}

UltraCryptResult UltraCrypt_AeadSeal(const UltraCryptSecureBuffer& key,
                                     UltraCryptAeadParams& params,
                                     const void* plaintext, size_t size,
                                     std::vector<uint8_t>& outCiphertext) {
    if (!EnsureReady()) return NoBackend();
    if (!UltraCrypt_IsAeadAvailable(params.algorithm)) {
        return UltraCryptResult::Error(
            UltraCryptResultCode::NotSupported,
            "AEAD algorithm unavailable on this CPU or in this build");
    }
    if (key.GetSize() != UltraCrypt_GetKeySize(params.algorithm)) {
        return UltraCryptResult::Error(UltraCryptResultCode::InvalidKeySize,
                                       "AEAD key must be 32 bytes");
    }
    if (size > 0 && !plaintext) {
        return UltraCryptResult::Error(UltraCryptResultCode::InvalidArgument,
                                       "null plaintext with non-zero size");
    }
#ifdef ULTRACRYPT_HAVE_SODIUM
    const size_t nonceSize = UltraCrypt_GetNonceSize(params.algorithm);
    if (params.nonce.empty()) {
        // Generating the nonce by default is what makes nonce reuse — the
        // catastrophic failure mode of both GCM and Poly1305 — something a
        // caller must opt into rather than forget to avoid.
        params.nonce.assign(nonceSize, 0);
        auto rr = UltraCrypt_RandomBytes(params.nonce.data(), nonceSize);
        if (!rr) { params.nonce.clear(); return rr; }
    } else if (params.nonce.size() != nonceSize) {
        return UltraCryptResult::Error(UltraCryptResultCode::InvalidNonceSize,
                                       "nonce length does not match the algorithm");
    }

    const auto* in = static_cast<const unsigned char*>(plaintext);
    const unsigned char empty = 0;
    if (!in) in = &empty;
    const unsigned char* ad = params.associatedData.empty()
                                  ? nullptr : params.associatedData.data();
    const unsigned long long adLen = params.associatedData.size();

    outCiphertext.assign(size + UltraCrypt_GetTagSize(params.algorithm), 0);
    unsigned long long written = 0;
    int rc = -1;
    switch (params.algorithm) {
        case UltraCryptAeadAlgorithm::XChaCha20Poly1305:
            rc = crypto_aead_xchacha20poly1305_ietf_encrypt(
                     outCiphertext.data(), &written, in, size,
                     ad, adLen, nullptr, params.nonce.data(), key.Data());
            break;
        case UltraCryptAeadAlgorithm::Aes256Gcm:
            rc = crypto_aead_aes256gcm_encrypt(
                     outCiphertext.data(), &written, in, size,
                     ad, adLen, nullptr, params.nonce.data(), key.Data());
            break;
    }
    if (rc != 0) {
        outCiphertext.clear();
        return UltraCryptResult::Error(UltraCryptResultCode::InternalError,
                                       "AEAD encryption failed");
    }
    outCiphertext.resize(static_cast<size_t>(written));
    return UltraCryptResult::Ok();
#else
    (void)plaintext; (void)outCiphertext;
    return NoBackend();
#endif
}

UltraCryptResult UltraCrypt_AeadOpen(const UltraCryptSecureBuffer& key,
                                     const UltraCryptAeadParams& params,
                                     const void* ciphertext, size_t size,
                                     UltraCryptSecureBuffer& outPlaintext) {
    outPlaintext.Clear();
    if (!EnsureReady()) return NoBackend();
    if (!UltraCrypt_IsAeadAvailable(params.algorithm)) {
        return UltraCryptResult::Error(
            UltraCryptResultCode::NotSupported,
            "AEAD algorithm unavailable on this CPU or in this build");
    }
    if (key.GetSize() != UltraCrypt_GetKeySize(params.algorithm)) {
        return UltraCryptResult::Error(UltraCryptResultCode::InvalidKeySize,
                                       "AEAD key must be 32 bytes");
    }
    if (params.nonce.size() != UltraCrypt_GetNonceSize(params.algorithm)) {
        return UltraCryptResult::Error(UltraCryptResultCode::InvalidNonceSize,
                                       "nonce length does not match the algorithm");
    }
    const size_t tagSize = UltraCrypt_GetTagSize(params.algorithm);
    if (size < tagSize || !ciphertext) {
        return UltraCryptResult::Error(UltraCryptResultCode::AuthenticationFailed,
                                       "ciphertext is shorter than the authentication tag");
    }
#ifdef ULTRACRYPT_HAVE_SODIUM
    UltraCryptSecureBuffer plain(size - tagSize);
    if (plain.GetSize() != size - tagSize) {
        return UltraCryptResult::Error(UltraCryptResultCode::InternalError,
                                       "could not allocate plaintext buffer");
    }
    const unsigned char* ad = params.associatedData.empty()
                                  ? nullptr : params.associatedData.data();
    const unsigned long long adLen = params.associatedData.size();
    const auto* in = static_cast<const unsigned char*>(ciphertext);

    unsigned long long written = 0;
    int rc = -1;
    switch (params.algorithm) {
        case UltraCryptAeadAlgorithm::XChaCha20Poly1305:
            rc = crypto_aead_xchacha20poly1305_ietf_decrypt(
                     plain.Data(), &written, nullptr, in, size,
                     ad, adLen, params.nonce.data(), key.Data());
            break;
        case UltraCryptAeadAlgorithm::Aes256Gcm:
            rc = crypto_aead_aes256gcm_decrypt(
                     plain.Data(), &written, nullptr, in, size,
                     ad, adLen, params.nonce.data(), key.Data());
            break;
    }
    if (rc != 0) {
        // Deliberately one undifferentiated failure: distinguishing a wrong
        // key from altered ciphertext or altered AAD would be an oracle.
        plain.Clear();
        return UltraCryptResult::Error(
            UltraCryptResultCode::AuthenticationFailed,
            "authentication failed: wrong key, or the data was altered");
    }
    plain.Resize(static_cast<size_t>(written));
    outPlaintext = std::move(plain);
    return UltraCryptResult::Ok();
#else
    return NoBackend();
#endif
}

// ============================================================================
// Key derivation
// ============================================================================
size_t UltraCrypt_GetKdfSaltSize() {
#ifdef ULTRACRYPT_HAVE_SODIUM
    return crypto_pwhash_SALTBYTES;   // 16
#else
    return 16;
#endif
}

UltraCryptKdfParams UltraCrypt_RecommendedKdfParams(
        UltraCryptKdfAlgorithm algorithm) {
    UltraCryptKdfParams params;
    params.algorithm    = algorithm;
    params.iterations   = 3;          // passes
    params.memoryKiB    = 64 * 1024;  // 64 MiB
    params.parallelism  = 1;
    params.outputLength = 32;
    return params;
}

UltraCryptResult UltraCrypt_DeriveKeyFromPassword(
        const UltraCryptSecureBuffer& password,
        UltraCryptKdfParams& params,
        UltraCryptSecureBuffer& outKey) {
    outKey.Clear();
    if (!EnsureReady()) return NoBackend();
    if (params.algorithm != UltraCryptKdfAlgorithm::Argon2id) {
        return UltraCryptResult::Error(UltraCryptResultCode::NotSupported,
                                       "only Argon2id is supported");
    }
    if (params.parallelism != 1) {
        // The backing implementation fixes Argon2id parallelism at 1. Rejecting
        // anything else keeps stored parameters honest: a file that records
        // parallelism=4 must never have been derived with parallelism=1.
        return UltraCryptResult::Error(
            UltraCryptResultCode::NotSupported,
            "Argon2id parallelism is fixed at 1 by the backing implementation");
    }
    if (params.outputLength < 16) {
        return UltraCryptResult::Error(UltraCryptResultCode::InvalidArgument,
                                       "derived key must be at least 16 bytes");
    }
    if (params.iterations == 0 || params.memoryKiB == 0) {
        return UltraCryptResult::Error(UltraCryptResultCode::InvalidArgument,
                                       "Argon2id cost parameters must be non-zero");
    }
#ifdef ULTRACRYPT_HAVE_SODIUM
    const size_t saltSize = UltraCrypt_GetKdfSaltSize();
    if (params.salt.empty()) {
        params.salt.assign(saltSize, 0);
        auto rr = UltraCrypt_RandomBytes(params.salt.data(), saltSize);
        if (!rr) { params.salt.clear(); return rr; }
    } else if (params.salt.size() != saltSize) {
        return UltraCryptResult::Error(UltraCryptResultCode::InvalidArgument,
                                       "Argon2id salt must be 16 bytes");
    }

    if (params.iterations < crypto_pwhash_OPSLIMIT_MIN ||
        static_cast<unsigned long long>(params.memoryKiB) * 1024ULL <
            crypto_pwhash_MEMLIMIT_MIN) {
        return UltraCryptResult::Error(
            UltraCryptResultCode::InvalidArgument,
            "Argon2id cost parameters below the minimum accepted by the backend");
    }

    UltraCryptSecureBuffer key(params.outputLength);
    if (key.GetSize() != params.outputLength) {
        return UltraCryptResult::Error(UltraCryptResultCode::InternalError,
                                       "could not allocate key buffer");
    }
    const int rc = crypto_pwhash(
            key.Data(), static_cast<unsigned long long>(params.outputLength),
            reinterpret_cast<const char*>(password.Data()), password.GetSize(),
            params.salt.data(),
            static_cast<unsigned long long>(params.iterations),
            static_cast<size_t>(params.memoryKiB) * 1024u,
            crypto_pwhash_ALG_ARGON2ID13);
    if (rc != 0) {
        // crypto_pwhash returns -1 when it cannot allocate the memory cost.
        return UltraCryptResult::Error(
            UltraCryptResultCode::InternalError,
            "Argon2id derivation failed (most likely out of memory for the "
            "requested memory cost)");
    }
    outKey = std::move(key);
    return UltraCryptResult::Ok();
#else
    (void)password;
    return NoBackend();
#endif
}

// ============================================================================
// Random
// ============================================================================
UltraCryptResult UltraCrypt_RandomBytes(void* out, size_t size) {
    if (!EnsureReady()) return NoBackend();
    if (size == 0) return UltraCryptResult::Ok();
    if (!out) {
        return UltraCryptResult::Error(UltraCryptResultCode::InvalidArgument,
                                       "null output buffer");
    }
#ifdef ULTRACRYPT_HAVE_SODIUM
    randombytes_buf(out, size);
    return UltraCryptResult::Ok();
#else
    return NoBackend();
#endif
}

UltraCryptResult UltraCrypt_RandomBytes(std::vector<uint8_t>& out, size_t size) {
    if (!EnsureReady()) return NoBackend();
    std::vector<uint8_t> fresh(size, 0);
    auto r = UltraCrypt_RandomBytes(fresh.data(), size);
    if (!r) return r;
    out = std::move(fresh);
    return UltraCryptResult::Ok();
}

UltraCryptResult UltraCrypt_RandomSecureBuffer(size_t size,
                                               UltraCryptSecureBuffer& out) {
    if (!EnsureReady()) return NoBackend();
    UltraCryptSecureBuffer fresh(size);
    if (fresh.GetSize() != size) {
        return UltraCryptResult::Error(UltraCryptResultCode::InternalError,
                                       "could not allocate buffer");
    }
    auto r = UltraCrypt_RandomBytes(fresh.Data(), size);
    if (!r) return r;
    out = std::move(fresh);
    return UltraCryptResult::Ok();
}

UltraCryptResult UltraCrypt_RandomUInt32(uint32_t bound, uint32_t& out) {
    if (!EnsureReady()) return NoBackend();
    if (bound == 0) {
        return UltraCryptResult::Error(UltraCryptResultCode::InvalidArgument,
                                       "bound must be greater than zero");
    }
#ifdef ULTRACRYPT_HAVE_SODIUM
    out = randombytes_uniform(bound);   // rejection-sampled: no modulo bias
    return UltraCryptResult::Ok();
#else
    (void)out;
    return NoBackend();
#endif
}

UltraCryptResult UltraCrypt_GenerateUuidV4(std::string& out) {
    std::vector<uint8_t> bytes;
    auto r = UltraCrypt_RandomBytes(bytes, 16);
    if (!r) return r;

    bytes[6] = static_cast<uint8_t>((bytes[6] & 0x0F) | 0x40);   // version 4
    bytes[8] = static_cast<uint8_t>((bytes[8] & 0x3F) | 0x80);   // variant 10x

    const std::string hex = UltraCrypt_ToHex(bytes);
    out = hex.substr(0, 8) + "-" + hex.substr(8, 4) + "-" +
          hex.substr(12, 4) + "-" + hex.substr(16, 4) + "-" + hex.substr(20, 12);
    return UltraCryptResult::Ok();
}
