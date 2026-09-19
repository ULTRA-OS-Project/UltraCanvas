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
    Job job;
    job.isListing = true;
    job.path = path;
    queue_.push_back(std::move(job));
    EnsureWorker();
    lk.unlock();
    cond_.notify_one();
    return true;
}

bool UltraFilerRemoteDrives::Submit(RemoteOperation operation,
                                    const std::string& path,
                                    const std::string& argument,
                                    bool isDirectory,
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

    // A name is a name: one carrying a separator would be a move, which none
    // of these three do, and which the provider would either refuse or - worse
    // - carry out somewhere the user did not look.
    if (operation != RemoteOperation::Delete) {
        if (argument.empty()) {
            error = "no name given";
            return false;
        }
        if (argument.find('/') != std::string::npos ||
            argument.find('\\') != std::string::npos) {
            error = "a name cannot contain a path separator";
            return false;
        }
    }
    // The drive's own root is not ours to delete or rename; creating inside it
    // is fine.
    if (operation != RemoteOperation::MakeDirectory && remotePath == "/") {
        error = "this is the drive itself, not something on it";
        return false;
    }

    {
        std::lock_guard<std::mutex> lk(mutex_);
        const RemoteDrive* drive = nullptr;
        for (const RemoteDrive& d : drives_) {
            if (d.accountId == accountId) { drive = &d; break; }
        }
        if (!drive) {
            error = "this drive is no longer configured";
            return false;
        }
        // Asked before the request is queued rather than after the server has
        // said no: a Nextcloud or Dropbox drive can be browsed and uploaded to
        // but not changed in place, and the answer is the same every time.
        if (!drive->canModify) {
            error = "this kind of drive cannot be changed from here";
            return false;
        }

        Job job;
        job.isListing = false;
        job.path = path;
        job.operation = operation;
        job.argument = argument;
        job.isDirectory = isDirectory;
        queue_.push_back(std::move(job));
        EnsureWorker();
    }
    cond_.notify_one();
    return true;
}

void UltraFilerRemoteDrives::RunOperation(const Job& job) {
#ifndef ULTRAFILER_HAS_ULTRACLOUD
    std::lock_guard<std::mutex> lk(mutex_);
    lastOperationError_ = "this build of UltraFiler carries no cloud support";
#else
    std::string accountId, remotePath;
    if (!SplitRemoteFilerPath(job.path, accountId, remotePath)) return;

    UltraCloud::Result r = UltraCloud::Result::Ok();
    switch (job.operation) {
        case RemoteOperation::Delete:
            r = impl_->service->Delete(accountId, remotePath, job.isDirectory);
            break;
        case RemoteOperation::Rename:
            r = impl_->service->Rename(accountId, remotePath, job.argument);
            break;
        case RemoteOperation::MakeDirectory: {
            // The provider takes the full path of the folder to create, so the
            // name is appended to the folder it goes in.
            const std::string parent = remotePath == "/" ? std::string()
                                                         : remotePath;
            r = impl_->service->MakeDirectory(accountId, parent + "/" + job.argument);
            break;
        }
    }

    std::lock_guard<std::mutex> lk(mutex_);
    // The provider's own words where there are any: "550 Permission denied"
    // tells the user what to change, "the operation failed" tells them nothing.
    lastOperationError_ = r.IsOk() ? std::string()
                        : r.message.empty() ? "the server refused this"
                                            : r.message;
#endif
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
        Job job;
        {
            std::unique_lock<std::mutex> lk(mutex_);
            cond_.wait(lk, [this]() { return shutdown_ || !queue_.empty(); });
            if (shutdown_) return;
            job = std::move(queue_.front());
            queue_.pop_front();
        }

        // An exception leaving a std::thread ends the process, and a provider
        // is network code: whatever it throws costs this one job and no more.
        // A change reports its failure through operationError so the UI hears
        // the same thing whether the provider refused or threw.
        std::string operationError;
        try {
            if (job.isListing) FetchListing(job.path);
            else               RunOperation(job);
        } catch (const std::exception& e) {
            if (job.isListing) {
                std::lock_guard<std::mutex> lk(mutex_);
                cache_[job.path] = CacheEntry{CacheState::Failed, {},
                                              std::string("listing failed: ") + e.what()};
            } else {
                operationError = std::string("the operation failed: ") + e.what();
            }
        } catch (...) {
            if (job.isListing) {
                std::lock_guard<std::mutex> lk(mutex_);
                cache_[job.path] = CacheEntry{CacheState::Failed, {}, "listing failed"};
            } else {
                operationError = "the operation failed";
            }
        }

        // A change that got as far as the server invalidates the folder it
        // touched, so the refresh below refetches instead of repainting what
        // the cache still holds. Done even on failure: a half-applied change
        // is exactly when the cached listing is least trustworthy.
        std::string changedFolder;
        if (!job.isListing) {
            changedFolder = job.operation == RemoteOperation::MakeDirectory
                    ? job.path                       // the folder created in
                    : RemoteFilerParent(job.path);   // the entry's own folder
            if (changedFolder.empty()) changedFolder = job.path;
            std::lock_guard<std::mutex> lk(mutex_);
            cache_.erase(changedFolder);
            if (operationError.empty() && !lastOperationError_.empty())
                operationError = lastOperationError_;
            lastOperationError_.clear();
        }

        // Tell the window on the UI thread. Posted rather than called: this is
        // a worker, and everything it would touch in the display belongs to
        // the UI thread.
        if (UltraCanvasApplicationBase* app = UltraCanvasApplicationBase::GetCurrent()) {
            auto alive = alive_;
            const bool listing = job.isListing;
            const std::string path = job.path;
            app->PostToUIThread([this, alive, listing, path, changedFolder,
                                 operationError]() {
                if (!alive->load()) return;   // owner destroyed meanwhile
                if (listing) {
                    if (onListingArrived) onListingArrived(path);
                } else if (onOperationFinished) {
                    onOperationFinished(changedFolder, operationError);
                }
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

    // Whether this drive can be changed decides the read-only badge on every
    // entry of it, so it is read once here rather than per entry.
    bool canModify = false;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        for (const RemoteDrive& d : drives_) {
            if (d.accountId == accountId) { canModify = d.canModify; break; }
        }
    }

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
            // The display draws its read-only badge from this, so it has to
            // follow what the drive can actually do: an FTP drive can be
            // changed, a Nextcloud or Dropbox one cannot (yet) and says so on
            // every entry rather than only when a command is tried.
            f.isReadOnly = !canModify;
            result.entries.push_back(std::move(f));
        }
    }

    std::lock_guard<std::mutex> lk(mutex_);
    cache_[path] = std::move(result);
#endif
}

} // namespace UltraCanvas
