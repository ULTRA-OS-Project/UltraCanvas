// Apps/UltraFiler/UltraFilerRemoteDrives.cpp
// Version: 1.0.0
// Last Modified: 2026-09-17
// Author: UltraCanvas Framework
#include "UltraFilerRemoteDrives.h"

#include "UltraFilerSettings.h"        // GetConfigDirectory

#include "UltraCanvasApplication.h"    // PostToUIThread

#include <algorithm>

#ifdef ULTRAFILER_HAS_ULTRACLOUD
#include <UltraCloud/UltraCloud.h>
#include <UltraCloud/UltraCloudAccounts.h>
#include <UltraCloud/UltraCloudProvider.h>
#include <UltraCloud/UltraCloudSecrets.h>
#include <UltraCloud/UltraCloudService.h>
#endif

namespace UltraCanvas {

// ===== THE ULTRACLOUD SIDE =====
// Everything that needs the module is in here, so the class itself compiles
// either way and the header stays free of it.

struct UltraFilerRemoteDrives::Impl {
#ifdef ULTRAFILER_HAS_ULTRACLOUD
    UltraCloud::AccountStore accounts;
    std::unique_ptr<UltraCloud::ISecretStore> secrets;
    std::unique_ptr<UltraCloud::CloudService> service;
    bool opened = false;
    std::string openError;

    // Opens the account store and the secret store once. The accounts are an
    // UltraDatabase file beside UltraFiler's settings; the secrets go to
    // UltraVault where it is built, and to the obfuscated per-app file only
    // when it is not - which is weaker, and is why it is the fallback rather
    // than the default.
    bool Open(std::string& error) {
        if (opened) return true;
        if (!openError.empty()) { error = openError; return false; }

        UltraCloud::RegisterBuiltInProviders();

        const std::string dir = UltraFilerSettings::GetConfigDirectory();
        if (dir.empty()) {
            openError = "no configuration directory to keep the drive list in";
            error = openError;
            return false;
        }
        const UltraCloud::Result r =
                accounts.Open("ultrafiler-cloud", dir + "/remote-drives.db");
        if (!r.IsOk()) {
            openError = "cannot open the drive list: " + r.message;
            error = openError;
            return false;
        }
#ifdef ULTRACLOUD_USE_ULTRAVAULT
        secrets = std::make_unique<UltraCloud::VaultSecretStore>();
#else
        secrets = std::make_unique<UltraCloud::FileSecretStore>(dir + "/remote-drive-secrets");
#endif
        service = std::make_unique<UltraCloud::CloudService>(accounts, *secrets);
        opened = true;
        return true;
    }
#endif
};

UltraFilerRemoteDrives::UltraFilerRemoteDrives()
    : impl_(std::make_unique<Impl>()) {}

UltraFilerRemoteDrives::~UltraFilerRemoteDrives() {
    // The worker posts into this object, so it must be gone before the
    // members it touches are. alive_ neutralises a UI-thread callback that
    // was already queued and cannot be recalled.
    alive_->store(false);
    Stop();
}

bool UltraFilerRemoteDrives::Available() {
#ifdef ULTRAFILER_HAS_ULTRACLOUD
    return true;
#else
    return false;
#endif
}

bool UltraFilerRemoteDrives::ProviderBelongsToKind(const std::string& providerId,
                                                    RemoteDriveKind kind) {
    // "ftp" is the one provider that speaks FTP, FTPS, FTPES and SFTP; the
    // rest - Nextcloud, WebDAV, Dropbox, OneDrive, Google Drive - are what
    // the Cloud Storage choice offers. The in-process "memory" provider is a
    // test fake and belongs in neither list.
    if (providerId == "memory") return false;
    const bool isFtp = providerId == "ftp";
    return kind == RemoteDriveKind::FtpOrSftp ? isFtp : !isFtp;
}

bool UltraFilerRemoteDrives::Reload(std::string& error) {
#ifndef ULTRAFILER_HAS_ULTRACLOUD
    (void)error;
    return true;   // nothing to load; the drive list stays empty
#else
    if (!impl_->Open(error)) return false;

    std::vector<UltraCloud::Account> accounts;
    const UltraCloud::Result r = impl_->accounts.List(accounts);
    if (!r.IsOk()) {
        error = "cannot read the drive list: " + r.message;
        return false;
    }

    std::vector<RemoteDrive> drives;
    drives.reserve(accounts.size());
    for (const UltraCloud::Account& a : accounts) {
        RemoteDrive d;
        d.accountId = a.accountId;
        d.providerId = a.providerId;
        // A drive with no name of its own is still a drive: fall back to what
        // identifies it, so the tree never shows a blank row.
        d.displayName = !a.displayName.empty() ? a.displayName
                      : !a.serverUrl.empty()   ? a.serverUrl
                                               : a.accountId;
        d.serverUrl = a.serverUrl;
        d.rootPath = MakeRemoteFilerPath(a.accountId, "/");
        if (std::shared_ptr<UltraCloud::ICloudProvider> p =
                UltraCloud::GetProvider(a.providerId)) {
            d.canModify = p->Capabilities().modify;
        }
        drives.push_back(std::move(d));
    }
    // By name, so the rows do not move about between runs; the account store
    // orders by "default first", which is not what a list of places wants.
    std::sort(drives.begin(), drives.end(),
              [](const RemoteDrive& a, const RemoteDrive& b) {
                  return a.displayName < b.displayName;
              });

    std::lock_guard<std::mutex> lk(mutex_);
    drives_ = std::move(drives);
    return true;
#endif
}

std::vector<RemoteDrive> UltraFilerRemoteDrives::Drives() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return drives_;
}

