// Apps/UltraAuthenticator/store/EncryptedFileStore.cpp
// Implementation of the encrypted vault. See the header for the design and the
// threat notes.
//
// Container layout, version 2 (all integers little-endian):
//
//   offset size field
//   0      8    magic "UCAVAULT"
//   8      1    container version (2)
//   9      1    KDF id      (1 = Argon2id)
//   10     1    AEAD id     (1 = XChaCha20-Poly1305)
//   11     1    flags       (bit 0: a recovery slot follows)
//   12     4    Argon2id passes
//   16     4    Argon2id memory cost, KiB
//   20     16   Argon2id salt
//   36     24   password slot nonce
//   60     48   password slot: the data key sealed with the password key,
//               bytes 0-35 as associated data
//   108         when flags bit 0 - the recovery slot:
//          32     the local share
//          24     recovery slot nonce
//          48     the data key sealed with the recovery key, associated
//                 data kRecoveryContext || local share || escrow id
//          1+n    escrow id (n <= 64; 0 for a recovery code only)
//          2+m    account service URL (m <= 512)
//   ...    24   entries nonce
//   ...    ...  entries ciphertext || 16-byte tag
//
// Everything before the entries' ciphertext is their associated data, so an
// edit anywhere in the header - the cost, a slot, the escrow id - fails the
// open. The recovery key is HKDF-SHA-256 over server share || local share,
// with kRecoveryContext as the info: 32 + 32 random bytes, so no password
// stretching is needed or wanted.
//
// Version 1, still read: the same first 20 bytes with version 1 and a
// reserved byte at 11, the salt, then the 24-byte entries nonce at 36 and the
// ciphertext at 60, sealed with the password key itself; the 60-byte header
// is the associated data.
//
// Decrypted payload:
//   4 bytes   entry count
//   per entry: 2-byte key length, key, 4-byte value length, value
//
// The payload is a plain binary encoding rather than JSON on purpose:
// UltraCanvasJSON's JSONValue is value-semantic and copies freely, so routing
// seeds through it would scatter plaintext copies across the heap
// (UltraAuthenticator-Investigation.md §3.2).
//
// Version: 0.2.0 - container version 2: the data key, the password slot, the recovery slot
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "EncryptedFileStore.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>
#include "../../../UltraCanvas/include/UltraCanvasPathUtf8.h"

