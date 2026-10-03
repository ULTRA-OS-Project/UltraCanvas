// Apps/UltraPassword/core/VaultFile.cpp
// Container layout (all integers little-endian):
//
//   offset size field
//   0      8    magic "UPWVAULT"
//   8      1    container version (1)
//   9      1    KDF id   (1 = Argon2id)
//   10     1    AEAD id  (1 = XChaCha20-Poly1305)
//   11     1    reserved (0)
//   12     4    Argon2id passes
//   16     4    Argon2id memory cost, KiB
//   20     16   Argon2id salt
//   36     24   AEAD nonce
//   60     ...  ciphertext || 16-byte Poly1305 tag
//
// The whole 60-byte header is the AEAD associated data. The plaintext is the
// payload PasswordVault::Serialize writes.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "VaultFile.h"

#include "UltraCanvasPathUtf8.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace UltraPassword {
namespace {

constexpr size_t  kMagicSize   = 8;
constexpr size_t  kSaltSize    = 16;
constexpr size_t  kNonceSize   = 24;
constexpr size_t  kTagSize     = 16;
constexpr size_t  kHeaderSize  = 60;
constexpr uint8_t kVersion     = 1;
constexpr uint8_t kKdfArgon2id = 1;
constexpr uint8_t kAeadXChaCha = 1;
const char kMagic[kMagicSize] = {'U', 'P', 'W', 'V', 'A', 'U', 'L', 'T'};

constexpr size_t kOffVersion    = 8;
constexpr size_t kOffKdfId      = 9;
constexpr size_t kOffAeadId     = 10;
constexpr size_t kOffIterations = 12;
constexpr size_t kOffMemoryKiB  = 16;
constexpr size_t kOffSalt       = 20;
constexpr size_t kOffNonce      = 36;

// A hostile file must not make the reader allocate wildly or spin for hours
// before the tag check can reject it.
constexpr uint32_t kMaxMemoryKiB  = 4u * 1024u * 1024u;   // 4 GiB
constexpr uint32_t kMaxIterations = 64;
constexpr size_t   kMaxFileBytes  = 64u * 1024u * 1024u;

void PutU32(uint8_t* p, uint32_t v) {
    p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); p[2] = uint8_t(v >> 16); p[3] = uint8_t(v >> 24);
}

uint32_t GetU32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

VaultResult AuthFailure() {
    return VaultResult::Fail(VaultError::AuthenticationFailed,
        "The vault could not be unlocked. The password may be wrong, or the "
        "file may have been altered.");
}

VaultResult KdfFailure(const UltraCryptResult& r) {
    if (r.code == UltraCryptResultCode::BackendUnavailable)
        return VaultResult::Fail(VaultError::BackendUnavailable,
                                 "This build has no cryptography backend.");
    return VaultResult::Fail(VaultError::OutOfMemory,
        "The key could not be derived from the password - the computer may not "
        "have enough free memory for the vault's Argon2id cost. (" + r.message + ")");
}

bool ReadWholeFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream file(UltraCanvas::PathFromUtf8(path), std::ios::binary | std::ios::ate);
    if (!file) return false;
    const std::streamoff size = file.tellg();
    if (size < 0 || static_cast<size_t>(size) > kMaxFileBytes) return false;
    out.resize(static_cast<size_t>(size));
    file.seekg(0);
    if (size > 0 && !file.read(reinterpret_cast<char*>(out.data()), size)) return false;
    return true;
}

