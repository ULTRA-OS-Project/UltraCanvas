// UltraCloud/include/UltraCloud/UltraCloudSecrets.h
// Where account secrets live. ISecretStore is the seam; VaultSecretStore
// keeps them in UltraVault — the store the application already opened, which
// for an UltraCanvas application is its UltraVault::DeviceKeyVault — and
// MemorySecretStore is the process-lifetime store for tests and demos.
//
// There is no file store of UltraCloud's own any more: the obfuscated
// per-app files (XOR against a cloud.key beside them) were a third copy of
// the weak format UltraMail and UltraSocial have both left behind. What
// remains of it is MigrateLegacyFileSecrets(), which carries such a
// directory into a real store once and removes it.
// Version: 0.3.0 - file store replaced by the vault + a one-way migration
// Last Modified: 2026-09-22
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCloudTypes.h"

#include <map>
#include <string>
#include <vector>

namespace UltraCloud {

class ISecretStore {
public:
    virtual ~ISecretStore() = default;
    virtual bool Store(const std::string& accountId, const Credentials& credentials) = 0;
    virtual bool Retrieve(const std::string& accountId, Credentials& out) const = 0;
    virtual bool Remove(const std::string& accountId) = 0;
};

// Secrets in UltraVault under "cloud.<accountId>.username|password|token|
// refresh|expires". The vault must be open (UltraVault::IsAvailable()): an
// application opens its own with UltraVault::DeviceKeyVault::TryAutoUnlock()
// before it touches its accounts. Every call refuses — never drops — while
// the vault is closed.
class VaultSecretStore : public ISecretStore {
public:
    bool Store(const std::string& accountId, const Credentials& credentials) override;
    bool Retrieve(const std::string& accountId, Credentials& out) const override;
    bool Remove(const std::string& accountId) override;
};

// Process-lifetime store, nothing persisted: tests, demos, and the in-memory
// provider.
class MemorySecretStore : public ISecretStore {
public:
    bool Store(const std::string& accountId, const Credentials& credentials) override;
    bool Retrieve(const std::string& accountId, Credentials& out) const override;
    bool Remove(const std::string& accountId) override;
private:
    std::map<std::string, Credentials> secrets_;
};

// Carry the secrets an earlier build kept in `directory` (UltraCloud's
// obfuscated per-account files, "<accountId>.secret" + "cloud.key") into
// `into`, one account at a time, and delete each file once its secret is
// stored; the key file goes when no secret file is left. Only the accounts
// listed are looked for — the file names are derived from the account ids —
// so pass the account store's list. Returns the number carried across; 0 when
// the directory does not exist. Safe to call on every start-up.
int MigrateLegacyFileSecrets(const std::string& directory,
                             const std::vector<Account>& accounts,
                             ISecretStore& into);

} // namespace UltraCloud
