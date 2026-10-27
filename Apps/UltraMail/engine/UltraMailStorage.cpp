// Apps/UltraMail/engine/UltraMailStorage.cpp
// See UltraMailStorage.h.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailStorage.h"

#include "UltraCanvasPathUtf8.h"
#include "UltraCrypt/UltraCryptCore.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;
using UltraCanvas::PathFromUtf8;
using UltraCanvas::PathToUtf8;

namespace UltraMail {

namespace {

constexpr size_t kMagicSize = 16;
constexpr char   kMagic[kMagicSize + 1] = "UltraMail:eml:1";   // 15 characters and its NUL
constexpr size_t kNonceSize = 24;
constexpr size_t kTagSize = 16;

std::mutex g_bodyKeyLock;
StorageKey g_bodyKey;

StorageKey Derive(const StorageKey& key, const std::string& purpose) {
    if (!key) return nullptr;
    const std::string salt = "UltraMail storage";
    UltraCryptSecureBuffer out;
    if (!UltraCrypt_DeriveKeyHkdf(UltraCryptHashAlgorithm::SHA256, *key,
                                  std::vector<uint8_t>(salt.begin(), salt.end()),
                                  std::vector<uint8_t>(purpose.begin(), purpose.end()), 32, out))
        return nullptr;
    return std::make_shared<const UltraCryptSecureBuffer>(std::move(out));
}

bool ReadWhole(const fs::path& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return !in.bad();
}

// `bytes` as `path`, through `<path>.part` and a rename.
bool WriteWhole(const fs::path& path, const std::string& bytes, std::string* error) {
    fs::path part = path;
    part += ".part";
    {
        std::ofstream out(part, std::ios::binary | std::ios::trunc);
        if (!out) {
            if (error) *error = "cannot write " + PathToUtf8(part);
            return false;
        }
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!out) {
            if (error) *error = "cannot write " + PathToUtf8(part);
            out.close();
            std::error_code ec;
            fs::remove(part, ec);
            return false;
        }
    }
    std::error_code ec;
    fs::rename(part, path, ec);
    if (ec) {
        if (error) *error = "cannot move " + PathToUtf8(part) + " into place: " + ec.message();
        fs::remove(part, ec);
        return false;
    }
    return true;
}

} // namespace

StorageKey LoadStorageKey(CredentialVault& vault, bool create, std::string& error) {
    error.clear();
    if (!UltraCrypt_IsAvailable() && !UltraCrypt_Initialize()) {
        error = "this build has no encryption library (UltraCrypt was built without libsodium)";
        return nullptr;
    }
    std::string hex;
    if (vault.Retrieve(kStorageKeyAccount, hex)) {
        std::vector<uint8_t> bytes;
        if (!UltraCrypt_FromHex(hex, bytes) || bytes.size() != 32) {
            UltraCrypt_SecureZero(hex.data(), hex.size());
            error = "the storage key in the vault is damaged";
            return nullptr;
        }
        UltraCrypt_SecureZero(hex.data(), hex.size());
        auto key = std::make_shared<const UltraCryptSecureBuffer>(bytes.data(), bytes.size());
        UltraCrypt_SecureZero(bytes.data(), bytes.size());
        return key;
    }
    if (!vault.IsUnlocked()) {
        error = "the vault is locked";
        return nullptr;
    }
    if (!create) return nullptr;
    std::vector<uint8_t> bytes;
    if (!UltraCrypt_RandomBytes(bytes, 32)) {
        error = "no random bytes for a storage key";
        return nullptr;
    }
    hex = UltraCrypt_ToHex(bytes);
    const bool stored = vault.Store(kStorageKeyAccount, hex);
    UltraCrypt_SecureZero(hex.data(), hex.size());
    if (!stored) {
        UltraCrypt_SecureZero(bytes.data(), bytes.size());
        error = "the storage key could not be written to the vault";
        return nullptr;
    }
    auto key = std::make_shared<const UltraCryptSecureBuffer>(bytes.data(), bytes.size());
    UltraCrypt_SecureZero(bytes.data(), bytes.size());
    return key;
}

StorageKey ReadMailboxStorageKey(const std::string& mailDataDir, std::string& problem) {
    problem.clear();
    CredentialVault vault(mailDataDir + "/vault");
    if (!vault.Exists()) return nullptr;   // TryAutoUnlock would make one
    if (vault.NeedsPassphrase()) {
        problem = "UltraMail's mail is locked with a Primary Password";
        return nullptr;
    }
    StorageKey key;
    std::string error;
    const bool opened = vault.Use([&](UltraVault::DeviceKeyVault&) {
        key = LoadStorageKey(vault, /*create=*/false, error);
    });
    if (!opened) {
        problem = "UltraMail's credential vault cannot be opened: " +
                  CredentialVault::DescribeUnlockStatus(vault.GetLastUnlockStatus());
        return nullptr;
    }
    if (!key && !error.empty()) problem = "UltraMail's storage key cannot be read: " + error;
    return key;
}