// Write a sibling temporary file, flush, restrict it to the owner, then rename
// it over the target, so an interrupted save leaves the previous file intact.
bool WriteFileAtomically(const std::string& path, const std::vector<uint8_t>& data) {
    namespace fs = std::filesystem;
    const fs::path target = UltraCanvas::PathFromUtf8(path);
    fs::path temporary = target;
    temporary += ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) return false;
        if (!data.empty())
            file.write(reinterpret_cast<const char*>(data.data()),
                       static_cast<std::streamsize>(data.size()));
        file.flush();
        if (!file) {
            file.close();
            std::error_code ignored;
            fs::remove(temporary, ignored);
            return false;
        }
    }
    std::error_code ec;
    // A filesystem without POSIX modes is no reason to fail: the encryption,
    // not the mode bits, protects the contents.
    fs::permissions(temporary, fs::perms::owner_read | fs::perms::owner_write,
                    fs::perm_options::replace, ec);
    ec.clear();
    fs::rename(temporary, target, ec);
    if (ec) {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        return false;
    }
    return true;
}

// Seals `contents` under `key` into a complete file image.
VaultResult SealImage(const UltraCryptSecureBuffer& key, const std::vector<uint8_t>& salt,
                      uint32_t iterations, uint32_t memoryKiB,
                      const PasswordVault& contents, std::vector<uint8_t>& image) {
    if (salt.size() != kSaltSize)
        return VaultResult::Fail(VaultError::InvalidArgument, "Invalid key salt.");

    std::vector<uint8_t> header(kHeaderSize, 0);
    std::memcpy(header.data(), kMagic, kMagicSize);
    header[kOffVersion] = kVersion;
    header[kOffKdfId]   = kKdfArgon2id;
    header[kOffAeadId]  = kAeadXChaCha;
    PutU32(header.data() + kOffIterations, iterations);
    PutU32(header.data() + kOffMemoryKiB, memoryKiB);
    std::memcpy(header.data() + kOffSalt, salt.data(), kSaltSize);

    UltraCryptAeadParams params;
    params.algorithm = UltraCryptAeadAlgorithm::XChaCha20Poly1305;
    if (!UltraCrypt_RandomBytes(params.nonce, kNonceSize))
        return VaultResult::Fail(VaultError::IoError, "The system random generator failed.");
    std::memcpy(header.data() + kOffNonce, params.nonce.data(), kNonceSize);
    params.associatedData = header;

    UltraCryptSecureBuffer plain;
    contents.Serialize(plain);

    std::vector<uint8_t> cipher;
    UltraCryptResult sealed = UltraCrypt_AeadSeal(key, params, plain.Data(), plain.GetSize(), cipher);
    if (!sealed)
        return VaultResult::Fail(VaultError::IoError, "Encryption failed: " + sealed.message);

    image = std::move(header);
    image.insert(image.end(), cipher.begin(), cipher.end());
    return VaultResult::Success();
}

