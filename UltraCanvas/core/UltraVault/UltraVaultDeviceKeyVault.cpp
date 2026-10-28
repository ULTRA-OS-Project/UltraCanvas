// core/UltraVault/UltraVaultDeviceKeyVault.cpp
// An application's own vault on UltraVault, unlocked by a device key — see
// the header. UI-free and free of UltraNet, like the rest of the module, so
// the headless consumers and the test binary link it as they are.
//
// Every path is UTF-8 and goes to the filesystem through PathFromUtf8: the
// device key's path used to be handed over as a std::string, which on Windows
// is read in the ANSI code page, so a profile folder the code page cannot
// spell (a Thai or Cyrillic user name under code page 1252) could not find,
// write or protect its device key, and the vault did not open.
// Version: 0.4.1 - a device.key.next is closed before it is renamed (Windows
//   renames no open file, so the key stayed behind as .next)
// Version: 0.4.0 - ChangePassphrase, NeedsPassphrase, ForgetDeviceKey: a Primary Password
// Version: 0.3.0 - several vaults in one process, from any thread: one lock,
//                  the store's owner remembered, a vault re-opens its own file
//                  before every access
// Version: 0.2.0 - GetLastUnlockStatus / DescribeUnlockStatus; UTF-8 paths
// Previous: 0.1.0 - moved here from UltraMail's CredentialVault 0.6.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraVault/UltraVaultDeviceKeyVault.h"
#include "UltraCanvasPathUtf8.h"   // PathFromUtf8 / PathToUtf8

#include "UltraCrypt/UltraCryptCore.h"   // UltraCrypt_RandomBytes

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <string>
#include <vector>

using UltraCanvas::PathFromUtf8;
using UltraCanvas::PathToUtf8;

namespace fs = std::filesystem;

