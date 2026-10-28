// Apps/UltraMail/engine/UltraMailSecureKeys.cpp
// See UltraMailSecureKeys.h.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailSecureKeys.h"

namespace UltraMail {

namespace {
std::string CertEntry(const std::string& accountId) { return accountId + ".smime.cert"; }
std::string KeyEntry(const std::string& accountId) { return accountId + ".smime.key"; }
} // namespace

bool StoreSmimeIdentity(CredentialVault& vault, const std::string& accountId, const SmimeIdentity& identity) {
    if (accountId.empty() || identity.certificatePem.empty() || identity.privateKeyPem.empty()) return false;
    // The key first: a certificate without its key is never left behind.
    if (!vault.Store(KeyEntry(accountId), identity.privateKeyPem)) return false;
    if (!vault.Store(CertEntry(accountId), identity.certificatePem)) {
        vault.Remove(KeyEntry(accountId));
        return false;
    }
    return true;
}

bool LoadSmimeIdentity(const CredentialVault& vault, const std::string& accountId, SmimeIdentity& out) {
    out = SmimeIdentity{};
    return !accountId.empty() && vault.Retrieve(CertEntry(accountId), out.certificatePem)
           && vault.Retrieve(KeyEntry(accountId), out.privateKeyPem);
}

bool RemoveSmimeIdentity(CredentialVault& vault, const std::string& accountId) {
    const bool cert = vault.Remove(CertEntry(accountId));
    const bool key = vault.Remove(KeyEntry(accountId));
    return cert || key;
}

} // namespace UltraMail
