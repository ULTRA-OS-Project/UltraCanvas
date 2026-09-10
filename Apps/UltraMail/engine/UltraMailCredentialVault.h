// Apps/UltraMail/engine/UltraMailCredentialVault.h
// Per-account secrets (mail passwords / OAuth tokens), stored in UltraVault —
// the framework's credential module (UltraCanvas/include/UltraVault) — rather
// than in UltraMail's own file format. UltraVault's file backend derives its
// key from a passphrase with Argon2id and seals the store with
// XChaCha20-Poly1305 via UltraCrypt, authenticating the header so tampering
// with the stored cost parameters is detected.
//
// The passphrase is UltraMail's master password. It is never written to disk,
// so the vault genuinely cannot be read without the user: Unlock() must
// succeed before Store() / Retrieve() will do anything. That is the difference
// from the 0.1 vault, which XOR-ed secrets against a key file kept beside the
// ciphertext — anyone who could read the folder could recover every password.
//
// Vaults written by that 0.1 format are migrated on the first successful
// unlock (see MigrateLegacy) and the old files are removed.
// Version: 0.5.0 - OAuth2 token sets (access + refresh + expiry) beside passwords
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstdint>
#include <string>

namespace UltraMail {

// The tokens of an account that signed in with OAuth2 (see UltraMailOAuth.h).
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
enum class SignInMethod { None, Password, OAuth2 };

// Why an unlock attempt failed, so the UI can tell "wrong password" (ask
// again) from "this build cannot open a vault at all" (say so and stop).
enum class VaultStatus {
    Ok,
    WrongPassphrase,     // wrong master password, or the vault file was altered
    Unavailable,         // no crypto backend (UltraCrypt without libsodium)
    IoError,             // the vault path cannot be read or written
    Locked               // Unlock() has not run yet
};

class CredentialVault {
public:
    // `directory` holds the vault file; it is created on first write.
    explicit CredentialVault(std::string directory) : dir_(std::move(directory)) {}

    // Path of the encrypted vault file inside `directory`.
    std::string VaultPath() const;

    // True when a vault file already exists — the caller asks for the master
    // password (existing vault) or asks the user to choose one (first run).
    bool Exists() const;

    // Open the vault with the master password. On success any 0.1-format vault
    // in the same directory is migrated and its files removed. An empty
    // passphrase is rejected: it would derive a key anyone could reproduce.
    VaultStatus Unlock(const std::string& passphrase);

    // True once Unlock() has succeeded.
    bool IsUnlocked() const { return unlocked_; }

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

    // The UltraVault key for an account, using the module's namespaced
    // "<vendor>.<app>.<purpose>" convention (UltraAI/Docs/UltraVault.md §4).
    static std::string KeyFor(const std::string& account);

private:
    // Import a 0.1-format vault (vault.key + creds.dat) into UltraVault and
    // delete it. Returns the number of secrets carried over.
    int MigrateLegacy();

    std::string dir_;
    bool        unlocked_ = false;
};

} // namespace UltraMail