namespace UltraVault {

namespace {

// The process has one open UltraVault store. Every DeviceKeyVault call takes
// this lock (recursive: the data operations nest, and Use() runs its
// function under it), and the vault whose file the store holds is
// remembered, so a vault that finds another's file open re-opens its own
// before it reads or writes, and never closes another vault's store.
std::recursive_mutex& Turns() {
    static std::recursive_mutex turns;
    return turns;
}

const DeviceKeyVault*& StoreOwner() {
    static const DeviceKeyVault* owner = nullptr;
    return owner;
}

using Turn = std::lock_guard<std::recursive_mutex>;

// The device key holds a random passphrase so the vault unlocks without a
// prompt (see the header). NOT "vault.key" — that name belongs to the 0.1
// legacy format below and would confuse its migration.
constexpr const char* kDeviceKeyFile = "device.key";

// A random passphrase (hex of 32 crypto-random bytes) for a new vault's device
// key; empty if secure randomness is unavailable.
std::string RandomPassphrase() {
    std::vector<uint8_t> bytes;
    if (!UltraCrypt_RandomBytes(bytes, 32)) return {};
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (uint8_t b : bytes) { out.push_back(hex[b >> 4]); out.push_back(hex[b & 0xF]); }
    return out;
}

// ---- 0.1-format reader (kept only to migrate away from it) ----------------
// The old vault XOR-ed each secret against a key stored in the same directory
// and wrote one "base64(account) TAB base64(xor(secret))" line per account.
// Nothing here writes that format; it exists to read it once and delete it.

constexpr const char* kLegacyKeyFile   = "vault.key";
constexpr const char* kLegacyCredsFile = "creds.dat";

// RFC 4648 Base64 decoder for those lines. The framework's codec lives in
// UltraCanvasTextUtils, which is linked into (and exported by) the UltraCanvas
// library; UltraVault stays off that library on purpose — the same link-time
// split that keeps UltraCrypt UI-free — so this private copy is deliberate.
// Lenient like UltraCanvas::Base64Decode: whitespace ignored, padding optional.
std::string LegacyBase64Decode(const std::string& text) {
    auto value = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::string out;
    out.reserve(text.size() * 3 / 4);
    uint32_t acc = 0;
    int bits = 0;
    for (char c : text) {
        if (c == '=') break;
        const int v = value(c);
        if (v < 0) continue;   // whitespace or stray byte
        acc = (acc << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((acc >> bits) & 0xFF));
        }
    }
    return out;
}

std::string LegacyXor(const std::string& data, const std::vector<uint8_t>& key) {
    std::string out = data;
    if (key.empty()) return out;
    for (std::size_t i = 0; i < out.size(); ++i)
        out[i] = static_cast<char>(static_cast<uint8_t>(out[i]) ^ key[i % key.size()]);
    return out;
}

std::map<std::string, std::string> ReadLegacyVault(const std::string& dir) {
    std::map<std::string, std::string> creds;
    std::error_code ec;
    const fs::path keyPath   = PathFromUtf8(dir) / kLegacyKeyFile;
    const fs::path credsPath = PathFromUtf8(dir) / kLegacyCredsFile;
    if (!fs::exists(keyPath, ec) || !fs::exists(credsPath, ec)) return creds;

    std::ifstream ks(keyPath, std::ios::binary);
    std::vector<uint8_t> key((std::istreambuf_iterator<char>(ks)),
                             std::istreambuf_iterator<char>());

    std::ifstream is(credsPath);
    std::string line;
    while (std::getline(is, line)) {
        const std::size_t tab = line.find('\t');
        if (tab == std::string::npos) continue;
        creds[LegacyBase64Decode(line.substr(0, tab))] =
            LegacyXor(LegacyBase64Decode(line.substr(tab + 1)), key);
    }
    return creds;
}

// Map an UltraVault result to the reason the caller reports.
UnlockStatus StatusFor(const Result& r) {
    switch (r.code) {
        case ResultCode::Ok:            return UnlockStatus::Ok;
        case ResultCode::AccessDenied:       return UnlockStatus::WrongPassphrase;
        case ResultCode::BackendUnavailable: return UnlockStatus::Unavailable;
        case ResultCode::IoError:            return UnlockStatus::IoError;
        case ResultCode::Locked:             return UnlockStatus::Locked;
        default:                             return UnlockStatus::IoError;
    }
}

constexpr const char* kAccessSuffix  = ".oauth.access";
constexpr const char* kRefreshSuffix = ".oauth.refresh";
constexpr const char* kExpiresSuffix = ".oauth.expires";

// `text` as the whole of `file`, readable by its owner only.
bool WriteOwnerOnly(const fs::path& file, const std::string& text) {
    { std::ofstream out(file, std::ios::binary | std::ios::trunc);
      if (!out) return false;
      out << text;
      if (!out) return false; }
    std::error_code ec;
    fs::permissions(file, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace, ec);
    return true;
}

// The whole of a key file, its line end dropped; "" when it cannot be read.
// The file is closed again before this returns: Windows renames or removes
// no file that is still open.
std::string ReadKeyFile(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
    return text;
}

} // namespace

std::string DeviceKeyVault::KeyFor(const std::string& account) const {
    return profile_.keyPrefix + account;
}

std::string DeviceKeyVault::VaultPath() const {
    return PathToUtf8(PathFromUtf8(dir_) / PathFromUtf8(profile_.vaultFileName));
}

bool DeviceKeyVault::Exists() const {
    std::error_code ec;
    return fs::exists(PathFromUtf8(dir_) / PathFromUtf8(profile_.vaultFileName), ec);
}

