// core/UltraVault/UltraVaultCore.cpp
// UltraVault implementation: memory and encrypted-file backends.
//
// File format v1 ("UVLT"):
//   magic[4]="UVLT" | version u8=1 | kdfIterations u32 | kdfMemoryKiB u32 |
//   saltLen u8 | salt | nonceLen u8 | nonce | ciphertext(+tag)
// Integers are little-endian. Everything before the ciphertext is passed to
// the AEAD as associated data, so editing the stored KDF cost parameters,
// salt or nonce invalidates the tag instead of being obeyed.
//
// Decrypted payload:
//   count u32, then per record: key, mimeType, value, ownerAppId as
//   u32-length-prefixed byte strings, readerCount u32 + readers,
//   requiresUserPresence u8.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraVault/UltraVault.h"
#include "UltraCrypt/UltraCryptCore.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <utility>

namespace UltraVault {

namespace {

constexpr char    kMagic[4]  = {'U', 'V', 'L', 'T'};
constexpr uint8_t kVersion   = 1;

struct StoredSecret {
    SecretValue value;
    SecretAcl   acl;
};

struct VaultState {
    std::mutex mutex;
    bool open = false;
    Backend backend = Backend::Memory;
    std::string filePath;
    UltraCryptSecureBuffer fileKey;      // derived once per session
    UltraCryptKdfParams    kdfParams;    // as stored in the file header
    std::map<std::string, StoredSecret> secrets;
};

VaultState& State() {
    static VaultState s;
    return s;
}

bool IsValidKey(const std::string& key) {
    if (key.empty() || key.size() > 256) return false;
    for (unsigned char c : key) {
        if (c <= ' ' || c > '~') return false;
    }
    return true;
}

// --------------------------------------------------------------------------
// Payload (de)serialization
// --------------------------------------------------------------------------
void PutU32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v));
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v >> 16));
    out.push_back(static_cast<uint8_t>(v >> 24));
}

void PutBlob(std::vector<uint8_t>& out, const void* data, size_t size) {
    PutU32(out, static_cast<uint32_t>(size));
    const auto* p = static_cast<const uint8_t*>(data);
    out.insert(out.end(), p, p + size);
}

void PutString(std::vector<uint8_t>& out, const std::string& s) {
    PutBlob(out, s.data(), s.size());
}

struct Reader {
    const uint8_t* p;
    size_t remaining;
    bool ok = true;

    bool Take(void* out, size_t size) {
        if (!ok || size > remaining) { ok = false; return false; }
        std::memcpy(out, p, size);
        p += size;
        remaining -= size;
        return true;
    }
    uint32_t U32() {
        uint8_t b[4] = {};
        if (!Take(b, 4)) return 0;
        return static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8) |
               (static_cast<uint32_t>(b[2]) << 16) |
               (static_cast<uint32_t>(b[3]) << 24);
    }
    uint8_t U8() {
        uint8_t b = 0;
        Take(&b, 1);
        return b;
    }
    std::string String() {
        const uint32_t n = U32();
        if (!ok || n > remaining) { ok = false; return {}; }
        std::string s(reinterpret_cast<const char*>(p), n);
        p += n; remaining -= n;
        return s;
    }
    std::vector<uint8_t> Blob() {
        const uint32_t n = U32();
        if (!ok || n > remaining) { ok = false; return {}; }
        std::vector<uint8_t> v(p, p + n);
        p += n; remaining -= n;
        return v;
    }
};

std::vector<uint8_t> SerializeSecrets(
        const std::map<std::string, StoredSecret>& secrets) {
    std::vector<uint8_t> out;
    PutU32(out, static_cast<uint32_t>(secrets.size()));
    for (const auto& [key, stored] : secrets) {
        PutString(out, key);
        PutString(out, stored.value.mimeType);
        PutBlob(out, stored.value.bytes.data(), stored.value.bytes.size());
        PutString(out, stored.acl.ownerAppId);
        PutU32(out, static_cast<uint32_t>(stored.acl.readers.size()));
        for (const auto& reader : stored.acl.readers) PutString(out, reader);
        out.push_back(stored.acl.requiresUserPresence ? 1 : 0);
    }
    return out;
}