bool UltraFilerRemoteDrives::Find(const std::string& accountId,
                                  RemoteDrive& out) const {
    std::lock_guard<std::mutex> lk(mutex_);
    for (const RemoteDrive& d : drives_) {
        if (d.accountId == accountId) { out = d; return true; }
    }
    return false;
}

bool UltraFilerRemoteDrives::List(const std::string& path,
                                  std::vector<FilerEntry>& out,
                                  std::string& error) {
    std::string accountId, remotePath;
    if (!SplitRemoteFilerPath(path, accountId, remotePath)) {
        error = "not a remote drive: " + path;
        return false;
    }
    if (!Available()) {
        error = "this build of UltraFiler carries no cloud support";
        return false;
    }

    std::unique_lock<std::mutex> lk(mutex_);

    // An account that is gone - removed while its folder was still open -
    // must say so rather than sit on "loading" for ever.
    bool known = false;
    for (const RemoteDrive& d : drives_) {
        if (d.accountId == accountId) { known = true; break; }
    }
    if (!known) {
        lk.unlock();
        error = "this drive is no longer configured";
        return false;
    }

    const auto it = cache_.find(path);
    if (it != cache_.end()) {
        switch (it->second.state) {
            case CacheState::Ready:
                out = it->second.entries;
                return true;
            case CacheState::Failed:
                error = it->second.error;
                return false;
            case CacheState::Loading:
                // The honest answer while the fetch is in flight: nothing
                // yet, and no error. The display shows an empty folder for
                // the moment and the arriving listing refreshes it.
                return true;
        }
    }

    // A miss. Remember that it is being fetched BEFORE releasing the lock, so
    // a second scan of the same folder joins this fetch instead of queueing
    // another - the folder display scans more than once per navigation.
    cache_[path] = CacheEntry{CacheState::Loading, {}, {}};
    queue_.push_back(path);
    EnsureWorker();
    lk.unlock();
    cond_.notify_one();
    return true;
}

void UltraFilerRemoteDrives::Invalidate(const std::string& path) {
    std::lock_guard<std::mutex> lk(mutex_);
    cache_.erase(path);
}

