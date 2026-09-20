// Apps/UltraSocial/engine/UltraSocialCredentialVault.h
// UltraSocial's per-account credential blobs (OAuth tokens / app passwords /
// bot tokens), keyed by accountId: the framework's device-key vault
// (UltraVault::DeviceKeyVault, see
// UltraCanvas/include/UltraVault/UltraVaultDeviceKeyVault.h) with UltraSocial's
// file name and key prefix. Until 0.1.0 this was a copy of UltraMail's vault
// that still wrote the 0.1 XOR format; a vault in that format is migrated on
// the first TryAutoUnlock() and its files removed.
//
// Layout in the vault folder: `ultrasocial.vault` (the encrypted store) and
// `device.key` (the random passphrase that unlocks it without a prompt). Keys
// are "social.ultrasocial.<accountId>".
// Version: 0.2.0 - profile of UltraVault::DeviceKeyVault (was the 0.1 file format)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <UltraVault/UltraVaultDeviceKeyVault.h>

#include <string>

namespace UltraSocial {

using VaultStatus = UltraVault::UnlockStatus;

// What tells UltraSocial's vault from another application's.
inline const UltraVault::DeviceKeyVaultProfile kVaultProfile{
    /*vaultFileName=*/"ultrasocial.vault",
    /*keyPrefix=*/    "social.ultrasocial."};

class CredentialVault : public UltraVault::DeviceKeyVault {
public:
    CredentialVault() : CredentialVault(std::string{}) {}
    // `directory` holds the vault file and the device key; created on first write.
    explicit CredentialVault(std::string directory)
        : UltraVault::DeviceKeyVault(std::move(directory), kVaultProfile) {}
};

} // namespace UltraSocial