bool DeserializeSecrets(const uint8_t* data, size_t size,
                        std::map<std::string, StoredSecret>& outSecrets) {
    Reader r{data, size};
    const uint32_t count = r.U32();
    std::map<std::string, StoredSecret> parsed;
    for (uint32_t i = 0; i < count && r.ok; ++i) {
        const std::string key = r.String();
        StoredSecret s;
        s.value.mimeType = r.String();
        s.value.bytes    = r.Blob();
        s.acl.ownerAppId = r.String();
        const uint32_t readers = r.U32();
        for (uint32_t j = 0; j < readers && r.ok; ++j) {
            s.acl.readers.push_back(r.String());
        }
        s.acl.requiresUserPresence = r.U8() != 0;
        if (r.ok) parsed.emplace(key, std::move(s));
    }
    if (!r.ok || r.remaining != 0) return false;
    outSecrets = std::move(parsed);
    return true;
}

// --------------------------------------------------------------------------
// File backend
// --------------------------------------------------------------------------
std::vector<uint8_t> BuildHeader(const UltraCryptKdfParams& kdf,
                                 const std::vector<uint8_t>& nonce) {
    std::vector<uint8_t> h;
    h.insert(h.end(), kMagic, kMagic + 4);
    h.push_back(kVersion);
    PutU32(h, kdf.iterations);
    PutU32(h, kdf.memoryKiB);
    h.push_back(static_cast<uint8_t>(kdf.salt.size()));
    h.insert(h.end(), kdf.salt.begin(), kdf.salt.end());
    h.push_back(static_cast<uint8_t>(nonce.size()));
    h.insert(h.end(), nonce.begin(), nonce.end());
    return h;
}

Result SaveFileLocked(VaultState& s) {
    const std::vector<uint8_t> payload = SerializeSecrets(s.secrets);

    UltraCryptAeadParams aead;                    // fresh random nonce
    // Seal against provisional AAD-less params first is wrong — the nonce is
    // needed inside the header before sealing. Generate it explicitly.
    aead.nonce.resize(UltraCrypt_GetNonceSize(aead.algorithm));
    if (!UltraCrypt_RandomBytes(aead.nonce.data(), aead.nonce.size())) {
        return Result::Error(ResultCode::BackendUnavailable,
                             "no entropy source for the vault nonce");
    }
    const std::vector<uint8_t> header = BuildHeader(s.kdfParams, aead.nonce);
    aead.associatedData = header;

    std::vector<uint8_t> ciphertext;
    UltraCryptResult sealed = UltraCrypt_AeadSeal(
        s.fileKey, aead, payload.data(), payload.size(), ciphertext);
    if (!sealed) {
        return Result::Error(ResultCode::BackendUnavailable,
                             "vault encryption failed: " + sealed.message);
    }

    const std::string tmpPath = s.filePath + ".tmp";
    std::FILE* f = std::fopen(tmpPath.c_str(), "wb");
    if (!f) {
        return Result::Error(ResultCode::IoError,
                             "cannot write vault file: " + tmpPath);
    }
    bool wrote =
        std::fwrite(header.data(), 1, header.size(), f) == header.size() &&
        std::fwrite(ciphertext.data(), 1, ciphertext.size(), f) ==
            ciphertext.size();
    wrote = (std::fclose(f) == 0) && wrote;
    if (!wrote || std::rename(tmpPath.c_str(), s.filePath.c_str()) != 0) {
        std::remove(tmpPath.c_str());
        return Result::Error(ResultCode::IoError,
                             "cannot update vault file: " + s.filePath);
    }
    return Result::Ok();
}