StorageKey DatabaseKey(const StorageKey& storageKey, const std::string& fileName) {
    return Derive(storageKey, "database " + fileName);
}

StorageKey BodyKey(const StorageKey& storageKey) { return Derive(storageKey, "message files"); }

UltraDbResult EncryptDatabaseFile(const std::string& path, const StorageKey& key) {
    if (!key) return UltraDbResult::Error(UltraDbResultCode::InvalidArgument, "no key");
    if (UltraDb_InspectSqliteFile(path) != UltraDbSqliteFileKind::Plain) return UltraDbResult::Ok();
    return UltraDb_SetSqliteFileEncryption(path, nullptr, key.get());
}

void SetBodyKey(StorageKey key) {
    std::lock_guard<std::mutex> lock(g_bodyKeyLock);
    g_bodyKey = std::move(key);
}

StorageKey GetBodyKey() {
    std::lock_guard<std::mutex> lock(g_bodyKeyLock);
    return g_bodyKey;
}

bool IsSealedBody(const std::string& stored) {
    return stored.size() >= kMagicSize + kNonceSize + kTagSize
           && std::memcmp(stored.data(), kMagic, kMagicSize) == 0;
}

bool SealBody(const StorageKey& key, const std::string& raw, std::string& out) {
    if (!key) return false;
    UltraCryptAeadParams params;
    params.associatedData.assign(kMagic, kMagic + kMagicSize);
    std::vector<uint8_t> sealed;
    if (!UltraCrypt_AeadSeal(*key, params, raw.data(), raw.size(), sealed)) return false;
    out.clear();
    out.reserve(kMagicSize + params.nonce.size() + sealed.size());
    out.append(kMagic, kMagicSize);
    out.append(reinterpret_cast<const char*>(params.nonce.data()), params.nonce.size());
    out.append(reinterpret_cast<const char*>(sealed.data()), sealed.size());
    return true;
}

bool OpenBody(const StorageKey& key, const std::string& stored, std::string& out) {
    if (!IsSealedBody(stored)) {
        // Not sealed: a file written before the mail was encrypted, or by a
        // build without the key. Taken as it is.
        out = stored;
        return true;
    }
    if (!key) return false;
    UltraCryptAeadParams params;
    params.associatedData.assign(kMagic, kMagic + kMagicSize);
    params.nonce.assign(stored.begin() + kMagicSize, stored.begin() + kMagicSize + kNonceSize);
    UltraCryptSecureBuffer opened;
    const size_t at = kMagicSize + kNonceSize;
    if (!UltraCrypt_AeadOpen(*key, params, stored.data() + at, stored.size() - at, opened)) return false;
    out.assign(reinterpret_cast<const char*>(opened.Data()), opened.GetSize());
    return true;
}

bool ReadBodyFile(const std::string& path, std::string& out, std::string* error) {
    std::string stored;
    if (!ReadWhole(PathFromUtf8(path), stored)) {
        if (error) *error = "cannot read " + path;
        return false;
    }
    if (!OpenBody(GetBodyKey(), stored, out)) {
        if (error) *error = path + " does not open with this mailbox's key";
        return false;
    }
    return true;
}

bool WriteBodyFile(const std::string& path, const std::string& raw, std::string* error) {
    const StorageKey key = GetBodyKey();
    if (!key) return WriteWhole(PathFromUtf8(path), raw, error);
    std::string sealed;
    if (!SealBody(key, raw, sealed)) {
        if (error) *error = "cannot encrypt " + path;
        return false;
    }
    return WriteWhole(PathFromUtf8(path), sealed, error);
}

size_t EncryptStoredBodies(const std::string& mailDir, const std::function<bool()>& stop) {
    const StorageKey key = GetBodyKey();
    if (!key) return 0;
    size_t sealedCount = 0;
    std::error_code ec;
    fs::recursive_directory_iterator it(PathFromUtf8(mailDir), fs::directory_options::skip_permission_denied, ec);
    for (fs::recursive_directory_iterator end; !ec && it != end; it.increment(ec)) {
        if (stop && stop()) break;
        std::error_code entryError;
        if (!it->is_regular_file(entryError) || it->path().extension() != ".eml") continue;
        std::string stored;
        if (!ReadWhole(it->path(), stored) || IsSealedBody(stored)) continue;
        std::string sealed;
        if (!SealBody(key, stored, sealed)) continue;
        // Gone meanwhile (the message was deleted or moved): not brought back.
        if (!fs::exists(it->path(), entryError)) continue;
        if (WriteWhole(it->path(), sealed, nullptr)) ++sealedCount;
    }
    return sealedCount;
}

} // namespace UltraMail
