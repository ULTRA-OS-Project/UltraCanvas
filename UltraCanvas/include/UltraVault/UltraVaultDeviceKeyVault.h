// include/UltraVault/UltraVaultDeviceKeyVault.h
// An application's own credential vault on UltraVault: one encrypted vault
// file in a directory the application owns, unlocked without a prompt by a
// random device key stored beside it. The one home for the per-account
// secrets of UltraMail, UltraSocial and any application that stores a
// password or a token set per account — each used to carry a copy of this
// class, and the copies drifted (UltraSocial's still wrote the 0.1 XOR format).
//
// The vault file is UltraVault's encrypted-file backend (Argon2id-derived
// key, XChaCha20-Poly1305 via UltraCrypt, authenticated header). By default
// it is unlocked with a random passphrase kept in `device.key` (owner-only)
// next to it — the same posture as Thunderbird with no Primary Password:
// secrets are encrypted at rest, but the key is readable by anyone with
// access to the directory, so the user is never prompted. Unlock() still
// accepts an explicit passphrase, so an opt-in "Primary Password" can
// re-introduce a real prompt without changing the storage format.
//
// Vaults written in the 0.1 format (secrets XOR-ed against a `vault.key`
// sidecar, one base64 line per account in `creds.dat`) are migrated on the
// first successful unlock and the old files removed.
//
// UltraVault itself is one store per process, so several DeviceKeyVaults in
// one program - ChatCube's three messenger providers, each on its own
// thread - take turns in it: every call takes one lock the whole process
// shares, the vault whose file the store holds is remembered, and a vault
// that finds another's file open re-opens its own (with its device key)
// before it reads or writes. So Store() never lands in another vault's file
// and Retrieve() never answers from one, whether the vault is used through
// Use() or unlocked once and used directly - until 0.3.0 a direct user's
// secrets went into whichever vault held the store at that moment, and its
// Lock() closed that other vault under its user. A vault unlocked with a
// passphrase of its own (no device key) cannot re-open silently: its calls
// then fail with Locked instead of touching the wrong file. A second
// instance on the same directory is fine - it re-opens the same file.
//
// The directory is a UTF-8 path on every platform (UltraCanvasPathUtf8.h): a
// profile folder named in Thai or Cyrillic opens the same as an ASCII one.
//
// Version: 0.3.0 - several vaults in one process, from any thread: one lock,
//                  the store's owner remembered, a vault re-opens its own file
//                  before every access
// Version: 0.2.0 - GetLastUnlockStatus / DescribeUnlockStatus: why the vault
//                  stayed closed; UTF-8 paths throughout
// Previous: 0.1.0 - moved here from UltraMail's CredentialVault 0.6.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once
#ifndef ULTRAVAULT_DEVICE_KEY_VAULT_H
#define ULTRAVAULT_DEVICE_KEY_VAULT_H

#include <UltraVault/UltraVault.h>

#include <cstdint>
#include <functional>
#include <string>

namespace UltraVault {

// The tokens of an account that signed in with OAuth2.
struct OAuthTokens {
    std::string accessToken;
    std::string refreshToken;
    int64_t     expiresAt = 0;   // epoch seconds the access token dies at; 0 = unknown

    bool Empty() const { return accessToken.empty() && refreshToken.empty(); }
    // True when a session must not start on the current access token.
    bool NeedsRefresh(int64_t now) const {
        return accessToken.empty() || (expiresAt > 0 && expiresAt <= now);
    }
};

// How an account signs in, decided by what the vault holds for it.
enum class SignInMethod { NoSignIn, Password, OAuth2 };

// Why an unlock attempt failed, so the UI can tell "wrong password" (ask
// again) from "this build cannot open a vault at all" (say so and stop).
enum class UnlockStatus {
    Ok,
    WrongPassphrase,     // wrong master password, or the vault file was altered
    Unavailable,         // no crypto backend (UltraCrypt without libsodium)
    IoError,             // the vault path cannot be read or written
    Locked               // Unlock() has not run yet
};

// What tells one application's vault from another's.
struct DeviceKeyVaultProfile {
    // Name of the encrypted vault file inside the directory, e.g. "ultramail.vault".
    std::string vaultFileName;
    // Prefix of every UltraVault key the vault writes, in the module's
    // "<vendor>.<app>." convention (UltraAI/Docs/UltraVault.md §4), e.g.
    // "mail.ultramail." — the account name is appended to it.
    std::string keyPrefix;
};

class DeviceKeyVault {
public:
    // An unusable vault (no directory); assign a real one before use.
    DeviceKeyVault() = default;

    // `directory` holds the vault file and the device key; it is created on
    // first write.
    DeviceKeyVault(std::string directory, DeviceKeyVaultProfile profile)
        : dir_(std::move(directory)), profile_(std::move(profile)) {}

