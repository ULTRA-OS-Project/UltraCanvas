// Apps/UltraSocial/engine/UltraSocialCredentialVault.h
// Per-account credential blobs (OAuth tokens / app passwords / bot tokens)
// out of the config files, keyed by accountId. Same fallback file backend as
// UltraMail's vault: secrets obfuscated with a per-vault key — better than
// plaintext but NOT strong at-rest encryption (the key lives beside the
// data). The target backend is the OS keychain / UltraVault behind this same
// Store/Retrieve/Remove interface (see UltraAI/Docs/UltraVault.md).
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <string>

namespace UltraSocial {

class CredentialVault {
public:
    explicit CredentialVault(std::string directory) : dir_(std::move(directory)) {}

    // Store (or replace) the secret for an account key. False on I/O error.
    bool Store(const std::string& accountId, const std::string& secret);

    // Retrieve the secret; false (and `out` empty) if absent.
    bool Retrieve(const std::string& accountId, std::string& out) const;

    bool Has(const std::string& accountId) const;

    // Remove the secret. True if it existed.
    bool Remove(const std::string& accountId);

private:
    std::string dir_;
};

} // namespace UltraSocial