UnlockStatus DeviceKeyVault::Unlock(const std::string& passphrase) {
    Turn turn(Turns());
    // An empty passphrase derives a key anyone could reproduce, which would
    // put us back where the 0.1 vault was. Refuse it outright. A vault with
    // no directory or no file name has nowhere to go.
    if (passphrase.empty()) return lastStatus_ = UnlockStatus::WrongPassphrase;
    if (dir_.empty() || profile_.vaultFileName.empty())
        return lastStatus_ = UnlockStatus::IoError;

    std::error_code ec;
    fs::create_directories(PathFromUtf8(dir_), ec);
    if (ec) return lastStatus_ = UnlockStatus::IoError;

    // Initialize() is idempotent per process and will not reconfigure an open
    // vault, so close any previous one before adopting this passphrase. The
    // vault that held it stays unlocked and re-opens its file on its next
    // access.
    Shutdown();
    StoreOwner() = nullptr;

    Config config;
    config.backend    = Backend::File;
    config.filePath   = VaultPath();
    config.passphrase = passphrase;   // wiped in place by Initialize()

    const Result r = Initialize(config);
    if (!r.IsOk()) {
        unlocked_ = false;
        return lastStatus_ = StatusFor(r);
    }
    unlocked_ = true;
    StoreOwner() = this;

    MigrateLegacy();
    return lastStatus_ = UnlockStatus::Ok;
}

bool DeviceKeyVault::Use(const std::function<void(DeviceKeyVault& vault)>& use) {
    // One per process (the core library), whichever module the vault is in.
    Turn turn(Turns());
    // Another vault may have opened its own file since: open ours again.
    if (unlocked_) Lock();
    if (!TryAutoUnlock()) return false;
    if (use) use(*this);
    Lock();
    return true;
}

void DeviceKeyVault::Lock() {
    Turn turn(Turns());
    if (!unlocked_) return;
    // Only the store this vault holds is closed: another vault's open file
    // is its own to close.
    if (StoreOwner() == this) {
        Shutdown();   // wipes the decrypted store and derived key
        StoreOwner() = nullptr;
    }
    unlocked_ = false;
}

bool DeviceKeyVault::HoldTheStore() const {
    if (!unlocked_) {
        lastStatus_ = UnlockStatus::Locked;
        return false;
    }
    if (StoreOwner() == this) return true;
    // Another vault took the one store since this one was unlocked: back to
    // our own file before touching anything - never the other vault's.
    return const_cast<DeviceKeyVault*>(this)->ReopenWithDeviceKey();
}

