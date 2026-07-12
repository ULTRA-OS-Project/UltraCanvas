// Apps/UltraMail/engine/UltraMailCredentialVault.h
// Stores per-account secrets (passwords / OAuth tokens) out of the config
// files. This is the *fallback* file backend: secrets are obfuscated with a
// per-vault key, which is better than plaintext but is NOT strong at-rest
// encryption (the key lives beside the data). The real target is the OS
// keychain (libsecret / Windows Credential Manager / macOS Keychain) — those
// plug in behind this same Store/Retrieve/Remove interface.
// Version: 0.1.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <string>

namespace UltraMail {

class CredentialVault {
public:
    explicit CredentialVault(std::string directory) : dir_(std::move(directory)) {}

    // Store (or replace) the secret for an account key. Returns false on I/O error.
    bool Store(const std::string& account, const std::string& secret);

    // Retrieve the secret; returns false (and leaves `out` empty) if absent.
    bool Retrieve(const std::string& account, std::string& out) const;

    bool Has(const std::string& account) const;

    // Remove the secret for an account. Returns true if it existed.
    bool Remove(const std::string& account);

private:
    std::string dir_;
};

} // namespace UltraMail