// Parses a file image, derives the key from `password` and decrypts.
VaultResult OpenImage(const std::vector<uint8_t>& image, const UltraCryptSecureBuffer& password,
                      UltraCryptSecureBuffer& outKey, std::vector<uint8_t>& outSalt,
                      uint32_t& outIterations, uint32_t& outMemoryKiB,
                      PasswordVault& out) {
    if (image.size() < kHeaderSize + kTagSize || std::memcmp(image.data(), kMagic, kMagicSize) != 0)
        return VaultResult::Fail(VaultError::NotAVault, "This is not an UltraPassword vault file.");
    if (image[kOffVersion] != kVersion || image[kOffKdfId] != kKdfArgon2id ||
        image[kOffAeadId] != kAeadXChaCha)
        return VaultResult::Fail(VaultError::UnsupportedVersion,
            "This vault was written by a newer UltraPassword. Update the app to open it.");

    const uint32_t iterations = GetU32(image.data() + kOffIterations);
    const uint32_t memoryKiB  = GetU32(image.data() + kOffMemoryKiB);
    if (iterations == 0 || iterations > kMaxIterations || memoryKiB == 0 || memoryKiB > kMaxMemoryKiB)
        return AuthFailure();   // an absurd cost is tampering, not a format

    UltraCryptKdfParams kdf;
    kdf.algorithm  = UltraCryptKdfAlgorithm::Argon2id;
    kdf.iterations = iterations;
    kdf.memoryKiB  = memoryKiB;
    kdf.salt.assign(image.begin() + kOffSalt, image.begin() + kOffSalt + kSaltSize);
    kdf.outputLength = UltraCrypt_GetKeySize(UltraCryptAeadAlgorithm::XChaCha20Poly1305);

    UltraCryptSecureBuffer key;
    UltraCryptResult derived = UltraCrypt_DeriveKeyFromPassword(password, kdf, key);
    if (!derived) return KdfFailure(derived);
    key.LockPages();

    UltraCryptAeadParams params;
    params.algorithm = UltraCryptAeadAlgorithm::XChaCha20Poly1305;
    params.nonce.assign(image.begin() + kOffNonce, image.begin() + kOffNonce + kNonceSize);
    params.associatedData.assign(image.begin(), image.begin() + kHeaderSize);

    UltraCryptSecureBuffer plain;
    UltraCryptResult opened = UltraCrypt_AeadOpen(key, params, image.data() + kHeaderSize,
                                                  image.size() - kHeaderSize, plain);
    if (!opened) return AuthFailure();

    PasswordVault decoded;
    if (!decoded.Deserialize(plain))
        return VaultResult::Fail(VaultError::NotAVault,
                                 "The vault decrypted, but its contents are damaged.");

    out = std::move(decoded);
    outKey = std::move(key);
    outSalt = std::move(kdf.salt);
    outIterations = iterations;
    outMemoryKiB = memoryKiB;
    return VaultResult::Success();
}

bool SamePath(const std::string& a, const std::string& b) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (fs::exists(UltraCanvas::PathFromUtf8(a), ec) && fs::exists(UltraCanvas::PathFromUtf8(b), ec))
        return fs::equivalent(UltraCanvas::PathFromUtf8(a), UltraCanvas::PathFromUtf8(b), ec);
    return fs::weakly_canonical(UltraCanvas::PathFromUtf8(a), ec) ==
           fs::weakly_canonical(UltraCanvas::PathFromUtf8(b), ec);
}

} // namespace

// ===== PROFILES =====
UltraCryptKdfParams KdfParamsFor(KdfProfile profile) {
    UltraCryptKdfParams p;
    p.algorithm   = UltraCryptKdfAlgorithm::Argon2id;
    p.parallelism = 1;
    p.outputLength = 32;
    switch (profile) {
        case KdfProfile::Maximum: p.iterations = 4; p.memoryKiB = 1024 * 1024; break;
        case KdfProfile::Strong:  p.iterations = 4; p.memoryKiB = 256 * 1024;  break;
    }
    return p;
}

const char* KdfProfileName(KdfProfile profile) {
    switch (profile) {
        case KdfProfile::Maximum: return "Maximum - Argon2id, 1 GiB, 4 passes";
        case KdfProfile::Strong:  return "Strong - Argon2id, 256 MiB, 4 passes";
    }
    return "";
}

// ===== VAULT FILE =====
bool VaultFile::Exists(const std::string& path) {
    std::error_code ec;
    return std::filesystem::exists(UltraCanvas::PathFromUtf8(path), ec) && !ec;
}

void VaultFile::Close() {
    key_.Clear();
    if (!salt_.empty()) UltraCrypt_SecureZero(salt_.data(), salt_.size());
    salt_.clear();
    iterations_ = 0;
    memoryKiB_ = 0;
    path_.clear();
    isOpen_ = false;
}