void UltraFilerRemoteDrives::InvalidateAll() {
    std::lock_guard<std::mutex> lk(mutex_);
    cache_.clear();
}

UltraCloud::CloudService* UltraFilerRemoteDrives::Service() {
#ifndef ULTRAFILER_HAS_ULTRACLOUD
    return nullptr;
#else
    return impl_->service.get();
#endif
}

void UltraFilerRemoteDrives::Stop() {
    {
        std::lock_guard<std::mutex> lk(mutex_);
        shutdown_ = true;
        queue_.clear();
    }
    cond_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void UltraFilerRemoteDrives::EnsureWorker() {
    // Called with the lock held. The thread's first act is to wait on the
    // condition variable, which needs that same lock, so it simply blocks
    // until the caller releases it.
    if (workerStarted_ || shutdown_) return;
    workerStarted_ = true;
    worker_ = std::thread([this]() { WorkerMain(); });
}

void UltraFilerRemoteDrives::WorkerMain() {
    for (;;) {
        std::string path;
        {
            std::unique_lock<std::mutex> lk(mutex_);
            cond_.wait(lk, [this]() { return shutdown_ || !queue_.empty(); });
            if (shutdown_) return;
            path = std::move(queue_.front());
            queue_.pop_front();
        }
        // An exception leaving a std::thread ends the process, and a provider
        // is network code: whatever it throws costs this one listing.
        try {
            FetchListing(path);
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lk(mutex_);
            cache_[path] = CacheEntry{CacheState::Failed, {},
                                      std::string("listing failed: ") + e.what()};
        } catch (...) {
            std::lock_guard<std::mutex> lk(mutex_);
            cache_[path] = CacheEntry{CacheState::Failed, {}, "listing failed"};
        }

        // Tell the window on the UI thread. Posted rather than called: this is
        // a worker, and everything it would touch in the display belongs to
        // the UI thread.
        if (UltraCanvasApplicationBase* app = UltraCanvasApplicationBase::GetCurrent()) {
            auto alive = alive_;
            app->PostToUIThread([this, alive, path]() {
                if (!alive->load()) return;   // owner destroyed meanwhile
                if (onListingArrived) onListingArrived(path);
            });
        }
    }
}

void UltraFilerRemoteDrives::FetchListing(const std::string& path) {
#ifndef ULTRAFILER_HAS_ULTRACLOUD
    std::lock_guard<std::mutex> lk(mutex_);
    cache_[path] = CacheEntry{CacheState::Failed, {},
                              "this build of UltraFiler carries no cloud support"};
#else
    std::string accountId, remotePath;
    if (!SplitRemoteFilerPath(path, accountId, remotePath)) return;

    std::vector<UltraCloud::Entry> entries;
    const UltraCloud::Result r =
            impl_->service->List(accountId, remotePath, entries);

    CacheEntry result;
    if (!r.IsOk()) {
        result.state = CacheState::Failed;
        // The provider's own words: "530 Login incorrect" tells the user what
        // to change, where "could not list" tells them nothing.
        result.error = r.message.empty()
                ? "cannot list this folder"
                : "cannot list this folder: " + r.message;
    } else {
        result.state = CacheState::Ready;
        result.entries.reserve(entries.size());
        for (const UltraCloud::Entry& e : entries) {
            FilerEntry f;
            f.name = e.name;
            f.path = MakeRemoteFilerPath(accountId, e.path);
            f.isDirectory = e.isDirectory;
            f.size = e.isDirectory ? 0 : static_cast<uint64_t>(e.size < 0 ? 0 : e.size);
            f.modifiedTime = ParseRemoteFilerTime(e.modified);
            // Nothing on a remote drive is writable through this build yet,
            // and the display draws the read-only badge from this.
            f.isReadOnly = true;
            result.entries.push_back(std::move(f));
        }
    }

    std::lock_guard<std::mutex> lk(mutex_);
    cache_[path] = std::move(result);
#endif
}

} // namespace UltraCanvas