Result LoadFileLocked(VaultState& s, UltraCryptSecureBuffer& passphrase) {
    std::FILE* f = std::fopen(s.filePath.c_str(), "rb");
    if (!f) {
        // A missing vault file is a fresh vault: derive with recommended
        // parameters; the file appears on the first Put.
        s.kdfParams = UltraCrypt_RecommendedKdfParams();
        UltraCryptResult derived =
            UltraCrypt_DeriveKeyFromPassword(passphrase, s.kdfParams, s.fileKey);
        if (!derived) {
            return Result::Error(ResultCode::BackendUnavailable,
                                 "key derivation failed: " + derived.message);
        }
        s.secrets.clear();
        return Result::Ok();
    }

    std::vector<uint8_t> raw;
    uint8_t chunk[4096];
    size_t n;
    while ((n = std::fread(chunk, 1, sizeof(chunk), f)) > 0) {
        raw.insert(raw.end(), chunk, chunk + n);
    }
    std::fclose(f);

    Reader r{raw.data(), raw.size()};
    char magic[4] = {};
    if (!r.Take(magic, 4) || std::memcmp(magic, kMagic, 4) != 0 ||
        r.U8() != kVersion) {
        return Result::Error(ResultCode::AccessDenied,
                             "not a readable vault file: " + s.filePath);
    }
    UltraCryptKdfParams kdf;
    kdf.iterations = r.U32();
    kdf.memoryKiB  = r.U32();
    const uint8_t saltLen = r.U8();
    kdf.salt.resize(saltLen);
    r.Take(kdf.salt.data(), saltLen);
    const uint8_t nonceLen = r.U8();
    UltraCryptAeadParams aead;
    aead.nonce.resize(nonceLen);
    r.Take(aead.nonce.data(), nonceLen);
    if (!r.ok) {
        return Result::Error(ResultCode::AccessDenied,
                             "not a readable vault file: " + s.filePath);
    }
    const size_t headerSize = raw.size() - r.remaining;
    aead.associatedData.assign(raw.begin(),
                               raw.begin() + static_cast<long>(headerSize));

    UltraCryptResult derived =
        UltraCrypt_DeriveKeyFromPassword(passphrase, kdf, s.fileKey);
    if (!derived) {
        return Result::Error(ResultCode::BackendUnavailable,
                             "key derivation failed: " + derived.message);
    }

    UltraCryptSecureBuffer payload;
    UltraCryptResult opened =
        UltraCrypt_AeadOpen(s.fileKey, aead, r.p, r.remaining, payload);
    if (!opened) {
        s.fileKey.Clear();
        // Deliberately identical for wrong passphrase and tampered file.
        return Result::Error(ResultCode::AccessDenied,
                             "vault cannot be opened: wrong passphrase or "
                             "altered file");
    }
    if (!DeserializeSecrets(payload.Data(), payload.GetSize(), s.secrets)) {
        s.fileKey.Clear();
        return Result::Error(ResultCode::AccessDenied,
                             "vault cannot be opened: wrong passphrase or "
                             "altered file");
    }
    s.kdfParams = kdf;
    return Result::Ok();
}

Result RequireOpenLocked(const VaultState& s) {
    if (!s.open) {
        return Result::Error(ResultCode::Locked,
                             "UltraVault::Initialize() has not run");
    }
    return Result::Ok();
}

} // namespace

// ==========================================================================
// Lifecycle
// ==========================================================================
Result Initialize(Config& config) {
    VaultState& s = State();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (s.open) return Result::Ok();

    Backend backend = config.backend;
    if (backend == Backend::Auto) {
        backend = config.filePath.empty() ? Backend::Memory : Backend::File;
    }

    if (backend == Backend::File) {
        if (config.filePath.empty()) {
            return Result::Error(ResultCode::InvalidKey,
                                 "file backend needs Config::filePath");
        }
        if (!UltraCrypt_IsAvailable()) {
            UltraCrypt_Initialize();
        }
        if (!UltraCrypt_IsAvailable()) {
            return Result::Error(ResultCode::BackendUnavailable,
                                 "UltraCrypt backend unavailable (built "
                                 "without libsodium?)");
        }
        UltraCryptSecureBuffer passphrase =
            UltraCryptSecureBuffer::AdoptString(config.passphrase);
        if (passphrase.IsEmpty()) {
            return Result::Error(ResultCode::AccessDenied,
                                 "file backend needs Config::passphrase");
        }
        s.filePath = config.filePath;
        Result loaded = LoadFileLocked(s, passphrase);
        if (!loaded.IsOk()) {
            s.secrets.clear();
            s.filePath.clear();
            return loaded;
        }
        s.backend = Backend::File;
        s.open = true;
        return Result::Ok();
    }

    s.backend = Backend::Memory;
    s.secrets.clear();
    s.open = true;
    return Result::Ok();
}