VaultResult VaultFile::Create(const std::string& path, const UltraCryptSecureBuffer& password,
                              const PasswordVault& contents, const UltraCryptKdfParams& cost) {
    Close();
    if (!UltraCrypt_IsAvailable())
        return VaultResult::Fail(VaultError::BackendUnavailable,
            "This build has no cryptography backend, so no vault can be created.");
    if (password.IsEmpty())
        return VaultResult::Fail(VaultError::InvalidArgument, "A master password is required.");
    if (Exists(path))
        return VaultResult::Fail(VaultError::AlreadyExists, "A vault already exists at that location.");

    UltraCryptKdfParams kdf = cost;
    kdf.salt.clear();   // always a fresh salt
    kdf.outputLength = UltraCrypt_GetKeySize(UltraCryptAeadAlgorithm::XChaCha20Poly1305);
    UltraCryptSecureBuffer key;
    UltraCryptResult derived = UltraCrypt_DeriveKeyFromPassword(password, kdf, key);
    if (!derived) return KdfFailure(derived);
    key.LockPages();

    std::vector<uint8_t> image;
    VaultResult sealed = SealImage(key, kdf.salt, kdf.iterations, kdf.memoryKiB, contents, image);
    if (!sealed) return sealed;

    std::error_code ec;
    std::filesystem::create_directories(UltraCanvas::PathFromUtf8(path).parent_path(), ec);
    if (!WriteFileAtomically(path, image))
        return VaultResult::Fail(VaultError::IoError, "The vault file could not be written.");

    path_ = path;
    key_ = std::move(key);
    salt_ = std::move(kdf.salt);
    iterations_ = kdf.iterations;
    memoryKiB_ = kdf.memoryKiB;
    isOpen_ = true;
    return VaultResult::Success();
}

VaultResult VaultFile::Open(const std::string& path, const UltraCryptSecureBuffer& password,
                            PasswordVault& out) {
    Close();
    if (!UltraCrypt_IsAvailable())
        return VaultResult::Fail(VaultError::BackendUnavailable,
                                 "This build has no cryptography backend.");
    if (password.IsEmpty())
        return VaultResult::Fail(VaultError::InvalidArgument, "Enter the master password.");

    std::vector<uint8_t> image;
    if (!Exists(path))
        return VaultResult::Fail(VaultError::NotFound, "The vault file does not exist.");
    if (!ReadWholeFile(path, image))
        return VaultResult::Fail(VaultError::IoError, "The vault file could not be read.");

    VaultResult opened = OpenImage(image, password, key_, salt_, iterations_, memoryKiB_, out);
    if (!opened) { Close(); return opened; }
    path_ = path;
    isOpen_ = true;
    return VaultResult::Success();
}

VaultResult VaultFile::Save(const PasswordVault& contents) {
    if (!isOpen_) return VaultResult::Fail(VaultError::InvalidArgument, "No vault is open.");
    std::vector<uint8_t> image;
    VaultResult sealed = SealImage(key_, salt_, iterations_, memoryKiB_, contents, image);
    if (!sealed) return sealed;
    if (!WriteFileAtomically(path_, image))
        return VaultResult::Fail(VaultError::IoError, "The vault file could not be saved.");
    return VaultResult::Success();
}

VaultResult VaultFile::ChangePassword(const UltraCryptSecureBuffer& newPassword,
                                      const PasswordVault& contents,
                                      const UltraCryptKdfParams* cost) {
    if (!isOpen_) return VaultResult::Fail(VaultError::InvalidArgument, "No vault is open.");
    if (newPassword.IsEmpty())
        return VaultResult::Fail(VaultError::InvalidArgument, "A master password is required.");

    UltraCryptKdfParams kdf;
    if (cost) {
        kdf = *cost;
    } else {
        kdf.algorithm = UltraCryptKdfAlgorithm::Argon2id;
        kdf.iterations = iterations_;
        kdf.memoryKiB = memoryKiB_;
    }
    kdf.salt.clear();
    kdf.outputLength = UltraCrypt_GetKeySize(UltraCryptAeadAlgorithm::XChaCha20Poly1305);
    UltraCryptSecureBuffer key;
    UltraCryptResult derived = UltraCrypt_DeriveKeyFromPassword(newPassword, kdf, key);
    if (!derived) return KdfFailure(derived);
    key.LockPages();

    std::vector<uint8_t> image;
    VaultResult sealed = SealImage(key, kdf.salt, kdf.iterations, kdf.memoryKiB, contents, image);
    if (!sealed) return sealed;
    if (!WriteFileAtomically(path_, image))
        return VaultResult::Fail(VaultError::IoError, "The vault file could not be saved.");

    key_ = std::move(key);
    UltraCrypt_SecureZero(salt_.data(), salt_.size());
    salt_ = std::move(kdf.salt);
    iterations_ = kdf.iterations;
    memoryKiB_ = kdf.memoryKiB;
    return VaultResult::Success();
}