    const std::string&           GetDirectory() const { return dir_; }
    const DeviceKeyVaultProfile& GetProfile()   const { return profile_; }

    // Path of the encrypted vault file inside the directory.
    std::string VaultPath() const;

    // True when a vault file already exists — the caller asks for the master
    // password (existing vault) or asks the user to choose one (first run).
    bool Exists() const;

    // Open the vault with the master password. On success any 0.1-format vault
    // in the same directory is migrated and its files removed. An empty
    // passphrase is rejected: it would derive a key anyone could reproduce.
    UnlockStatus Unlock(const std::string& passphrase);

    // Unlock without prompting, using the local device key (see the file
    // header). Returns true when the vault is open afterwards:
    //  - device key present  -> unlock with it;
    //  - no key, no vault yet -> generate a key, write it, create the vault;
    //  - no key but a vault exists (migration from a master-password vault)
    //    -> returns false so the caller prompts once, then PersistDeviceKey().
    bool TryAutoUnlock();

    // The process holds one open UltraVault store, so several vaults - one per
    // module, say two messenger providers - take turns: this opens this
    // vault's own file (with the device key, as TryAutoUnlock), runs `use`,
    // and closes it again, under the lock every DeviceKeyVault in the
    // process shares (every other call takes it too, and a vault used
    // directly re-opens its own file when a Use() took the store meanwhile).
    // False (and `use` not run) when the vault cannot be opened.
    bool Use(const std::function<void(DeviceKeyVault& vault)>& use);

    // Write `passphrase` as the device key (owner-only perms), so the next
    // launch unlocks silently. Call after a manual Unlock() succeeds.
    bool PersistDeviceKey(const std::string& passphrase);

    // True once Unlock() has succeeded (and until Lock()): the vault has its
    // key, whether or not the process's one store holds its file right now.
    bool IsUnlocked() const { return unlocked_; }

    // Why the last Unlock() or TryAutoUnlock() left the vault closed; Ok
    // after one that opened it, Locked before any. TryAutoUnlock() reports
    //  - Unavailable when this build has no crypto backend (UltraCrypt
    //    without libsodium): it cannot even make a device key;
    //  - IoError when the directory or the device key cannot be written or
    //    read;
    //  - Locked when a vault exists without a device key (made with a master
    //    password): the caller asks for it once, then PersistDeviceKey();
    //  - whatever Unlock() reported for the device key otherwise.
    // A bool alone told an application only "cannot open the credential
    // vault", whatever the reason.
    UnlockStatus GetLastUnlockStatus() const { return lastStatus_; }

    // `status` in words, for the end of an error message: "this build has no
    // encryption library (UltraCrypt was built without libsodium) ...".
    static std::string DescribeUnlockStatus(UnlockStatus status);

    // Close the vault and wipe the derived key from memory.
    void Lock();

    // Store (or replace) the secret for an account. False when locked or on a
    // write error — never silently drops the secret.
    bool Store(const std::string& account, const std::string& secret);

    // Retrieve the secret; false (and `out` empty) when absent or locked.
    bool Retrieve(const std::string& account, std::string& out) const;

    bool Has(const std::string& account) const;

    // Remove the secret for an account. True if it existed and was removed.
    bool Remove(const std::string& account);

    // OAuth2 token set of an account, kept as three entries beside the
    // password slot. Storing tokens drops a stored password and vice versa,
    // so an account has exactly one way of signing in.
    bool StoreOAuthTokens(const std::string& account, const OAuthTokens& tokens);
    bool RetrieveOAuthTokens(const std::string& account, OAuthTokens& out) const;
    bool HasOAuthTokens(const std::string& account) const;
    bool RemoveOAuthTokens(const std::string& account);

    // OAuth2 when a token set is stored, Password when a password is, else None.
    SignInMethod MethodFor(const std::string& account) const;

    // The UltraVault key for an account: the profile's prefix + the account.
    std::string KeyFor(const std::string& account) const;

private:
    // Import a 0.1-format vault (vault.key + creds.dat) into UltraVault and
    // delete it. Returns the number of secrets carried over.
    int MigrateLegacy();

    // Path of the device key file (the random passphrase, owner-only).
    std::string DeviceKeyPath() const;
    // Before a read or write: the vault is unlocked and the process's store
    // holds its file - re-opened with the device key when another vault took
    // the store since. False, with lastStatus_ saying why, otherwise.
    bool HoldTheStore() const;
    bool ReopenWithDeviceKey();

    std::string           dir_;
    DeviceKeyVaultProfile profile_;
    bool                  unlocked_ = false;
    mutable UnlockStatus  lastStatus_ = UnlockStatus::Locked;   // a read may re-open, and say why it could not
};

} // namespace UltraVault

#endif // ULTRAVAULT_DEVICE_KEY_VAULT_H