Result Initialize() {
    Config config;
    const char* file = std::getenv("ULTRAVAULT_FILE");
    const char* pass = std::getenv("ULTRAVAULT_PASSPHRASE");
    if (file && *file && pass && *pass) {
        config.backend    = Backend::File;
        config.filePath   = file;
        config.passphrase = pass;
    }
    return Initialize(config);
}

void Shutdown() {
    VaultState& s = State();
    std::lock_guard<std::mutex> lock(s.mutex);
    for (auto& [key, stored] : s.secrets) {
        if (!stored.value.bytes.empty()) {
            UltraCrypt_SecureZero(stored.value.bytes.data(),
                                  stored.value.bytes.size());
        }
    }
    s.secrets.clear();
    s.fileKey.Clear();
    s.filePath.clear();
    s.open = false;
}

bool IsAvailable() {
    VaultState& s = State();
    std::lock_guard<std::mutex> lock(s.mutex);
    return s.open;
}

std::string GetBackendName() {
    VaultState& s = State();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!s.open) return {};
    return s.backend == Backend::File ? "file" : "memory";
}

// ==========================================================================
// Core operations
// ==========================================================================
Result Put(const std::string& key, const SecretValue& value,
           const SecretAcl& acl) {
    VaultState& s = State();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (Result r = RequireOpenLocked(s); !r.IsOk()) return r;
    if (!IsValidKey(key)) {
        return Result::Error(ResultCode::InvalidKey,
                             "secret keys are non-empty printable ASCII "
                             "without whitespace, e.g. ai.anthropic.api_key");
    }

    StoredSecret previous;
    const auto it = s.secrets.find(key);
    const bool had = it != s.secrets.end();
    if (had) previous = it->second;

    s.secrets[key] = StoredSecret{value, acl};
    if (s.backend == Backend::File) {
        if (Result saved = SaveFileLocked(s); !saved.IsOk()) {
            if (had) s.secrets[key] = std::move(previous);
            else     s.secrets.erase(key);
            return saved;
        }
    }
    return Result::Ok();
}

Result Get(const std::string& key, SecretValue& outValue) {
    VaultState& s = State();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (Result r = RequireOpenLocked(s); !r.IsOk()) return r;
    if (!IsValidKey(key)) {
        return Result::Error(ResultCode::InvalidKey, "malformed secret key");
    }
    const auto it = s.secrets.find(key);
    if (it == s.secrets.end()) {
        return Result::Error(ResultCode::NotFound,
                             "no secret stored under '" + key + "'");
    }
    outValue = it->second.value;
    return Result::Ok();
}

Result Delete(const std::string& key) {
    VaultState& s = State();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (Result r = RequireOpenLocked(s); !r.IsOk()) return r;
    auto it = s.secrets.find(key);
    if (it == s.secrets.end()) {
        return Result::Error(ResultCode::NotFound,
                             "no secret stored under '" + key + "'");
    }
    StoredSecret removed = std::move(it->second);
    s.secrets.erase(it);
    if (s.backend == Backend::File) {
        if (Result saved = SaveFileLocked(s); !saved.IsOk()) {
            s.secrets.emplace(key, std::move(removed));
            return saved;
        }
    }
    if (!removed.value.bytes.empty()) {
        UltraCrypt_SecureZero(removed.value.bytes.data(),
                              removed.value.bytes.size());
    }
    return Result::Ok();
}

std::vector<std::string> List(const std::string& prefix) {
    VaultState& s = State();
    std::lock_guard<std::mutex> lock(s.mutex);
    std::vector<std::string> keys;
    if (!s.open) return keys;
    for (const auto& [key, stored] : s.secrets) {
        (void)stored;
        if (key.compare(0, prefix.size(), prefix) == 0) keys.push_back(key);
    }
    return keys;   // std::map iteration is already sorted
}

// ==========================================================================
// Bootstrap / migration — v0.1 stubs
// ==========================================================================
Result Import(const std::string& sourcePath) {
    (void)sourcePath;
    return Result::Error(ResultCode::Unknown,
                         "UltraVault::Import is not implemented in v0.1");
}

Result PromptUserForSecret(const std::string& key,
                           const std::string& displayPrompt) {
    (void)key;
    (void)displayPrompt;
    return Result::Error(ResultCode::Unknown,
                         "UltraVault::PromptUserForSecret is not implemented "
                         "in v0.1 (headless builds have no prompt UI)");
}

} // namespace UltraVault
