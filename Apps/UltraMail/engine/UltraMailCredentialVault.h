// Apps/UltraMail/engine/UltraMailCredentialVault.h
// UltraMail's per-account secrets (mail passwords / OAuth token sets): the
// framework's device-key vault (UltraVault::DeviceKeyVault, see
// UltraCanvas/include/UltraVault/UltraVaultDeviceKeyVault.h) with UltraMail's
// file name and key prefix. The class used to live here in full and was copied
// into UltraSocial; the implementation is the framework's now, and this header
// only names the vault.
//
// Layout in the account folder: `ultramail.vault` (the encrypted store) and
// `device.key` (the random passphrase that unlocks it without a prompt). Keys
// are "mail.ultramail.<account>" plus ".oauth.access|refresh|expires".
// Version: 0.7.0 - profile of UltraVault::DeviceKeyVault (was 0.6.0 in-app)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <UltraVault/UltraVaultDeviceKeyVault.h>

#include <string>

namespace UltraMail {

using OAuthTokens  = UltraVault::OAuthTokens;
using SignInMethod = UltraVault::SignInMethod;
using VaultStatus  = UltraVault::UnlockStatus;

// What tells UltraMail's vault from another application's.
inline const UltraVault::DeviceKeyVaultProfile kVaultProfile{
    /*vaultFileName=*/"ultramail.vault",
    /*keyPrefix=*/    "mail.ultramail."};

class CredentialVault : public UltraVault::DeviceKeyVault {
public:
    CredentialVault() : CredentialVault(std::string{}) {}
    // `directory` holds the vault file and the device key; created on first write.
    explicit CredentialVault(std::string directory)
        : UltraVault::DeviceKeyVault(std::move(directory), kVaultProfile) {}
};

} // namespace UltraMail
