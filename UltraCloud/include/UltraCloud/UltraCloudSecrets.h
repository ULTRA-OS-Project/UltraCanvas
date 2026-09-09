// UltraCloud/include/UltraCloud/UltraCloudSecrets.h
// Where account secrets live. ISecretStore is the seam; VaultSecretStore
// keeps them in UltraVault (the system secret store) and FileSecretStore is
// the per-app fallback: obfuscated files, not strong at-rest encryption.
// Version: 0.1.0
// Last Modified: 2026-09-03
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCloudTypes.h"

#include <string>

namespace UltraCloud {

class ISecretStore {
public:
    virtual ~ISecretStore() = default;
    virtual bool Store(const std::string& accountId, const Credentials& credentials) = 0;
    virtual bool Retrieve(const std::string& accountId, Credentials& out) const = 0;
    virtual bool Remove(const std::string& accountId) = 0;
};

// Obfuscated files under `directory` (one per secret, XOR against a per-store
// key kept beside them). Better than plaintext; the real target is UltraVault.
class FileSecretStore : public ISecretStore {
public:
    explicit FileSecretStore(std::string directory) : dir_(std::move(directory)) {}
    bool Store(const std::string& accountId, const Credentials& credentials) override;
    bool Retrieve(const std::string& accountId, Credentials& out) const override;
    bool Remove(const std::string& accountId) override;
private:
    std::string dir_;
};

#ifdef ULTRACLOUD_USE_ULTRAVAULT
// Secrets in UltraVault under "cloud.<accountId>.password" / ".token".
// UltraVault::Initialize() must have run (and IsAvailable() hold).
class VaultSecretStore : public ISecretStore {
public:
    bool Store(const std::string& accountId, const Credentials& credentials) override;
    bool Retrieve(const std::string& accountId, Credentials& out) const override;
    bool Remove(const std::string& accountId) override;
};
#endif

} // namespace UltraCloud
