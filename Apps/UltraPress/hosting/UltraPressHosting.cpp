// Apps/UltraPress/hosting/UltraPressHosting.cpp
// See UltraPressHosting.h.
// Version: 0.5.1 - the remove verb for pruning
// Version: 0.5.0
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraPressHosting.h"

#include <UltraCloud/UltraCloudMemory.h>

#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasSettingsFolder.h"
#include "UltraVault/UltraVaultDeviceKeyVault.h"
#if ULTRAPRESS_HAS_ULTRANET
#include "UltraNet/UltraNetCore.h"
#endif

#include <filesystem>

namespace fs = std::filesystem;

namespace UltraPress {

struct Hosts::Impl {
    UltraCloud::AccountStore accounts;
    UltraVault::DeviceKeyVault vault;
    std::unique_ptr<UltraCloud::ISecretStore> secrets;
    std::unique_ptr<UltraCloud::CloudService> service;
    bool opened = false;
};

Hosts::Hosts() : impl(std::make_unique<Impl>()) {}
Hosts::~Hosts() = default;

std::string Hosts::ConfigDirectory() {
    const fs::path folder = UltraCanvas::UltraCanvasSettingsFolder();
    if (folder.empty()) return "";
    return UltraCanvas::PathToUtf8(folder / "UltraPress");
}

bool Hosts::IsHostProvider(const std::string& providerId) {
    return providerId == "ftp" || providerId == "webdav" || providerId == "nextcloud";
}

bool Hosts::IsOpen() const { return impl->opened; }

bool Hosts::Open(std::string& error) {
    if (impl->opened) return true;
    UltraCloud::RegisterBuiltInProviders();
#if ULTRAPRESS_HAS_ULTRANET
    // The FTP and HTTP calls go through UltraNet; initialising twice is a
    // no-op, so the import command's own call and this one do not collide.
    const UltraNetResult net = UltraNet_Initialize();
    if (!net.success) {
        error = "UltraNet: " + net.message;
        return false;
    }
#endif
    const std::string dir = ConfigDirectory();
    if (dir.empty()) {
        error = "no settings folder to keep the host list in (no home directory)";
        return false;
    }
    // SQLite creates the file but not the folders above it; the vault makes
    // its own folder on its first write.
    std::error_code ec;
    fs::create_directories(UltraCanvas::PathFromUtf8(dir), ec);
    if (ec) {
        error = "cannot create " + dir + ": " + ec.message();
        return false;
    }
    const UltraCloud::Result opened = impl->accounts.Open("ultrapress-hosts", dir + "/hosts.db");
    if (!opened) {
        error = "cannot open the host list " + dir + "/hosts.db: " + opened.message;
        return false;
    }
    impl->vault = UltraVault::DeviceKeyVault(dir + "/vault", {"ultrapress.vault", "press.ultrapress."});
    if (!impl->vault.TryAutoUnlock()) {
        error = "cannot open the credential vault in " + dir + "/vault: " +
                UltraVault::DeviceKeyVault::DescribeUnlockStatus(impl->vault.GetLastUnlockStatus());
        return false;
    }
    impl->secrets = std::make_unique<UltraCloud::VaultSecretStore>();
    impl->service = std::make_unique<UltraCloud::CloudService>(impl->accounts, *impl->secrets);
    impl->opened = true;
    return true;
}

bool Hosts::OpenInMemory() {
    if (impl->opened) return true;
    UltraCloud::RegisterProvider(std::make_shared<UltraCloud::MemoryProvider>());
    if (!impl->accounts.Open("ultrapress-hosts-memory", ":memory:")) return false;
    impl->secrets = std::make_unique<UltraCloud::MemorySecretStore>();
    impl->service = std::make_unique<UltraCloud::CloudService>(impl->accounts, *impl->secrets);
    impl->opened = true;
    return true;
}

UltraCloud::CloudService& Hosts::Service() { return *impl->service; }

std::vector<UltraCloud::Account> Hosts::List() {
    std::vector<UltraCloud::Account> out;
    if (impl->opened) impl->accounts.List(out);
    return out;
}

UltraCloud::Result Hosts::Find(const std::string& idOrName, UltraCloud::Account& out) {
    if (!impl->opened) return UltraCloud::Result::Error(UltraCloud::ResultCode::Unknown, "the host list is not open");
    if (impl->accounts.Get(idOrName, out)) return UltraCloud::Result::Ok();
    for (const UltraCloud::Account& account : List()) {
        if (account.displayName == idOrName) {
            out = account;
            return UltraCloud::Result::Ok();
        }
    }
    return UltraCloud::Result::Error(UltraCloud::ResultCode::NotFound, "no host '" + idOrName + "'");
}

UltraCloud::Result Hosts::Add(UltraCloud::Account& account, const UltraCloud::Credentials& credentials, bool verify) {
    if (!impl->opened) return UltraCloud::Result::Error(UltraCloud::ResultCode::Unknown, "the host list is not open");
    return impl->service->AddAccount(account, credentials, verify);
}

UltraCloud::Result Hosts::Remove(const std::string& accountId) {
    if (!impl->opened) return UltraCloud::Result::Error(UltraCloud::ResultCode::Unknown, "the host list is not open");
    return impl->service->RemoveAccount(accountId);
}

Uploader Hosts::UploaderFor(const std::string& accountId) {
    Uploader uploader;
    UltraCloud::CloudService* service = impl->opened ? impl->service.get() : nullptr;
    uploader.makeDirectory = [service, accountId](const std::string& remotePath, std::string& error) {
        if (!service) { error = "the host list is not open"; return false; }
        const UltraCloud::Result r = service->MakeDirectory(accountId, remotePath);
        if (!r) error = r.message;
        return r.IsOk();
    };
    uploader.upload = [service, accountId](const std::string& localPath, const std::string& remotePath,
                                           std::string& error) {
        if (!service) { error = "the host list is not open"; return false; }
        const UltraCloud::Result r = service->Upload(accountId, localPath, remotePath);
        if (!r) error = r.message;
        return r.IsOk();
    };
    uploader.remove = [service, accountId](const std::string& remotePath, bool isDirectory, std::string& error) {
        if (!service) { error = "the host list is not open"; return false; }
        const UltraCloud::Result r = service->Delete(accountId, remotePath, isDirectory);
        if (!r) error = r.message;
        return r.IsOk();
    };
    return uploader;
}

std::string DescribeHost(const UltraCloud::Account& account) {
    std::string line = account.accountId + "  " + account.providerId;
    if (!account.serverUrl.empty()) line += "  " + account.serverUrl;
    if (!account.username.empty()) line += "  " + account.username;
    if (!account.remoteFolder.empty()) line += "  folder " + account.remoteFolder;
    if (!account.displayName.empty() && account.displayName != account.accountId) line += "  \"" + account.displayName + "\"";
    if (account.isDefault) line += "  (default)";
    return line;
}

} // namespace UltraPress