bool DeviceKeyVault::ReopenWithDeviceKey() {
    const fs::path keyFile = PathFromUtf8(DeviceKeyPath());
    std::error_code ec;
    if (dir_.empty() || !fs::exists(keyFile, ec)) {
        // Unlocked with a passphrase of its own, which is not kept: this
        // vault cannot re-open silently. Its calls fail rather than reach
        // into the other vault's file.
        lastStatus_ = UnlockStatus::Locked;
        return false;
    }
    std::ifstream in(keyFile, std::ios::binary);
    std::string pass((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    while (!pass.empty() && (pass.back() == '\n' || pass.back() == '\r')) pass.pop_back();
    if (pass.empty()) {
        lastStatus_ = UnlockStatus::IoError;
        return false;
    }
    return Unlock(pass) == UnlockStatus::Ok;
}

std::string DeviceKeyVault::DeviceKeyPath() const {
    return PathToUtf8(PathFromUtf8(dir_) / kDeviceKeyFile);
}

bool DeviceKeyVault::TryAutoUnlock() {
    Turn turn(Turns());
    if (unlocked_ && StoreOwner() == this) {
        lastStatus_ = UnlockStatus::Ok;
        return true;
    }
    unlocked_ = false;      // another vault took the store: open ours afresh
    if (dir_.empty()) {
        lastStatus_ = UnlockStatus::IoError;
        return false;
    }
    const fs::path keyFile = PathFromUtf8(DeviceKeyPath());

    // A device key written for a new vault file that ChangePassphrase did not
    // get to move into place: the file it is for may be in place already.
    std::error_code ec;
    const fs::path nextKey = PathFromUtf8(DeviceKeyPath() + ".next");
    if (fs::exists(nextKey, ec)) {
        const std::string pass = ReadKeyFile(nextKey);
        if (!pass.empty() && Unlock(pass) == UnlockStatus::Ok) {
            fs::rename(nextKey, keyFile, ec);
            return true;
        }
        fs::remove(nextKey, ec);
    }

    // A stored device key: unlock silently with it.
    if (fs::exists(keyFile, ec)) {
        const std::string pass = ReadKeyFile(keyFile);
        if (pass.empty()) {
            lastStatus_ = UnlockStatus::IoError;   // unreadable, or emptied
            return false;
        }
        return Unlock(pass) == UnlockStatus::Ok;
    }

    // No device key. If a vault already exists it was made with a master
    // password we don't have — the caller must prompt once, then persist the
    // key. Only create a fresh vault + key when there is nothing to migrate.
    if (Exists()) {
        lastStatus_ = UnlockStatus::Locked;
        return false;
    }

    // No random bytes means no crypto backend: UltraCrypt was built without
    // libsodium. Unlock() would say the same, but a device key cannot even be
    // made, so it is said here.
    const std::string pass = RandomPassphrase();
    if (pass.empty()) {
        lastStatus_ = UnlockStatus::Unavailable;
        return false;
    }
    if (!PersistDeviceKey(pass)) {
        lastStatus_ = UnlockStatus::IoError;        // could not write the key file
        return false;
    }
    if (Unlock(pass) == UnlockStatus::Ok) return true;
    // Creating the vault failed: drop the key file so a retry is not blocked.
    // lastStatus_ keeps what Unlock() reported.
    fs::remove(keyFile, ec);
    return false;
}

bool DeviceKeyVault::PersistDeviceKey(const std::string& passphrase) {
    if (passphrase.empty() || dir_.empty()) return false;
    std::error_code ec;
    fs::create_directories(PathFromUtf8(dir_), ec);
    const fs::path keyFile = PathFromUtf8(DeviceKeyPath());
    { std::ofstream out(keyFile, std::ios::binary | std::ios::trunc);
      if (!out) return false;
      out << passphrase;
      if (!out) return false; }
    // Owner-only: the local key is the only thing standing between the folder
    // and the secrets, so keep it off other users (Thunderbird's key4.db posture).
    fs::permissions(keyFile,
                    fs::perms::owner_read | fs::perms::owner_write,
                    fs::perm_options::replace, ec);
    return true;
}

bool DeviceKeyVault::NeedsPassphrase() const {
    std::error_code ec;
    return Exists() && !dir_.empty() && !fs::exists(PathFromUtf8(DeviceKeyPath()), ec);
}

std::string DeviceKeyVault::NewDeviceKey() { return RandomPassphrase(); }

UnlockStatus DeviceKeyVault::ChangePassphrase(const std::string& currentPassphrase,
                                              const std::string& newPassphrase,
                                              bool keepAsDeviceKey) {
    Turn turn(Turns());
    if (newPassphrase.empty()) return lastStatus_ = UnlockStatus::WrongPassphrase;

    // The current passphrase, proven by opening the file with it - it is
    // also what re-opens the old file if anything below fails.
    std::string current = currentPassphrase;
    if (current.empty()) {
        current = ReadKeyFile(PathFromUtf8(DeviceKeyPath()));
        if (current.empty()) return lastStatus_ = UnlockStatus::Locked;
    }
    if (const UnlockStatus opened = Unlock(current); opened != UnlockStatus::Ok) return opened;

    std::vector<std::pair<std::string, SecretValue>> secrets;
    for (const std::string& key : List("")) {
        SecretValue value;
        if (!Get(key, value).IsOk()) return lastStatus_ = UnlockStatus::IoError;
        secrets.emplace_back(key, std::move(value));
    }
    auto wipe = [&secrets] {
        for (auto& entry : secrets)
            if (!entry.second.bytes.empty())
                UltraCrypt_SecureZero(entry.second.bytes.data(), entry.second.bytes.size());
        secrets.clear();
    };

    const fs::path vaultFile = PathFromUtf8(VaultPath());
    const std::string freshPath = VaultPath() + ".rekey";
    const fs::path fresh = PathFromUtf8(freshPath);
    std::error_code ec;
    fs::remove(fresh, ec);
    Shutdown();
    StoreOwner() = nullptr;
    unlocked_ = false;

    Config config;
    config.backend    = Backend::File;
    config.filePath   = freshPath;
    config.passphrase = newPassphrase;   // wiped in place by Initialize()
    bool ok = Initialize(config).IsOk();
    for (const auto& entry : secrets)
        if (ok) ok = Put(entry.first, entry.second).IsOk();
    if (ok && !fs::exists(fresh, ec)) {
        // Nothing to carry over: a mutation writes the (empty) file.
        const std::string marker = profile_.keyPrefix + "passphrase-changed";
        ok = Put(marker, SecretValue::FromString("1")).IsOk() && Delete(marker).IsOk();
    }
    Shutdown();
    wipe();
    // A kept device key is written beside the old one before the swap
    // (device.key.next, which TryAutoUnlock also tries), so whenever the
    // new file is in place a key that opens it is on disk too.
    const fs::path nextKey = PathFromUtf8(DeviceKeyPath() + ".next");
    if (ok && keepAsDeviceKey) ok = WriteOwnerOnly(nextKey, newPassphrase);
    if (ok) fs::rename(fresh, vaultFile, ec);
    if (!ok || ec) {
        fs::remove(fresh, ec);
        fs::remove(nextKey, ec);
        Unlock(current);
        return lastStatus_ = UnlockStatus::IoError;
    }
    if (keepAsDeviceKey) {
        fs::rename(nextKey, PathFromUtf8(DeviceKeyPath()), ec);
    } else {
        // Interrupted before this, the old device key stays and no longer
        // opens the file: the caller asks for the passphrase, and forgets
        // the stale key once the typed one works (ForgetDeviceKey).
        fs::remove(PathFromUtf8(DeviceKeyPath()), ec);
    }
    return Unlock(newPassphrase);
}

bool DeviceKeyVault::ForgetDeviceKey() {
    std::error_code ec;
    if (dir_.empty()) return false;
    fs::remove(PathFromUtf8(DeviceKeyPath() + ".next"), ec);
    return fs::remove(PathFromUtf8(DeviceKeyPath()), ec);
}

std::string DeviceKeyVault::DescribeUnlockStatus(UnlockStatus status) {
    switch (status) {
        case UnlockStatus::Ok:
            return "the vault is open";
        case UnlockStatus::WrongPassphrase:
            return "the key does not open it - a wrong password, or the vault file "
                   "was changed";
        case UnlockStatus::Unavailable:
            return "this build has no encryption library (UltraCrypt was built without "
                   "libsodium), so it cannot keep passwords";
        case UnlockStatus::IoError:
            return "the vault folder or its files cannot be read or written";
        case UnlockStatus::Locked:
            return "the vault was made with a master password and has no device key; "
                   "it has to be unlocked with that password once";
    }
    return "unknown reason";
}

int DeviceKeyVault::MigrateLegacy() {
    std::error_code ec;
    auto legacy = ReadLegacyVault(dir_);
    if (legacy.empty()) {
        // The 0.1 code wrote its key file on the first read, so a folder that
        // never held a secret still carries one. With no creds.dat there is
        // nothing it could decrypt; drop it rather than leave a stray key.
        if (!fs::exists(PathFromUtf8(dir_) / kLegacyCredsFile, ec))
            fs::remove(PathFromUtf8(dir_) / kLegacyKeyFile, ec);
        return 0;
    }

    int carried = 0;
    for (const auto& [account, secret] : legacy) {
        if (account.empty()) continue;
        if (Put(KeyFor(account), SecretValue::FromString(secret)).IsOk())
            ++carried;
    }
    // Only drop the old files once every secret is safely in the new vault;
    // a partial migration keeps them so nothing is lost.
    if (carried == static_cast<int>(legacy.size())) {
        fs::remove(PathFromUtf8(dir_) / kLegacyCredsFile, ec);
        fs::remove(PathFromUtf8(dir_) / kLegacyKeyFile, ec);
    }
    return carried;
}

bool DeviceKeyVault::Store(const std::string& account, const std::string& secret) {
    Turn turn(Turns());
    if (account.empty() || !HoldTheStore()) return false;
    if (!Put(KeyFor(account), SecretValue::FromString(secret)).IsOk())
        return false;
    RemoveOAuthTokens(account);   // one sign-in method per account
    return true;
}

bool DeviceKeyVault::StoreOAuthTokens(const std::string& account, const OAuthTokens& tokens) {
    Turn turn(Turns());
    if (account.empty() || tokens.Empty() || !HoldTheStore()) return false;
    const std::string base = KeyFor(account);
    auto put = [&](const char* suffix, const std::string& value) {
        return Put(base + suffix, SecretValue::FromString(value)).IsOk();
    };
    if (!put(kAccessSuffix, tokens.accessToken) ||
        !put(kRefreshSuffix, tokens.refreshToken) ||
        !put(kExpiresSuffix, std::to_string(tokens.expiresAt)))
        return false;
    Delete(base);   // the password slot, if the account had one
    return true;
}

bool DeviceKeyVault::RetrieveOAuthTokens(const std::string& account, OAuthTokens& out) const {
    Turn turn(Turns());
    out = OAuthTokens{};
    if (account.empty() || !HoldTheStore()) return false;
    const std::string base = KeyFor(account);
    SecretValue v;
    if (!Get(base + kAccessSuffix, v).IsOk()) return false;
    out.accessToken = v.AsString();
    if (Get(base + kRefreshSuffix, v).IsOk()) out.refreshToken = v.AsString();
    if (Get(base + kExpiresSuffix, v).IsOk())
        out.expiresAt = std::strtoll(v.AsString().c_str(), nullptr, 10);
    return !out.Empty();
}

bool DeviceKeyVault::HasOAuthTokens(const std::string& account) const {
    OAuthTokens ignore;
    return RetrieveOAuthTokens(account, ignore);
}

bool DeviceKeyVault::RemoveOAuthTokens(const std::string& account) {
    Turn turn(Turns());
    if (account.empty() || !HoldTheStore()) return false;
    const std::string base = KeyFor(account);
    const bool had = Delete(base + kAccessSuffix).IsOk();
    Delete(base + kRefreshSuffix);
    Delete(base + kExpiresSuffix);
    return had;
}

SignInMethod DeviceKeyVault::MethodFor(const std::string& account) const {
    Turn turn(Turns());
    if (HasOAuthTokens(account)) return SignInMethod::OAuth2;
    if (Has(account)) return SignInMethod::Password;
    return SignInMethod::NoSignIn;
}

bool DeviceKeyVault::Retrieve(const std::string& account, std::string& out) const {
    Turn turn(Turns());
    out.clear();
    if (account.empty() || !HoldTheStore()) return false;
    SecretValue value;
    if (!Get(KeyFor(account), value).IsOk()) return false;
    out = value.AsString();
    return true;
}

bool DeviceKeyVault::Has(const std::string& account) const {
    std::string ignore;
    return Retrieve(account, ignore);
}

bool DeviceKeyVault::Remove(const std::string& account) {
    Turn turn(Turns());
    if (account.empty() || !HoldTheStore()) return false;
    return Delete(KeyFor(account)).IsOk();
}

} // namespace UltraVault