namespace UltraCanvas {
namespace Authenticator {
namespace {

constexpr size_t  kMagicSize   = 8;
constexpr size_t  kSaltSize    = 16;
constexpr size_t  kNonceSize   = 24;
constexpr size_t  kHeaderSize  = 60;     // version 1
constexpr uint8_t kVersion1    = 1;
constexpr uint8_t kVersion     = 2;
constexpr size_t  kKeyBytes    = 32;
constexpr size_t  kTagBytes    = 16;
constexpr size_t  kSlotBytes   = kKeyBytes + kTagBytes;
constexpr size_t  kFixedHeaderV2 = 108;
constexpr uint8_t kFlagRecovery  = 0x01;
const char kRecoveryContext[] = "UltraAuthenticator vault recovery v1";
constexpr uint8_t kKdfArgon2id = 1;
constexpr uint8_t kAeadXChaCha = 1;
const char kMagic[kMagicSize] = {'U', 'C', 'A', 'V', 'A', 'U', 'L', 'T'};

constexpr size_t kOffVersion    = 8;
constexpr size_t kOffKdfId      = 9;
constexpr size_t kOffAeadId     = 10;
constexpr size_t kOffIterations = 12;
constexpr size_t kOffMemoryKiB  = 16;
constexpr size_t kOffSalt       = 20;
constexpr size_t kOffNonce      = 36;   // version 1: the entries nonce; version 2: the password slot's
constexpr size_t kOffFlags      = 11;
constexpr size_t kOffSlot       = 60;

// Guard rails on parsed cost parameters: a hostile file must not be able to
// make us try to allocate terabytes before the tag check rejects it.
constexpr uint32_t kMaxMemoryKiB  = 4u * 1024u * 1024u;   // 4 GiB
constexpr uint32_t kMaxIterations = 64;

void PutUint16LE(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void PutUint32LE(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

uint16_t ReadUint16LE(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

uint32_t ReadUint32LE(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

// One undifferentiated failure for a wrong password and a modified file.
StoreResult AuthFailure() {
    return StoreResult::Error(
        StoreResultCode::AuthenticationFailed,
        "could not unlock the vault — the password may be incorrect, or the "
        "file may have been altered");
}

bool ReadWholeFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream file(UltraCanvas::PathFromUtf8(path), std::ios::binary | std::ios::ate);
    if (!file) return false;
    const std::streamoff size = file.tellg();
    if (size < 0 || static_cast<size_t>(size) > kMaxStoreFileBytes) return false;
    out.resize(static_cast<size_t>(size));
    file.seekg(0);
    if (size > 0 && !file.read(reinterpret_cast<char*>(out.data()), size)) return false;
    return true;
}

// Write to a sibling temporary file, flush it, then rename over the vault, so
// an interrupted save leaves the previous vault intact rather than a truncated
// one. Losing a vault of second factors is not a recoverable error for a user.
bool WriteFileAtomically(const std::string& path, const std::vector<uint8_t>& data) {
    const std::string temporary = path + ".tmp";
    {
        std::ofstream file(UltraCanvas::PathFromUtf8(temporary), std::ios::binary | std::ios::trunc);
        if (!file) return false;
        if (!data.empty()) {
            file.write(reinterpret_cast<const char*>(data.data()),
                       static_cast<std::streamsize>(data.size()));
        }
        file.flush();
        if (!file) { std::remove(temporary.c_str()); return false; }
    }

    std::error_code ec;
    std::filesystem::permissions(
        UltraCanvas::PathFromUtf8(temporary),
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
        std::filesystem::perm_options::replace, ec);
    // A filesystem without POSIX permissions is not a reason to fail the save;
    // the encryption, not the mode bits, is what protects the contents.

    std::filesystem::rename(UltraCanvas::PathFromUtf8(temporary), UltraCanvas::PathFromUtf8(path), ec);
    if (ec) {
        std::remove(temporary.c_str());
        return false;
    }
    return true;
}

std::vector<uint8_t> RecoveryAssociatedData(const std::vector<uint8_t>& localShare,
                                            const std::string& escrowId) {
    std::vector<uint8_t> ad(kRecoveryContext, kRecoveryContext + sizeof(kRecoveryContext) - 1);
    ad.insert(ad.end(), localShare.begin(), localShare.end());
    ad.insert(ad.end(), escrowId.begin(), escrowId.end());
    return ad;
}

UltraCryptResult DeriveRecoveryKey(const UltraCryptSecureBuffer& serverShare,
                                   const std::vector<uint8_t>& localShare,
                                   UltraCryptSecureBuffer& outKey) {
    UltraCryptSecureBuffer material(serverShare.GetSize() + localShare.size());
    std::memcpy(material.Data(), serverShare.Data(), serverShare.GetSize());
    std::memcpy(material.Data() + serverShare.GetSize(), localShare.data(), localShare.size());
    const std::vector<uint8_t> info(kRecoveryContext, kRecoveryContext + sizeof(kRecoveryContext) - 1);
    return UltraCrypt_DeriveKeyHkdf(UltraCryptHashAlgorithm::SHA256, material, {}, info,
                                    kKeyBytes, outKey);
}

} // namespace

StoreResult StoreResult::Ok() {
    StoreResult r;
    r.code = StoreResultCode::Ok;
    r.success = true;
    return r;
}

StoreResult StoreResult::Error(StoreResultCode code, const std::string& message) {
    StoreResult r;
    r.code = code;
    r.success = false;
    r.message = message;
    return r;
}

EncryptedFileStore::~EncryptedFileStore() {
    Close();
}

bool EncryptedFileStore::Exists(const std::string& path) {
    std::error_code ec;
    return std::filesystem::exists(UltraCanvas::PathFromUtf8(path), ec) && !ec;
}

void EncryptedFileStore::Close() {
    passwordKey_.Clear();
    dataKey_.Clear();
    containerVersion_ = kVersion;
    recovery_ = VaultRecoveryInfo{};
    recoveryLocalShare_.clear();
    recoveryNonce_.clear();
    recoverySealedKey_.clear();
    for (auto& entry : entries_) entry.second.Clear();
    entries_.clear();
    UltraCrypt_SecureZero(salt_.data(), salt_.size());
    salt_.clear();
    kdfIterations_ = 0;
    kdfMemoryKiB_  = 0;
    path_.clear();
    isOpen_ = false;
}

StoreResult EncryptedFileStore::Create(const std::string& path,
                                       const UltraCryptSecureBuffer& password) {
    Close();

    if (!UltraCrypt_IsAvailable()) {
        return StoreResult::Error(
            StoreResultCode::BackendUnavailable,
            "this build has no cryptography backend, so an encrypted vault "
            "cannot be created");
    }
    if (password.IsEmpty()) {
        // Deliberate: there is no unprotected vault mode. See the header.
        return StoreResult::Error(StoreResultCode::InvalidArgument,
                                  "a master password is required");
    }
    if (Exists(path)) {
        return StoreResult::Error(StoreResultCode::AlreadyExists,
                                  "a vault already exists at that location");
    }

    UltraCryptKdfParams kdf = UltraCrypt_RecommendedKdfParams();
    auto derived = UltraCrypt_DeriveKeyFromPassword(password, kdf, passwordKey_);
    if (!derived) {
        Close();
        return StoreResult::Error(StoreResultCode::InternalError,
                                  "could not derive the vault key: " + derived.message);
    }
    auto drawn = UltraCrypt_RandomSecureBuffer(kKeyBytes, dataKey_);
    if (!drawn) {
        Close();
        return StoreResult::Error(StoreResultCode::InternalError,
                                  "could not draw the data key: " + drawn.message);
    }
    containerVersion_ = kVersion;

    path_          = path;
    salt_          = kdf.salt;
    kdfIterations_ = kdf.iterations;
    kdfMemoryKiB_  = kdf.memoryKiB;
    isOpen_        = true;

    auto saved = SaveLocked();
    if (!saved) { Close(); return saved; }
    return StoreResult::Ok();
}

StoreResult EncryptedFileStore::ParseFile(const std::string& path,
                                          std::vector<uint8_t>& raw,
                                          size_t& outBodyOffset) {
    // Reads and checks the container's structure and fills the header fields
    // that need no key: the cost, the salt, the recovery slot. Nothing here
    // is trusted until a tag check passes.
    if (!ReadWholeFile(path, raw)) {
        return StoreResult::Error(StoreResultCode::IoError,
                                  "could not read the vault file: " + path);
    }
    if (raw.size() <= kHeaderSize) {
        return StoreResult::Error(StoreResultCode::Corrupt,
                                  "the vault file is truncated");
    }
    if (std::memcmp(raw.data(), kMagic, kMagicSize) != 0) {
        return StoreResult::Error(StoreResultCode::Corrupt,
                                  "this is not an authenticator vault file");
    }
    const uint8_t version = raw[kOffVersion];
    if (version != kVersion && version != kVersion1) {
        return StoreResult::Error(
            StoreResultCode::Corrupt,
            "this vault was written by a newer version of the application");
    }
    if (raw[kOffKdfId] != kKdfArgon2id || raw[kOffAeadId] != kAeadXChaCha) {
        return StoreResult::Error(StoreResultCode::Corrupt,
                                  "the vault uses an unsupported algorithm");
    }
    kdfIterations_ = ReadUint32LE(raw.data() + kOffIterations);
    kdfMemoryKiB_  = ReadUint32LE(raw.data() + kOffMemoryKiB);
    salt_.assign(raw.begin() + kOffSalt, raw.begin() + kOffSalt + kSaltSize);
    if (kdfIterations_ == 0 || kdfIterations_ > kMaxIterations ||
        kdfMemoryKiB_ == 0 || kdfMemoryKiB_ > kMaxMemoryKiB) {
        return StoreResult::Error(
            StoreResultCode::Corrupt,
            "the vault declares unreasonable key-derivation parameters");
    }
    containerVersion_ = version;
    recovery_ = VaultRecoveryInfo{};
    recoveryLocalShare_.clear();
    recoveryNonce_.clear();
    recoverySealedKey_.clear();

    if (version == kVersion1) {
        outBodyOffset = kOffNonce;
        return StoreResult::Ok();
    }

    const StoreResult truncated = StoreResult::Error(StoreResultCode::Corrupt,
                                                     "the vault file is truncated");
    size_t offset = kFixedHeaderV2;
    if (raw.size() < offset) return truncated;
    const uint8_t flags = raw[kOffFlags];
    if ((flags & ~kFlagRecovery) != 0) {
        return StoreResult::Error(
            StoreResultCode::Corrupt,
            "this vault was written by a newer version of the application");
    }
    if (flags & kFlagRecovery) {
        if (raw.size() < offset + kRecoveryShareBytes + kNonceSize + kSlotBytes + 1) return truncated;
        recoveryLocalShare_.assign(raw.begin() + static_cast<std::ptrdiff_t>(offset),
                                   raw.begin() + static_cast<std::ptrdiff_t>(offset + kRecoveryShareBytes));
        offset += kRecoveryShareBytes;
        recoveryNonce_.assign(raw.begin() + static_cast<std::ptrdiff_t>(offset),
                              raw.begin() + static_cast<std::ptrdiff_t>(offset + kNonceSize));
        offset += kNonceSize;
        recoverySealedKey_.assign(raw.begin() + static_cast<std::ptrdiff_t>(offset),
                                  raw.begin() + static_cast<std::ptrdiff_t>(offset + kSlotBytes));
        offset += kSlotBytes;
        const size_t idLength = raw[offset++];
        if (idLength > kMaxRecoveryEscrowIdBytes || raw.size() < offset + idLength + 2) return truncated;
        recovery_.escrowId.assign(reinterpret_cast<const char*>(raw.data() + offset), idLength);
        offset += idLength;
        const size_t urlLength = ReadUint16LE(raw.data() + offset);
        offset += 2;
        if (urlLength > kMaxRecoveryServiceBytes || raw.size() < offset + urlLength) return truncated;
        recovery_.serviceUrl.assign(reinterpret_cast<const char*>(raw.data() + offset), urlLength);
        offset += urlLength;
        recovery_.enabled = true;
    }
    if (raw.size() <= offset + kNonceSize + kTagBytes) return truncated;
    outBodyOffset = offset;
    return StoreResult::Ok();
}

StoreResult EncryptedFileStore::DerivePasswordKey(const UltraCryptSecureBuffer& password,
                                                  UltraCryptKdfParams& kdf,
                                                  UltraCryptSecureBuffer& outKey) const {
    kdf.algorithm    = UltraCryptKdfAlgorithm::Argon2id;
    kdf.iterations   = kdfIterations_;
    kdf.memoryKiB    = kdfMemoryKiB_;
    kdf.parallelism  = 1;
    kdf.outputLength = UltraCrypt_GetKeySize(UltraCryptAeadAlgorithm::XChaCha20Poly1305);
    kdf.salt         = salt_;
    auto derived = UltraCrypt_DeriveKeyFromPassword(password, kdf, outKey);
    if (!derived) {
        return StoreResult::Error(StoreResultCode::InternalError,
                                  "could not derive the vault key: " + derived.message);
    }
    return StoreResult::Ok();
}

namespace {

// Opens the entries with `key`: the nonce at bodyOffset, everything before
// the ciphertext as associated data.
bool OpenEntries(const std::vector<uint8_t>& raw, size_t bodyOffset,
                 const UltraCryptSecureBuffer& key, UltraCryptSecureBuffer& outPlain) {
    UltraCryptAeadParams aead;
    aead.algorithm = UltraCryptAeadAlgorithm::XChaCha20Poly1305;
    aead.nonce.assign(raw.begin() + static_cast<std::ptrdiff_t>(bodyOffset),
                      raw.begin() + static_cast<std::ptrdiff_t>(bodyOffset + kNonceSize));
    const size_t cipherOffset = bodyOffset + kNonceSize;
    aead.associatedData.assign(raw.begin(), raw.begin() + static_cast<std::ptrdiff_t>(cipherOffset));
    return static_cast<bool>(UltraCrypt_AeadOpen(key, aead, raw.data() + cipherOffset,
                                                 raw.size() - cipherOffset, outPlain));
}

bool OpenSlot(const UltraCryptSecureBuffer& key, const std::vector<uint8_t>& nonce,
              const std::vector<uint8_t>& associatedData, const uint8_t* sealed,
              UltraCryptSecureBuffer& outDataKey) {
    UltraCryptAeadParams aead;
    aead.algorithm = UltraCryptAeadAlgorithm::XChaCha20Poly1305;
    aead.nonce = nonce;
    aead.associatedData = associatedData;
    if (!UltraCrypt_AeadOpen(key, aead, sealed, kSlotBytes, outDataKey)) return false;
    return outDataKey.GetSize() == kKeyBytes;
}

} // namespace

StoreResult EncryptedFileStore::Open(const std::string& path,
                                     const UltraCryptSecureBuffer& password) {
    Close();

    if (!UltraCrypt_IsAvailable()) {
        return StoreResult::Error(
            StoreResultCode::BackendUnavailable,
            "this build has no cryptography backend, so the vault cannot be "
            "opened");
    }
    if (password.IsEmpty()) {
        return StoreResult::Error(StoreResultCode::InvalidArgument,
                                  "a master password is required");
    }

    std::vector<uint8_t> raw;
    size_t bodyOffset = 0;
    auto parsed = ParseFile(path, raw, bodyOffset);
    if (!parsed) { Close(); return parsed; }

    UltraCryptKdfParams kdf;
    UltraCryptSecureBuffer passwordKey;
    auto derived = DerivePasswordKey(password, kdf, passwordKey);
    if (!derived) { Close(); return derived; }

    UltraCryptSecureBuffer dataKey;
    UltraCryptSecureBuffer plain;
    if (containerVersion_ == kVersion1) {
        // The password key sealed the entries itself. A data key is drawn for
        // the next write, which upgrades the file.
        if (!OpenEntries(raw, bodyOffset, passwordKey, plain)) { Close(); return AuthFailure(); }
        auto drawn = UltraCrypt_RandomSecureBuffer(kKeyBytes, dataKey);
        if (!drawn) {
            Close();
            return StoreResult::Error(StoreResultCode::InternalError,
                                      "could not draw the data key: " + drawn.message);
        }
    } else {
        const std::vector<uint8_t> nonce(raw.begin() + kOffNonce, raw.begin() + kOffNonce + kNonceSize);
        const std::vector<uint8_t> ad(raw.begin(), raw.begin() + kOffNonce);
        if (!OpenSlot(passwordKey, nonce, ad, raw.data() + kOffSlot, dataKey) ||
            !OpenEntries(raw, bodyOffset, dataKey, plain)) {
            Close();
            return AuthFailure();
        }
    }

    path_        = path;
    passwordKey_ = std::move(passwordKey);
    dataKey_     = std::move(dataKey);
    isOpen_      = true;

    auto read = DeserializeEntries(plain);
    if (!read) { Close(); return read; }
    return StoreResult::Ok();
}

StoreResult EncryptedFileStore::ReadRecoveryInfo(const std::string& path,
                                                 VaultRecoveryInfo& outInfo) {
    outInfo = VaultRecoveryInfo{};
    EncryptedFileStore probe;
    std::vector<uint8_t> raw;
    size_t bodyOffset = 0;
    auto parsed = probe.ParseFile(path, raw, bodyOffset);
    if (!parsed) return parsed;
    outInfo = probe.recovery_;
    return StoreResult::Ok();
}

StoreResult EncryptedFileStore::OpenWithRecovery(const std::string& path,
                                                 const UltraCryptSecureBuffer& serverShare,
                                                 const UltraCryptSecureBuffer& newPassword) {
    Close();
    if (!UltraCrypt_IsAvailable()) {
        return StoreResult::Error(StoreResultCode::BackendUnavailable,
                                  "this build has no cryptography backend, so the vault cannot be opened");
    }
    if (newPassword.IsEmpty()) {
        return StoreResult::Error(StoreResultCode::InvalidArgument,
                                  "a new master password is required");
    }
    if (serverShare.GetSize() != kRecoveryShareBytes) {
        return StoreResult::Error(StoreResultCode::InvalidArgument,
                                  "a recovery key is 32 bytes");
    }

    std::vector<uint8_t> raw;
    size_t bodyOffset = 0;
    auto parsed = ParseFile(path, raw, bodyOffset);
    if (!parsed) { Close(); return parsed; }
    if (!recovery_.enabled) {
        Close();
        return StoreResult::Error(StoreResultCode::NotFound,
                                  "recovery is not turned on for this vault");
    }

    UltraCryptSecureBuffer recoveryKey;
    auto derived = DeriveRecoveryKey(serverShare, recoveryLocalShare_, recoveryKey);
    if (!derived) {
        Close();
        return StoreResult::Error(StoreResultCode::InternalError,
                                  "could not derive the recovery key: " + derived.message);
    }
    UltraCryptSecureBuffer dataKey;
    UltraCryptSecureBuffer plain;
    if (!OpenSlot(recoveryKey, recoveryNonce_,
                  RecoveryAssociatedData(recoveryLocalShare_, recovery_.escrowId),
                  recoverySealedKey_.data(), dataKey) ||
        !OpenEntries(raw, bodyOffset, dataKey, plain)) {
        Close();
        return AuthFailure();
    }

    path_    = path;
    dataKey_ = std::move(dataKey);
    isOpen_  = true;
    auto read = DeserializeEntries(plain);
    if (!read) { Close(); return read; }

    // The share has been out of its safe place: this slot is spent.
    recovery_ = VaultRecoveryInfo{};
    recoveryLocalShare_.clear();
    recoveryNonce_.clear();
    recoverySealedKey_.clear();

    UltraCryptKdfParams kdf = UltraCrypt_RecommendedKdfParams();
    UltraCryptSecureBuffer newKey;
    auto stretched = UltraCrypt_DeriveKeyFromPassword(newPassword, kdf, newKey);
    if (!stretched) {
        Close();
        return StoreResult::Error(StoreResultCode::InternalError,
                                  "could not derive the new key: " + stretched.message);
    }
    passwordKey_   = std::move(newKey);
    salt_          = kdf.salt;
    kdfIterations_ = kdf.iterations;
    kdfMemoryKiB_  = kdf.memoryKiB;

    auto saved = SaveLocked();
    if (!saved) { Close(); return saved; }
    return StoreResult::Ok();
}

VaultRecoveryInfo EncryptedFileStore::GetRecoveryInfo() const {
    return isOpen_ ? recovery_ : VaultRecoveryInfo{};
}

StoreResult EncryptedFileStore::EnableRecovery(const UltraCryptSecureBuffer& serverShare,
                                               const std::string& escrowId,
                                               const std::string& serviceUrl) {
    if (!isOpen_) {
        return StoreResult::Error(StoreResultCode::NotOpen, "no vault is open");
    }
    if (serverShare.GetSize() != kRecoveryShareBytes) {
        return StoreResult::Error(StoreResultCode::InvalidArgument,
                                  "a recovery key is 32 bytes");
    }
    if (escrowId.size() > kMaxRecoveryEscrowIdBytes || serviceUrl.size() > kMaxRecoveryServiceBytes) {
        return StoreResult::Error(StoreResultCode::InvalidArgument,
                                  "the recovery key's id or the service address is too long");
    }

    std::vector<uint8_t> localShare(kRecoveryShareBytes, 0);
    auto drawn = UltraCrypt_RandomBytes(localShare.data(), localShare.size());
    if (!drawn) {
        return StoreResult::Error(StoreResultCode::InternalError,
                                  "could not draw the local share: " + drawn.message);
    }
    UltraCryptSecureBuffer recoveryKey;
    auto derived = DeriveRecoveryKey(serverShare, localShare, recoveryKey);
    if (!derived) {
        return StoreResult::Error(StoreResultCode::InternalError,
                                  "could not derive the recovery key: " + derived.message);
    }
    UltraCryptAeadParams aead;
    aead.algorithm = UltraCryptAeadAlgorithm::XChaCha20Poly1305;
    aead.associatedData = RecoveryAssociatedData(localShare, escrowId);
    std::vector<uint8_t> sealed;
    auto wrapped = UltraCrypt_AeadSeal(recoveryKey, aead, dataKey_.Data(), dataKey_.GetSize(), sealed);
    if (!wrapped || sealed.size() != kSlotBytes || aead.nonce.size() != kNonceSize) {
        return StoreResult::Error(StoreResultCode::InternalError,
                                  "could not seal the recovery slot");
    }

    VaultRecoveryInfo previous = recovery_;
    std::vector<uint8_t> previousShare = recoveryLocalShare_;
    std::vector<uint8_t> previousNonce = recoveryNonce_;
    std::vector<uint8_t> previousSealed = recoverySealedKey_;

    recovery_ = VaultRecoveryInfo{true, escrowId, serviceUrl};
    recoveryLocalShare_ = std::move(localShare);
    recoveryNonce_ = aead.nonce;
    recoverySealedKey_ = std::move(sealed);

    auto saved = SaveLocked();
    if (!saved) {
        recovery_ = previous;
        recoveryLocalShare_ = previousShare;
        recoveryNonce_ = previousNonce;
        recoverySealedKey_ = previousSealed;
        return saved;
    }
    return StoreResult::Ok();
}

StoreResult EncryptedFileStore::DisableRecovery() {
    if (!isOpen_) {
        return StoreResult::Error(StoreResultCode::NotOpen, "no vault is open");
    }
    if (!recovery_.enabled) return StoreResult::Ok();
    VaultRecoveryInfo previous = recovery_;
    std::vector<uint8_t> previousShare = recoveryLocalShare_;
    std::vector<uint8_t> previousNonce = recoveryNonce_;
    std::vector<uint8_t> previousSealed = recoverySealedKey_;
    recovery_ = VaultRecoveryInfo{};
    recoveryLocalShare_.clear();
    recoveryNonce_.clear();
    recoverySealedKey_.clear();
    auto saved = SaveLocked();
    if (!saved) {
        recovery_ = previous;
        recoveryLocalShare_ = previousShare;
        recoveryNonce_ = previousNonce;
        recoverySealedKey_ = previousSealed;
        return saved;
    }
    return StoreResult::Ok();
}

StoreResult EncryptedFileStore::SerializeEntries(UltraCryptSecureBuffer& outPlain) const {
    std::vector<uint8_t> buffer;
    PutUint32LE(buffer, static_cast<uint32_t>(entries_.size()));
    for (const auto& entry : entries_) {
        PutUint16LE(buffer, static_cast<uint16_t>(entry.first.size()));
        buffer.insert(buffer.end(), entry.first.begin(), entry.first.end());
        PutUint32LE(buffer, static_cast<uint32_t>(entry.second.GetSize()));
        buffer.insert(buffer.end(), entry.second.Data(),
                      entry.second.Data() + entry.second.GetSize());
    }
    outPlain = UltraCryptSecureBuffer(buffer.data(), buffer.size());
    // The staging vector held plaintext secrets; wipe it before it is freed.
    UltraCrypt_SecureZero(buffer.data(), buffer.size());
    return StoreResult::Ok();
}

StoreResult EncryptedFileStore::DeserializeEntries(const UltraCryptSecureBuffer& plain) {
    for (auto& entry : entries_) entry.second.Clear();
    entries_.clear();

    const uint8_t* p = plain.Data();
    const size_t size = plain.GetSize();
    size_t offset = 0;

    if (size < 4) {
        return StoreResult::Error(StoreResultCode::Corrupt,
                                  "the vault contents are truncated");
    }
    const uint32_t count = ReadUint32LE(p);
    offset += 4;
    if (count > kMaxStoreEntries) {
        return StoreResult::Error(StoreResultCode::Corrupt,
                                  "the vault declares implausibly many entries");
    }

    for (uint32_t i = 0; i < count; ++i) {
        if (offset + 2 > size) {
            return StoreResult::Error(StoreResultCode::Corrupt,
                                      "the vault contents are truncated");
        }
        const uint16_t keyLength = ReadUint16LE(p + offset);
        offset += 2;
        if (keyLength == 0 || keyLength > kMaxStoreKeyLength ||
            offset + keyLength > size) {
            return StoreResult::Error(StoreResultCode::Corrupt,
                                      "the vault contains a malformed key");
        }
        std::string key(reinterpret_cast<const char*>(p + offset), keyLength);
        offset += keyLength;

        if (offset + 4 > size) {
            return StoreResult::Error(StoreResultCode::Corrupt,
                                      "the vault contents are truncated");
        }
        const uint32_t valueLength = ReadUint32LE(p + offset);
        offset += 4;
        if (valueLength > kMaxStoreValueBytes || offset + valueLength > size) {
            return StoreResult::Error(StoreResultCode::Corrupt,
                                      "the vault contains a malformed value");
        }
        entries_.emplace(std::move(key),
                         UltraCryptSecureBuffer(p + offset, valueLength));
        offset += valueLength;
    }

    if (offset != size) {
        // Trailing bytes inside an authenticated payload mean our own writer
        // and reader disagree; refuse rather than guess.
        return StoreResult::Error(StoreResultCode::Corrupt,
                                  "the vault contains unexpected trailing data");
    }
    return StoreResult::Ok();
}

StoreResult EncryptedFileStore::SaveLocked() const {
    if (!isOpen_) {
        return StoreResult::Error(StoreResultCode::NotOpen, "no vault is open");
    }

    UltraCryptSecureBuffer plain;
    auto serialized = SerializeEntries(plain);
    if (!serialized) return serialized;

    // Always written as version 2, whatever was read.
    std::vector<uint8_t> header;
    header.reserve(kFixedHeaderV2 + 256);
    header.insert(header.end(), kMagic, kMagic + kMagicSize);
    header.push_back(kVersion);
    header.push_back(kKdfArgon2id);
    header.push_back(kAeadXChaCha);
    header.push_back(recovery_.enabled ? kFlagRecovery : 0);
    PutUint32LE(header, kdfIterations_);
    PutUint32LE(header, kdfMemoryKiB_);
    header.insert(header.end(), salt_.begin(), salt_.end());

    // The password slot: the data key sealed with the password key, a fresh
    // nonce, the KDF parameters as associated data.
    UltraCryptAeadParams slot;
    slot.algorithm = UltraCryptAeadAlgorithm::XChaCha20Poly1305;
    slot.associatedData = header;
    std::vector<uint8_t> sealedKey;
    auto wrapped = UltraCrypt_AeadSeal(passwordKey_, slot, dataKey_.Data(), dataKey_.GetSize(), sealedKey);
    if (!wrapped || slot.nonce.size() != kNonceSize || sealedKey.size() != kSlotBytes) {
        return StoreResult::Error(StoreResultCode::InternalError,
                                  "could not seal the vault's data key");
    }
    header.insert(header.end(), slot.nonce.begin(), slot.nonce.end());
    header.insert(header.end(), sealedKey.begin(), sealedKey.end());
    if (header.size() != kFixedHeaderV2) {
        return StoreResult::Error(StoreResultCode::InternalError,
                                  "internal error building the vault header");
    }
    if (recovery_.enabled) {
        header.insert(header.end(), recoveryLocalShare_.begin(), recoveryLocalShare_.end());
        header.insert(header.end(), recoveryNonce_.begin(), recoveryNonce_.end());
        header.insert(header.end(), recoverySealedKey_.begin(), recoverySealedKey_.end());
        header.push_back(static_cast<uint8_t>(recovery_.escrowId.size()));
        header.insert(header.end(), recovery_.escrowId.begin(), recovery_.escrowId.end());
        PutUint16LE(header, static_cast<uint16_t>(recovery_.serviceUrl.size()));
        header.insert(header.end(), recovery_.serviceUrl.begin(), recovery_.serviceUrl.end());
    }

    // A fresh nonce for every write. Safe to reuse the data key because the
    // 192-bit nonce space makes a random collision negligible.
    UltraCryptAeadParams aead;
    aead.algorithm = UltraCryptAeadAlgorithm::XChaCha20Poly1305;
    aead.nonce.assign(kNonceSize, 0);
    auto nonce = UltraCrypt_RandomBytes(aead.nonce.data(), aead.nonce.size());
    if (!nonce) {
        return StoreResult::Error(StoreResultCode::InternalError,
                                  "could not generate a nonce: " + nonce.message);
    }
    header.insert(header.end(), aead.nonce.begin(), aead.nonce.end());
    aead.associatedData = header;

    std::vector<uint8_t> ciphertext;
    auto sealed = UltraCrypt_AeadSeal(dataKey_, aead, plain.Data(), plain.GetSize(),
                                      ciphertext);
    if (!sealed) {
        return StoreResult::Error(StoreResultCode::InternalError,
                                  "could not encrypt the vault: " + sealed.message);
    }

    std::vector<uint8_t> file = std::move(header);
    file.insert(file.end(), ciphertext.begin(), ciphertext.end());
    if (!WriteFileAtomically(path_, file)) {
        return StoreResult::Error(StoreResultCode::IoError,
                                  "could not write the vault file: " + path_);
    }
    containerVersion_ = kVersion;
    return StoreResult::Ok();
}

StoreResult EncryptedFileStore::Put(const std::string& key,
                                    const UltraCryptSecureBuffer& value) {
    if (!isOpen_) {
        return StoreResult::Error(StoreResultCode::NotOpen, "no vault is open");
    }
    if (key.empty() || key.size() > kMaxStoreKeyLength) {
        return StoreResult::Error(StoreResultCode::InvalidArgument,
                                  "the key is empty or too long");
    }
    if (value.GetSize() > kMaxStoreValueBytes) {
        return StoreResult::Error(StoreResultCode::InvalidArgument,
                                  "the value is too large");
    }
    if (entries_.size() >= kMaxStoreEntries && entries_.find(key) == entries_.end()) {
        return StoreResult::Error(StoreResultCode::InvalidArgument,
                                  "the vault is full");
    }

    // Keep the previous value so a failed write leaves memory matching disk.
    auto existing = entries_.find(key);
    UltraCryptSecureBuffer previous;
    const bool hadPrevious = existing != entries_.end();
    if (hadPrevious) previous = std::move(existing->second);

    entries_[key] = value.Clone();
    auto saved = SaveLocked();
    if (!saved) {
        if (hadPrevious) {
            entries_[key] = std::move(previous);
        } else {
            entries_.erase(key);
        }
        return saved;
    }
    previous.Clear();
    return StoreResult::Ok();
}

StoreResult EncryptedFileStore::Get(const std::string& key,
                                    UltraCryptSecureBuffer& outValue) const {
    outValue.Clear();
    if (!isOpen_) {
        return StoreResult::Error(StoreResultCode::NotOpen, "no vault is open");
    }
    const auto it = entries_.find(key);
    if (it == entries_.end()) {
        return StoreResult::Error(StoreResultCode::NotFound,
                                  "no entry under that key");
    }
    outValue = it->second.Clone();
    return StoreResult::Ok();
}

StoreResult EncryptedFileStore::Delete(const std::string& key) {
    if (!isOpen_) {
        return StoreResult::Error(StoreResultCode::NotOpen, "no vault is open");
    }
    const auto it = entries_.find(key);
    if (it == entries_.end()) {
        return StoreResult::Error(StoreResultCode::NotFound,
                                  "no entry under that key");
    }

    UltraCryptSecureBuffer removed = std::move(it->second);
    entries_.erase(it);
    auto saved = SaveLocked();
    if (!saved) {
        entries_[key] = std::move(removed);   // keep memory consistent with disk
        return saved;
    }
    removed.Clear();
    return StoreResult::Ok();
}

StoreResult EncryptedFileStore::Replace(const std::string& oldKey,
                                        const std::string& newKey,
                                        const UltraCryptSecureBuffer& newValue) {
    if (!isOpen_) {
        return StoreResult::Error(StoreResultCode::NotOpen, "no vault is open");
    }
    if (newKey.empty() || newKey.size() > kMaxStoreKeyLength) {
        return StoreResult::Error(StoreResultCode::InvalidArgument,
                                  "the key is empty or too long");
    }
    if (newValue.GetSize() > kMaxStoreValueBytes) {
        return StoreResult::Error(StoreResultCode::InvalidArgument,
                                  "the value is too large");
    }

    const auto it = entries_.find(oldKey);
    if (it == entries_.end()) {
        return StoreResult::Error(StoreResultCode::NotFound,
                                  "no entry under that key");
    }
    // Moving onto an unrelated existing entry would destroy that entry's
    // secret, so it is refused exactly as Put refuses to clobber. Renaming an
    // entry onto itself is not a collision, and is how a parameter-only edit
    // arrives here.
    if (newKey != oldKey && entries_.find(newKey) != entries_.end()) {
        return StoreResult::Error(StoreResultCode::AlreadyExists,
                                  "an entry named '" + newKey +
                                      "' already exists");
    }

    UltraCryptSecureBuffer previous = std::move(it->second);
    entries_.erase(it);
    entries_[newKey] = newValue.Clone();

    auto saved = SaveLocked();
    if (!saved) {
        // Put both halves back: the entry returns to its old key with its old
        // value, so memory still matches what is on disk.
        entries_.erase(newKey);
        entries_[oldKey] = std::move(previous);
        return saved;
    }
    previous.Clear();
    return StoreResult::Ok();
}

StoreResult EncryptedFileStore::List(std::vector<std::string>& outKeys) const {
    outKeys.clear();
    if (!isOpen_) {
        return StoreResult::Error(StoreResultCode::NotOpen, "no vault is open");
    }
    outKeys.reserve(entries_.size());
    for (const auto& entry : entries_) outKeys.push_back(entry.first);
    return StoreResult::Ok();
}

StoreResult EncryptedFileStore::ChangePassword(const UltraCryptSecureBuffer& newPassword) {
    if (!isOpen_) {
        return StoreResult::Error(StoreResultCode::NotOpen, "no vault is open");
    }
    if (newPassword.IsEmpty()) {
        return StoreResult::Error(StoreResultCode::InvalidArgument,
                                  "a master password is required");
    }

    // A fresh salt, so the new key is unrelated to the old one.
    UltraCryptKdfParams kdf = UltraCrypt_RecommendedKdfParams();
    UltraCryptSecureBuffer newKey;
    auto derived = UltraCrypt_DeriveKeyFromPassword(newPassword, kdf, newKey);
    if (!derived) {
        return StoreResult::Error(StoreResultCode::InternalError,
                                  "could not derive the new key: " + derived.message);
    }

    // Only the password slot changes: the data key, and with it the
    // recovery slot, stay as they are.
    UltraCryptSecureBuffer previousKey = std::move(passwordKey_);
    const std::vector<uint8_t> previousSalt = salt_;
    const uint32_t previousIterations = kdfIterations_;
    const uint32_t previousMemory     = kdfMemoryKiB_;

    passwordKey_   = std::move(newKey);
    salt_          = kdf.salt;
    kdfIterations_ = kdf.iterations;
    kdfMemoryKiB_  = kdf.memoryKiB;

    auto saved = SaveLocked();
    if (!saved) {
        passwordKey_   = std::move(previousKey);   // the vault on disk is unchanged
        salt_          = previousSalt;
        kdfIterations_ = previousIterations;
        kdfMemoryKiB_  = previousMemory;
        return saved;
    }
    previousKey.Clear();
    return StoreResult::Ok();
}

} // namespace Authenticator
} // namespace UltraCanvas
