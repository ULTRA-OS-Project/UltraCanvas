// Apps/UltraAuthenticator/store/EncryptedFileStore.cpp
// Implementation of the encrypted vault. See the header for the design and the
// threat notes.
//
// Container layout (all integers little-endian):
//
//   offset size field
//   0      8    magic "UCAVAULT"
//   8      1    container version (1)
//   9      1    KDF id      (1 = Argon2id)
//   10     1    AEAD id     (1 = XChaCha20-Poly1305)
//   11     1    reserved (0)
//   12     4    Argon2id passes
//   16     4    Argon2id memory cost, KiB
//   20     16   Argon2id salt
//   36     24   AEAD nonce
//   60     ...  ciphertext || 16-byte tag
//
// The whole 60-byte header is the AEAD associated data.
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
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "EncryptedFileStore.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace UltraCanvas {
namespace Authenticator {
namespace {

constexpr size_t  kMagicSize   = 8;
constexpr size_t  kSaltSize    = 16;
constexpr size_t  kNonceSize   = 24;
constexpr size_t  kHeaderSize  = 60;
constexpr uint8_t kVersion     = 1;
constexpr uint8_t kKdfArgon2id = 1;
constexpr uint8_t kAeadXChaCha = 1;
const char kMagic[kMagicSize] = {'U', 'C', 'A', 'V', 'A', 'U', 'L', 'T'};

constexpr size_t kOffVersion    = 8;
constexpr size_t kOffKdfId      = 9;
constexpr size_t kOffAeadId     = 10;
constexpr size_t kOffIterations = 12;
constexpr size_t kOffMemoryKiB  = 16;
constexpr size_t kOffSalt       = 20;
constexpr size_t kOffNonce      = 36;

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
    std::ifstream file(path, std::ios::binary | std::ios::ate);
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
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
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
        temporary,
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
        std::filesystem::perm_options::replace, ec);
    // A filesystem without POSIX permissions is not a reason to fail the save;
    // the encryption, not the mode bits, is what protects the contents.

    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        std::remove(temporary.c_str());
        return false;
    }
    return true;
}

} // namespace

StoreResult StoreResult::Ok() {
    StoreResult r;
    r.code = StoreResultCode::Success;
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
    return std::filesystem::exists(path, ec) && !ec;
}

void EncryptedFileStore::Close() {
    key_.Clear();
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
    auto derived = UltraCrypt_DeriveKeyFromPassword(password, kdf, key_);
    if (!derived) {
        Close();
        return StoreResult::Error(StoreResultCode::InternalError,
                                  "could not derive the vault key: " + derived.message);
    }

    path_          = path;
    salt_          = kdf.salt;
    kdfIterations_ = kdf.iterations;
    kdfMemoryKiB_  = kdf.memoryKiB;
    isOpen_        = true;

    auto saved = SaveLocked();
    if (!saved) { Close(); return saved; }
    return StoreResult::Ok();
}

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
    if (raw[kOffVersion] != kVersion) {
        return StoreResult::Error(
            StoreResultCode::Corrupt,
            "this vault was written by a newer version of the application");
    }
    if (raw[kOffKdfId] != kKdfArgon2id || raw[kOffAeadId] != kAeadXChaCha) {
        return StoreResult::Error(StoreResultCode::Corrupt,
                                  "the vault uses an unsupported algorithm");
    }

    UltraCryptKdfParams kdf;
    kdf.algorithm    = UltraCryptKdfAlgorithm::Argon2id;
    kdf.iterations   = ReadUint32LE(raw.data() + kOffIterations);
    kdf.memoryKiB    = ReadUint32LE(raw.data() + kOffMemoryKiB);
    kdf.parallelism  = 1;
    kdf.outputLength = UltraCrypt_GetKeySize(UltraCryptAeadAlgorithm::XChaCha20Poly1305);
    kdf.salt.assign(raw.begin() + kOffSalt, raw.begin() + kOffSalt + kSaltSize);

    if (kdf.iterations == 0 || kdf.iterations > kMaxIterations ||
        kdf.memoryKiB == 0 || kdf.memoryKiB > kMaxMemoryKiB) {
        return StoreResult::Error(
            StoreResultCode::Corrupt,
            "the vault declares unreasonable key-derivation parameters");
    }

    UltraCryptSecureBuffer key;
    auto derived = UltraCrypt_DeriveKeyFromPassword(password, kdf, key);
    if (!derived) {
        return StoreResult::Error(StoreResultCode::InternalError,
                                  "could not derive the vault key: " + derived.message);
    }

    UltraCryptAeadParams aead;
    aead.algorithm = UltraCryptAeadAlgorithm::XChaCha20Poly1305;
    aead.nonce.assign(raw.begin() + kOffNonce, raw.begin() + kOffNonce + kNonceSize);
    aead.associatedData.assign(raw.begin(), raw.begin() + kHeaderSize);

    UltraCryptSecureBuffer plain;
    auto opened = UltraCrypt_AeadOpen(key, aead, raw.data() + kHeaderSize,
                                      raw.size() - kHeaderSize, plain);
    if (!opened) {
        return AuthFailure();
    }

    path_          = path;
    key_           = std::move(key);
    salt_          = kdf.salt;
    kdfIterations_ = kdf.iterations;
    kdfMemoryKiB_  = kdf.memoryKiB;
    isOpen_        = true;

    auto parsed = DeserializeEntries(plain);
    if (!parsed) { Close(); return parsed; }
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

    // A fresh nonce for every write. Safe to reuse the session key because the
    // 192-bit nonce space makes a random collision negligible.
    UltraCryptAeadParams aead;
    aead.algorithm = UltraCryptAeadAlgorithm::XChaCha20Poly1305;
    aead.nonce.assign(kNonceSize, 0);
    auto nonce = UltraCrypt_RandomBytes(aead.nonce.data(), aead.nonce.size());
    if (!nonce) {
        return StoreResult::Error(StoreResultCode::InternalError,
                                  "could not generate a nonce: " + nonce.message);
    }

    std::vector<uint8_t> header;
    header.reserve(kHeaderSize);
    header.insert(header.end(), kMagic, kMagic + kMagicSize);
    header.push_back(kVersion);
    header.push_back(kKdfArgon2id);
    header.push_back(kAeadXChaCha);
    header.push_back(0);
    PutUint32LE(header, kdfIterations_);
    PutUint32LE(header, kdfMemoryKiB_);
    header.insert(header.end(), salt_.begin(), salt_.end());
    header.insert(header.end(), aead.nonce.begin(), aead.nonce.end());
    if (header.size() != kHeaderSize) {
        return StoreResult::Error(StoreResultCode::InternalError,
                                  "internal error building the vault header");
    }
    aead.associatedData = header;

    std::vector<uint8_t> ciphertext;
    auto sealed = UltraCrypt_AeadSeal(key_, aead, plain.Data(), plain.GetSize(),
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

    UltraCryptSecureBuffer previousKey = std::move(key_);
    const std::vector<uint8_t> previousSalt = salt_;
    const uint32_t previousIterations = kdfIterations_;
    const uint32_t previousMemory     = kdfMemoryKiB_;

    key_           = std::move(newKey);
    salt_          = kdf.salt;
    kdfIterations_ = kdf.iterations;
    kdfMemoryKiB_  = kdf.memoryKiB;

    auto saved = SaveLocked();
    if (!saved) {
        key_           = std::move(previousKey);   // the vault on disk is unchanged
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