VaultResult VaultFile::ExportCopy(const std::string& destination, const PasswordVault& contents) const {
    if (!isOpen_) return VaultResult::Fail(VaultError::InvalidArgument, "No vault is open.");
    if (SamePath(destination, path_))
        return VaultResult::Fail(VaultError::InvalidArgument,
                                 "Choose a different file - that is the open vault itself.");
    std::vector<uint8_t> image;
    VaultResult sealed = SealImage(key_, salt_, iterations_, memoryKiB_, contents, image);
    if (!sealed) return sealed;
    if (!WriteFileAtomically(destination, image))
        return VaultResult::Fail(VaultError::IoError, "The export file could not be written.");
    return VaultResult::Success();
}

VaultResult VaultFile::ExportWithPassword(const std::string& destination,
                                          const UltraCryptSecureBuffer& password,
                                          const PasswordVault& contents,
                                          const UltraCryptKdfParams& cost) {
    if (!UltraCrypt_IsAvailable())
        return VaultResult::Fail(VaultError::BackendUnavailable,
                                 "This build has no cryptography backend.");
    if (password.IsEmpty())
        return VaultResult::Fail(VaultError::InvalidArgument, "An export password is required.");
    UltraCryptKdfParams kdf = cost;
    kdf.salt.clear();
    kdf.outputLength = UltraCrypt_GetKeySize(UltraCryptAeadAlgorithm::XChaCha20Poly1305);
    UltraCryptSecureBuffer key;
    UltraCryptResult derived = UltraCrypt_DeriveKeyFromPassword(password, kdf, key);
    if (!derived) return KdfFailure(derived);

    std::vector<uint8_t> image;
    VaultResult sealed = SealImage(key, kdf.salt, kdf.iterations, kdf.memoryKiB, contents, image);
    if (!sealed) return sealed;
    if (!WriteFileAtomically(destination, image))
        return VaultResult::Fail(VaultError::IoError, "The export file could not be written.");
    return VaultResult::Success();
}

VaultResult VaultFile::ReadFile(const std::string& path, const UltraCryptSecureBuffer& password,
                                PasswordVault& out) {
    if (!UltraCrypt_IsAvailable())
        return VaultResult::Fail(VaultError::BackendUnavailable,
                                 "This build has no cryptography backend.");
    std::vector<uint8_t> image;
    if (!ReadWholeFile(path, image))
        return VaultResult::Fail(VaultError::IoError, "The file could not be read.");
    UltraCryptSecureBuffer key;
    std::vector<uint8_t> salt;
    uint32_t iterations = 0, memoryKiB = 0;
    return OpenImage(image, password, key, salt, iterations, memoryKiB, out);
}

// ===== THROTTLE =====
uint32_t UnlockThrottle::SecondsToWait(int64_t nowSeconds) const {
    return notBefore_ > nowSeconds ? static_cast<uint32_t>(notBefore_ - nowSeconds) : 0;
}

void UnlockThrottle::RecordFailure(int64_t nowSeconds) {
    ++failures_;
    if (failures_ < 3) return;   // typos are free
    int64_t wait = 1;
    for (int i = 3; i < failures_ && wait < 60; ++i) wait *= 2;
    if (wait > 60) wait = 60;
    notBefore_ = nowSeconds + wait;
}

} // namespace UltraPassword
