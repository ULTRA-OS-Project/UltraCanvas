// Apps/UltraFiler/UltraFilerRemoteDrives.cpp
// Version: 1.3.0
// Last Modified: 2026-09-27
// Author: UltraCanvas Framework
#include "UltraFilerRemoteDrives.h"
#include "UltraFilerRemoteCache.h"

#include "UltraFilerSettings.h"        // GetConfigDirectory

#include "UltraCanvasApplication.h"    // PostToUIThread
#include "UltraCanvasDiskCache.h"      // the preview copies' cache root

#include "UltraNet/UltraNetCore.h"   // the transfer callbacks a job reports through

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

#ifdef ULTRAFILER_HAS_ULTRACLOUD
#include <UltraCloud/UltraCloud.h>
#include <UltraCloud/UltraCloudAccounts.h>
#include <UltraCloud/UltraCloudProvider.h>
#include <UltraCloud/UltraCloudSecrets.h>
#include <UltraCloud/UltraCloudService.h>
#include <UltraVault/UltraVaultDeviceKeyVault.h>
#endif

namespace UltraCanvas {

namespace fs = std::filesystem;

// ===== THE ULTRACLOUD SIDE =====
// Everything that needs the module is in here, so the class itself compiles
// either way and the header stays free of it.

struct UltraFilerRemoteDrives::Impl {
#ifdef ULTRAFILER_HAS_ULTRACLOUD
    UltraCloud::AccountStore accounts;
    // UltraFiler's own vault: ultrafiler.vault + device.key under the config
    // directory, unlocked without a prompt (UltraVault::DeviceKeyVault). The
    // secret store writes into it under "cloud.<accountId>.*".
    UltraVault::DeviceKeyVault vault;
    std::unique_ptr<UltraCloud::ISecretStore> secrets;
    std::unique_ptr<UltraCloud::CloudService> service;
    bool opened = false;
    std::string openError;

    // Opens the account store, the vault and the secret store once. The
    // accounts are an UltraDatabase file beside UltraFiler's settings; the
    // secrets are in the vault beside it. Earlier builds kept them in
    // obfuscated files under remote-drive-secrets/ - or, once UltraVault was
    // built, in a VaultSecretStore that nothing had ever opened, so they were
    // never saved at all; the files are carried into the vault here.
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
        vault = UltraVault::DeviceKeyVault(dir + "/vault",
                                           {"ultrafiler.vault", "files.ultrafiler."});
        if (!vault.TryAutoUnlock()) {
            openError = "cannot open the credential vault in " + dir + "/vault";
            error = openError;
            return false;
        }
        secrets = std::make_unique<UltraCloud::VaultSecretStore>();
        std::vector<UltraCloud::Account> known;
        accounts.List(known);
        UltraCloud::MigrateLegacyFileSecrets(dir + "/remote-drive-secrets", known, *secrets);
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
            d.canUpload = p->Capabilities().upload;
        }
        d.prefetches = RemoteProviderPrefetches(a.providerId);
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
    // The first time the drives are known is the first time the listings
    // kept from the last run can be matched to them.
    LoadDiskCacheLocked();
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
        it->second.lastUsed = ++useCounter_;
        switch (it->second.state) {
            case CacheState::Ready:
                out = it->second.entries;
                return true;
            case CacheState::Stale: {
                // What the folder held last time, shown now; the server is
                // asked again behind it and the answer replaces it through
                // onListingArrived. Queued as the user's request, not as a
                // prefetch: they are looking at this folder.
                out = it->second.entries;
                if (!it->second.revalidating) {
                    it->second.revalidating = true;
                    Job job;
                    job.isListing = true;
                    job.path = path;
                    queue_.push_back(std::move(job));
                    EnsureWorker();
                    lk.unlock();
                    cond_.notify_one();
                }
                return true;
            }
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
    CacheEntry loading;
    loading.lastUsed = ++useCounter_;
    cache_[path] = std::move(loading);
    Job job;
    job.isListing = true;
    job.path = path;
    queue_.push_back(std::move(job));
    EnsureWorker();
    lk.unlock();
    cond_.notify_one();
    return true;
}

std::string UltraFilerRemoteDrives::ListingStatus(const std::string& path) const {
    std::string accountId, remotePath;
    if (!SplitRemoteFilerPath(path, accountId, remotePath)) return {};

    std::lock_guard<std::mutex> lk(mutex_);
    const auto it = cache_.find(path);
    if (it == cache_.end() || it->second.state != CacheState::Loading) return {};

    // The server as the user knows it: the drive's name, and its address
    // when it has one ("Backup NAS (ftp://nas.local)").
    std::string server = accountId;
    for (const RemoteDrive& d : drives_) {
        if (d.accountId != accountId) continue;
        server = d.displayName.empty() ? d.serverUrl : d.displayName;
        if (!d.displayName.empty() && !d.serverUrl.empty())
            server += " (" + d.serverUrl + ")";
        break;
    }
    // The folder as the server sees it; the drive's root is just "/".
    std::string folder = remotePath;
    if (folder.empty()) folder = "/";

    if (activeJobPath_ == path) {
        const auto waited = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - activeJobSince_).count();
        std::string line = "Connecting to " + server + " and reading " + folder;
        // A server that answers within a moment needs no clock; one that
        // does not gets a count, so a stalled connection looks stalled and
        // not frozen.
        if (waited >= 2) line += " - " + std::to_string(waited) + " s";
        return line;
    }

    // Queued: everything the worker takes before it. A listing already in
    // flight counts as one, whatever it is.
    size_t ahead = activeJobPath_.empty() ? 0 : 1;
    for (const Job& j : queue_) {
        if (j.path == path && j.isListing) break;
        ++ahead;
    }
    if (ahead == 0) return "Waiting for " + server;
    return "Waiting for " + server + " - " + std::to_string(ahead) +
           (ahead == 1 ? " request ahead" : " requests ahead");
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
    // of these do, and which the provider would either refuse or - worse -
    // carry out somewhere the user did not look. Upload is the exception,
    // because its argument is a local path and separators are what it is
    // made of.
    if (operation == RemoteOperation::Upload) {
        std::error_code ec;
        if (argument.empty()) {
            error = "no file given";
            return false;
        }
        // Only a regular file. A folder dropped on a drive is a recursive
        // copy, which this queue cannot report the progress of; saying so is
        // better than uploading the first file and going quiet.
        if (std::filesystem::is_directory(argument, ec) && !ec) {
            error = "a folder cannot be uploaded from here, only files: " +
                    std::filesystem::path(argument).filename().string();
            return false;
        }
        ec.clear();
        if (!std::filesystem::is_regular_file(argument, ec) || ec) {
            error = "not a file: " + argument;
            return false;
        }
    } else if (operation == RemoteOperation::Download) {
        // The full local path to write, chosen by Download() while it still
        // had the local filesystem in front of it.
        if (argument.empty()) {
            error = "no local file given";
            return false;
        }
    } else if (operation != RemoteOperation::Delete) {
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
    // - or uploading into it - is fine.
    if (operation != RemoteOperation::MakeDirectory &&
        operation != RemoteOperation::Upload && remotePath == "/") {
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
        // said no: the answer is the same every time. The two capabilities are
        // asked separately because they differ - a Nextcloud or Dropbox drive
        // can be browsed and uploaded to but not changed in place, so a drive
        // that refuses a rename still takes a file dropped onto it.
        if (operation == RemoteOperation::Upload) {
            if (!drive->canUpload) {
                error = "this drive does not take uploads";
                return false;
            }
        } else if (operation == RemoteOperation::Download) {
            // Reading is what a drive is for: every provider implements
            // Download, and a drive that can be browsed can be copied from.
        } else if (!drive->canModify) {
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

bool UltraFilerRemoteDrives::CanUpload(const std::string& path) const {
    std::string accountId, remotePath;
    if (!SplitRemoteFilerPath(path, accountId, remotePath)) return false;
    if (!Available()) return false;
    std::lock_guard<std::mutex> lk(mutex_);
    for (const RemoteDrive& d : drives_) {
        if (d.accountId == accountId) return d.canUpload;
    }
    return false;
}

bool UltraFilerRemoteDrives::Upload(const std::string& remoteFolder,
                                    const std::string& localFile,
                                    std::string& error) {
    std::string accountId, remotePath;
    if (!SplitRemoteFilerPath(remoteFolder, accountId, remotePath)) {
        error = "not a remote drive: " + remoteFolder;
        return false;
    }
    if (!Available()) {
        error = "this build of UltraFiler carries no cloud support";
        return false;
    }
    // A local path on a server is meaningless, and a folder is not one
    // transfer: the provider verb takes one file.
    if (localFile.empty() || IsRemoteFilerPath(localFile)) {
        error = "only local files can be uploaded";
        return false;
    }
    std::error_code ec;
    if (fs::is_directory(localFile, ec)) {
        error = "folders cannot be uploaded - drop the files inside it";
        return false;
    }
    if (!fs::is_regular_file(localFile, ec) || ec) {
        error = "cannot read " + localFile;
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
        if (!drive->canUpload) {
            error = "this kind of drive cannot take uploads from here";
            return false;
        }
        Job job;
        job.isListing = false;
        job.path = remoteFolder;
        job.operation = RemoteOperation::Upload;
        job.argument = localFile;
        job.isDirectory = false;
        queue_.push_back(std::move(job));
        EnsureWorker();
    }
    cond_.notify_one();
    return true;
}

bool UltraFilerRemoteDrives::Download(const std::string& remoteFile,
                                      const std::string& localFolder,
                                      std::string& savedAs,
                                      std::string& error) {
    savedAs.clear();
    std::string accountId, remotePath;
    if (!SplitRemoteFilerPath(remoteFile, accountId, remotePath)) {
        error = "not a file on a drive: " + remoteFile;
        return false;
    }
    if (!Available()) {
        error = "this build of UltraFiler carries no cloud support";
        return false;
    }
    const std::string name = RemoteFilerName(remoteFile);
    if (name.empty() || remotePath == "/") {
        error = "this is the drive itself, not a file on it";
        return false;
    }
    // Where it is going has to be a folder on this computer: a drive is not a
    // download target (that is an upload, and a different provider verb), and
    // a folder that is not there is a failure the user should hear about now
    // rather than after the round trip.
    if (localFolder.empty() || IsRemoteFilerPath(localFolder)) {
        error = "a file from a drive has to be saved somewhere on this computer";
        return false;
    }
    std::error_code ec;
    if (!fs::is_directory(localFolder, ec) || ec) {
        error = "not a folder on this computer: " + localFolder;
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
        // A folder is not one transfer, the same way it is not one upload.
        // The listing the user dragged the entry out of is still cached, so
        // this costs a lookup rather than a request; an entry that is not in
        // it (a drive listed and then forgotten) is left to the provider,
        // which will refuse to read a directory as a file.
        const std::string parent = RemoteFilerParent(remoteFile);
        auto it = cache_.find(parent);
        if (it != cache_.end() && (it->second.state == CacheState::Ready ||
                                   it->second.state == CacheState::Stale)) {
            for (const FilerEntry& e : it->second.entries) {
                if (e.path != remoteFile) continue;
                if (e.isDirectory) {
                    error = "folders cannot be downloaded - open it and take "
                            "the files inside";
                    return false;
                }
                break;
            }
        }
    }

    // The name is settled here, on the UI thread, where the local folder can
    // be looked at: the widget's own "Keep both" rule, so a download never
    // overwrites a file that is already there and the copy is called what a
    // paste would have called it. The rule is asked about this queue as well
    // as about the disk - a file promised by a job that has not run yet is
    // not on the disk, and two of one name queued together would otherwise
    // both be told the name was free.
    {
        std::lock_guard<std::mutex> lk(mutex_);
        savedAs = UltraCanvasFilerWidget::UniquePathIn(
                localFolder, name, [this](const std::string& candidate) {
            return promisedDownloads_.count(candidate) > 0;
        });
        promisedDownloads_.insert(savedAs);

        Job job;
        job.isListing = false;
        job.path = remoteFile;
        job.operation = RemoteOperation::Download;
        job.argument = savedAs;
        job.isDirectory = false;
        queue_.push_back(std::move(job));
        EnsureWorker();
    }
    cond_.notify_one();
    return true;
}

std::string UltraFilerRemoteDrives::PreviewCacheDirectory() {
    return DiskCache::Directory("remote-previews");
}

namespace {

// Deletes the preview copies nobody has looked at within the disk cache's
// usual age, and any ".part" a download interrupted by a crash left behind.
// Each copy has a folder of its own (so the viewer browses nothing else), and
// DiskCache::Sweep does not descend, hence this: the same rule, one level
// down. A copy's stamp is its file's time, refreshed by DiskCache::Touch on
// every look.
void SweepPreviewCache(const std::string& directory) {
    if (directory.empty()) return;
    const auto now = fs::file_time_type::clock::now();
    const auto maxAge = std::chrono::duration_cast<fs::file_time_type::duration>(
            DiskCache::kDefaultMaxAge);
    std::error_code ec;
    std::vector<fs::path> stale;
    for (fs::directory_iterator it(directory, ec), end; it != end && !ec;
         it.increment(ec)) {
        std::error_code dec;
        if (!it->is_directory(dec) || dec) continue;
        bool fresh = false;
        for (fs::directory_iterator f(it->path(), dec), fend; f != fend && !dec;
             f.increment(dec)) {
            std::error_code fec;
            if (f->path().extension() == ".part") {
                fs::remove(f->path(), fec);
                continue;
            }
            const auto t = fs::last_write_time(f->path(), fec);
            // A stamp in the future reads as fresh, as DiskCache::Sweep has it.
            if (!fec && (t > now || now - t < maxAge)) fresh = true;
        }
        if (!fresh) stale.push_back(it->path());
    }
    for (const fs::path& d : stale) {
        std::error_code rec;
        fs::remove_all(d, rec);
    }
}

} // namespace

UltraFilerRemoteDrives::PreviewCopy UltraFilerRemoteDrives::RequestPreviewCopy(
        const FilerEntry& entry, std::string& localPath, std::string& error) {
    localPath.clear();
    std::string accountId, remotePath;
    if (!SplitRemoteFilerPath(entry.path, accountId, remotePath) ||
        entry.isDirectory || remotePath == "/") {
        error = "not a file on a drive";
        return PreviewCopy::Failed;
    }
    if (!Available()) {
        error = "this build of UltraFiler carries no cloud support";
        return PreviewCopy::Failed;
    }
    if (entry.size > kRemotePreviewMaxBytes) {
        error = "too large to fetch for a preview";
        return PreviewCopy::TooLarge;
    }
    const std::string directory = PreviewCacheDirectory();
    if (directory.empty()) {
        error = "there is no cache folder to fetch a preview into";
        return PreviewCopy::Failed;
    }
    if (!previewCacheSwept_) {
        previewCacheSwept_ = true;
        SweepPreviewCache(directory);
    }

    const std::string name = RemotePreviewLocalName(
            entry.name.empty() ? RemoteFilerName(entry.path) : entry.name);
    const fs::path target = fs::path(directory) /
            RemotePreviewCacheKey(entry.path, entry.size, entry.modifiedTime) / name;
    const std::string targetPath = target.string();

    // Fetched before - in this run or an earlier one - and unchanged since,
    // since a change would have given it another folder.
    std::error_code ec;
    if (fs::is_regular_file(target, ec) && !ec) {
        DiskCache::Touch(targetPath);
        localPath = targetPath;
        return PreviewCopy::Ready;
    }

    {
        std::lock_guard<std::mutex> lk(mutex_);
        bool known = false;
        for (const RemoteDrive& d : drives_) {
            if (d.accountId == accountId) { known = true; break; }
        }
        if (!known) {
            error = "this drive is no longer configured";
            return PreviewCopy::Failed;
        }

        auto it = previews_.find(entry.path);
        if (it != previews_.end() && it->second.localPath == targetPath) {
            if (it->second.pending) return PreviewCopy::Pending;
            if (!it->second.error.empty()) {
                error = it->second.error;
                return PreviewCopy::Failed;
            }
        }

        // The selection has moved on: a preview download that has not
        // started is for a file nobody is looking at any more. One that has
        // started is left to finish - it is the nearest thing to done.
        for (auto q = queue_.begin(); q != queue_.end();) {
            if (q->isPreview && q->path != entry.path) {
                previews_.erase(q->path);
                q = queue_.erase(q);
            } else {
                ++q;
            }
        }

        Job job;
        job.isListing = false;
        job.isPreview = true;
        job.operation = RemoteOperation::Download;
        job.path = entry.path;
        job.argument = targetPath + ".part";
        job.previewTarget = targetPath;
        // At the front: the user is looking at the selection now, and a batch
        // of uploads queued earlier should not stand between them and it.
        queue_.push_front(std::move(job));
        previews_[entry.path] = PreviewState{true, targetPath, {}};
        EnsureWorker();
    }
    cond_.notify_one();
    return PreviewCopy::Pending;
}

void UltraFilerRemoteDrives::RunOperation(const Job& job) {
#ifndef ULTRAFILER_HAS_ULTRACLOUD
    std::lock_guard<std::mutex> lk(mutex_);
    lastOperationError_ = "this build of UltraFiler carries no cloud support";
#else
    std::string accountId, remotePath;
    if (!SplitRemoteFilerPath(job.path, accountId, remotePath)) return;

    // A preview copy's folder is made here, off the UI thread, and only once
    // the download is really going to happen.
    if (job.isPreview) {
        std::error_code ec;
        fs::create_directories(fs::path(job.previewTarget).parent_path(), ec);
    }

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
        case RemoteOperation::Upload: {
            // Under the file's own name, in the folder the job names.
            const std::string parent = remotePath == "/" ? std::string()
                                                         : remotePath;
            const std::string name = fs::path(job.argument).filename().string();
            r = impl_->service->Upload(accountId, job.argument, parent + "/" + name);
            break;
        }
        case RemoteOperation::Download:
            // The full local path was settled when the job was queued.
            r = impl_->service->Download(accountId, remotePath, job.argument);
            break;
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
    // A Refresh is also "try again" for a preview in that folder that failed.
    for (auto it = previews_.begin(); it != previews_.end();) {
        if (!it->second.pending && RemoteFilerParent(it->first) == path)
            it = previews_.erase(it);
        else
            ++it;
    }
}

void UltraFilerRemoteDrives::InvalidateAll() {
    std::lock_guard<std::mutex> lk(mutex_);
    cache_.clear();
    for (auto it = previews_.begin(); it != previews_.end();) {
        if (!it->second.pending) it = previews_.erase(it);
        else ++it;
    }
}

std::size_t UltraFilerRemoteDrives::PrefetchQueueSize() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return prefetchQueue_.size();
}

bool UltraFilerRemoteDrives::DrivePrefetchesLocked(const std::string& accountId,
                                                   bool* canModify) const {
    for (const RemoteDrive& d : drives_) {
        if (d.accountId != accountId) continue;
        if (canModify) *canModify = d.canModify;
        return d.prefetches;
    }
    return false;
}

void UltraFilerRemoteDrives::QueuePrefetchLocked(
        const std::string& folderPath, const std::vector<FilerEntry>& entries) {
    if (shutdown_) return;
    if (!DrivePrefetchesLocked(RemoteFilerAccountId(folderPath))) return;

    // Known: anything cached that the server has answered for this session,
    // or that is already on its way. A listing kept from the last run is not
    // known - fetching it ahead is what brings it up to date before it is
    // opened.
    std::unordered_set<std::string> known = prefetchQueued_;
    for (const auto& [path, entry] : cache_) {
        if (entry.state != CacheState::Stale || entry.revalidating)
            known.insert(path);
    }
    std::vector<RemoteCachedEntry> candidates;
    candidates.reserve(entries.size());
    for (const FilerEntry& e : entries) {
        RemoteCachedEntry c;
        c.name = e.name;
        c.path = e.path;
        c.isDirectory = e.isDirectory;
        candidates.push_back(std::move(c));
    }
    const std::vector<std::string> targets =
            SelectRemotePrefetchTargets(candidates, known);
    if (targets.empty()) return;

    // To the front, in listing order: the folder the user has just opened is
    // where they will go next, not the one they opened a minute ago.
    for (auto it = targets.rbegin(); it != targets.rend(); ++it) {
        prefetchQueue_.push_front(*it);
        prefetchQueued_.insert(*it);
    }
    // Bounded: a user walking quickly through a big tree leaves behind
    // folders they are no longer near. The oldest are the ones dropped.
    const std::size_t cap = kRemotePrefetchPerFolder * 4;
    while (prefetchQueue_.size() > cap) {
        prefetchQueued_.erase(prefetchQueue_.back());
        prefetchQueue_.pop_back();
    }
    EnsureWorker();
}

bool UltraFilerRemoteDrives::TakePrefetchLocked(Job& job) {
    while (!prefetchQueue_.empty()) {
        std::string path = std::move(prefetchQueue_.front());
        prefetchQueue_.pop_front();
        prefetchQueued_.erase(path);

        // The drive may have gone, or the user may have opened the folder
        // meanwhile - its own listing is then already queued or in.
        if (!DrivePrefetchesLocked(RemoteFilerAccountId(path))) continue;
        auto it = cache_.find(path);
        if (it != cache_.end()) {
            if (it->second.state != CacheState::Stale || it->second.revalidating)
                continue;
            // Kept from the last run: stays on show while it is checked.
            it->second.revalidating = true;
        } else {
            cache_[path] = CacheEntry{};   // Loading
        }
        job = Job{};
        job.isListing = true;
        job.isPrefetch = true;
        job.path = std::move(path);
        return true;
    }
    return false;
}

std::string UltraFilerRemoteDrives::DiskCachePath() {
    return UltraFilerSettings::GetConfigDirectory() + "/remote-listings.cache";
}

void UltraFilerRemoteDrives::LoadDiskCacheLocked() {
    if (diskCacheLoaded_) return;
    diskCacheLoaded_ = true;

    std::ifstream in(DiskCachePath(), std::ios::binary);
    if (!in) return;   // the first run, or the cache was deleted: nothing kept
    std::ostringstream text;
    text << in.rdbuf();
    std::vector<RemoteCachedListing> listings;
    if (!ParseRemoteListings(text.str(), listings)) return;

    // The file lists the most recently used first. Each listing is stamped
    // below every use this session will make, in that order, so the next
    // save keeps what was used last time ahead of what was only carried over.
    useCounter_ = std::max<uint64_t>(useCounter_, listings.size());
    uint64_t stamp = listings.size();
    for (const RemoteCachedListing& l : listings) {
        const uint64_t lastUsed = stamp--;
        bool canModify = false;
        // Only for a drive that is still configured and still keeps listings;
        // an account removed since then leaves its listings behind unread,
        // and the next save drops them.
        if (!DrivePrefetchesLocked(RemoteFilerAccountId(l.folderPath), &canModify))
            continue;
        if (cache_.count(l.folderPath)) continue;
        CacheEntry entry;
        entry.state = CacheState::Stale;
        entry.entries.reserve(l.entries.size());
        for (const RemoteCachedEntry& c : l.entries) {
            // An entry is kept only inside the drive of its folder; a line
            // that names another drive is damage, not data.
            if (RemoteFilerAccountId(c.path) != RemoteFilerAccountId(l.folderPath))
                continue;
            FilerEntry f;
            f.name = c.name;
            f.path = c.path;
            f.isDirectory = c.isDirectory;
            f.isHidden = IsHiddenRemoteFilerName(c.name);
            f.size = c.size;
            f.modifiedTime = c.modifiedTime;
            // Asked of the drive now, not remembered: the badge follows what
            // the provider can do today.
            f.isReadOnly = !canModify;
            entry.entries.push_back(std::move(f));
        }
        entry.lastUsed = lastUsed;
        cache_[l.folderPath] = std::move(entry);
    }
}

void UltraFilerRemoteDrives::SaveDiskCache() {
    std::vector<RemoteCachedListing> listings;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (diskCacheSaved_ || !diskCacheLoaded_) return;
        diskCacheSaved_ = true;

        struct Candidate { uint64_t lastUsed; const std::string* path;
                           const CacheEntry* entry; };
        std::vector<Candidate> candidates;
        for (const auto& [path, entry] : cache_) {
            if (entry.state != CacheState::Ready && entry.state != CacheState::Stale)
                continue;
            if (!DrivePrefetchesLocked(RemoteFilerAccountId(path))) continue;
            candidates.push_back({entry.lastUsed, &path, &entry});
        }
        // Most recently used first, so a full cache keeps what the user was
        // actually working in; by path among equals, so the file is stable.
        std::sort(candidates.begin(), candidates.end(),
                  [](const Candidate& a, const Candidate& b) {
                      if (a.lastUsed != b.lastUsed) return a.lastUsed > b.lastUsed;
                      return *a.path < *b.path;
                  });
        if (candidates.size() > kRemoteCacheMaxListings)
            candidates.resize(kRemoteCacheMaxListings);

        listings.reserve(candidates.size());
        for (const Candidate& c : candidates) {
            RemoteCachedListing l;
            l.folderPath = *c.path;
            l.entries.reserve(c.entry->entries.size());
            for (const FilerEntry& f : c.entry->entries) {
                RemoteCachedEntry e;
                e.name = f.name;
                e.path = f.path;
                e.isDirectory = f.isDirectory;
                e.size = f.size;
                e.modifiedTime = f.modifiedTime;
                l.entries.push_back(std::move(e));
            }
            listings.push_back(std::move(l));
        }
    }

    // Written beside the target and renamed over it, so a crash half way
    // leaves last run's cache rather than half of this one's.
    const std::string target = DiskCachePath();
    const std::string temp = target + ".tmp";
    std::error_code ec;
    fs::create_directories(fs::path(target).parent_path(), ec);
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) return;
        out << SerializeRemoteListings(listings);
        if (!out) { out.close(); fs::remove(temp, ec); return; }
    }
    fs::rename(temp, target, ec);
    if (ec) fs::remove(temp, ec);
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
        prefetchQueue_.clear();
        prefetchQueued_.clear();
    }
    cond_.notify_all();
    if (worker_.joinable()) worker_.join();
    // After the join: the worker writes the cache, and what it was fetching
    // when asked to stop is either in by now or never will be.
    SaveDiskCache();
}

void UltraFilerRemoteDrives::EnsureWorker() {
    // Called with the lock held. The thread's first act is to wait on the
    // condition variable, which needs that same lock, so it simply blocks
    // until the caller releases it.
    if (workerStarted_ || shutdown_) return;
    workerStarted_ = true;
    worker_ = std::thread([this]() { WorkerMain(); });
}

RemoteActivity::Kind UltraFilerRemoteDrives::ActivityKindFor(const Job& job) {
    if (job.isListing) return RemoteActivity::Kind::Listing;
    switch (job.operation) {
        case RemoteOperation::Delete:        return RemoteActivity::Kind::Deleting;
        case RemoteOperation::Rename:        return RemoteActivity::Kind::Renaming;
        case RemoteOperation::MakeDirectory: return RemoteActivity::Kind::MakingDirectory;
        case RemoteOperation::Upload:        return RemoteActivity::Kind::Uploading;
        case RemoteOperation::Download:      return RemoteActivity::Kind::Downloading;
    }
    return RemoteActivity::Kind::Idle;
}

void UltraFilerRemoteDrives::ReportActivity(const RemoteActivity& activity,
                                            bool force) {
    if (!onActivityChanged) return;
    // libcurl counts bytes, so a transfer would otherwise post hundreds of
    // times a second at a UI thread that can only repaint sixty. The first
    // and last report of a job are forced through: those are the ones that
    // say what started and that it is over.
    const auto now = std::chrono::steady_clock::now();
    if (!force) {
        const auto since = now - lastActivityPost_;
        if (since < std::chrono::milliseconds(80)) return;
    }
    lastActivityPost_ = now;

    UltraCanvasApplicationBase* app = UltraCanvasApplicationBase::GetCurrent();
    if (!app) return;
    auto alive = alive_;
    app->PostToUIThread([this, alive, activity]() {
        if (!alive->load()) return;   // owner destroyed meanwhile
        if (onActivityChanged) onActivityChanged(activity);
    });
}

void UltraFilerRemoteDrives::WorkerMain() {
    for (;;) {
        Job job;
        std::size_t waiting = 0;
        {
            std::unique_lock<std::mutex> lk(mutex_);
            cond_.wait(lk, [this]() {
                return shutdown_ || !queue_.empty() || !prefetchQueue_.empty();
            });
            if (shutdown_) return;
            if (!queue_.empty()) {
                job = std::move(queue_.front());
                queue_.pop_front();
            } else if (!TakePrefetchLocked(job)) {
                continue;   // every queued prefetch had been overtaken
            }
            waiting = queue_.size();
            // main's ListingStatus reads these to describe the job the folder
            // display is waiting on; the activity report below is the other
            // half of the same story, for the status strip.
            activeJobPath_ = job.path;
            activeJobSince_ = std::chrono::steady_clock::now();
        }

        // What this job is, said before it starts rather than after: the whole
        // point is to fill the wait, and a report that arrives with the answer
        // fills nothing.
        RemoteActivity activity;
        activity.kind = ActivityKindFor(job);
        activity.queued = waiting;
        activity.what = job.isListing
                ? RemoteFilerName(job.path)   // "" at a drive root: its own name
                : job.operation == RemoteOperation::Upload
                        ? std::filesystem::path(job.argument).filename().string()
                        : job.operation == RemoteOperation::MakeDirectory
                                ? job.argument
                                : RemoteFilerName(job.path);
        // A prefetch is not something the user asked for, so the status line
        // does not mention it: it would read as the drive being busy with
        // folders nobody opened.
        if (!job.isPrefetch) ReportActivity(activity, /*force=*/true);

        // A transfer counts its own bytes. UltraNet reports them through the
        // module's global transfer callbacks, which is why the previous bag is
        // put back afterwards rather than simply cleared: this process shares
        // them with every other UltraNet caller, and an upload is no reason to
        // deafen the rest of the application.
        UltraNetTransferCallbacks previousCallbacks;
        const bool watchesBytes = !job.isListing &&
                                  (job.operation == RemoteOperation::Upload ||
                                   job.operation == RemoteOperation::Download);
        if (watchesBytes) {
            // One counter for either direction: what the status line says
            // about a transfer is the same either way, and which way it is
            // going is already in the activity's kind.
            auto count = [this, activity](int64_t moved, int64_t total) {
                RemoteActivity moving = activity;
                moving.bytesDone = moved > 0 ? static_cast<uint64_t>(moved) : 0;
                // A server that sent no length reports -1; that is the busy
                // case, not a total of zero bytes to move.
                moving.bytesTotal = total > 0 ? static_cast<uint64_t>(total) : 0;
                ReportActivity(moving, /*force=*/false);
            };
            UltraNetTransferCallbacks bag;
            if (job.operation == RemoteOperation::Upload) bag.onUploadProgress = count;
            else                                          bag.onDownloadProgress = count;
            previousCallbacks = UltraNet_SetTransferCallbacks(bag);
        }

        // An exception leaving a std::thread ends the process, and a provider
        // is network code: whatever it throws costs this one job and no more.
        // A change reports its failure through operationError so the UI hears
        // the same thing whether the provider refused or threw.
        std::string operationError;
        bool notifyListing = true;
        try {
            if (job.isListing) notifyListing = FetchListing(job.path, job.isPrefetch);
            else               RunOperation(job);
        } catch (const std::exception& e) {
            if (job.isListing) {
                std::lock_guard<std::mutex> lk(mutex_);
                notifyListing = RecordListingFailureLocked(
                        job.path, job.isPrefetch,
                        std::string("listing failed: ") + e.what());
            } else {
                operationError = std::string("the operation failed: ") + e.what();
            }
        } catch (...) {
            if (job.isListing) {
                std::lock_guard<std::mutex> lk(mutex_);
                notifyListing = RecordListingFailureLocked(job.path, job.isPrefetch,
                                                           "listing failed");
            } else {
                operationError = "the operation failed";
            }
        }

        {
            std::lock_guard<std::mutex> lk(mutex_);
            activeJobPath_.clear();
        }

        // A preview copy changed nothing anybody is looking at: it is renamed
        // into place if it arrived whole, and the window is told either way.
        if (job.isPreview) {
            std::string previewError = operationError;
            {
                std::lock_guard<std::mutex> lk(mutex_);
                if (previewError.empty()) previewError = lastOperationError_;
                lastOperationError_.clear();
            }
            std::error_code ec;
            if (previewError.empty()) {
                fs::rename(job.argument, job.previewTarget, ec);
                if (ec) previewError = "cannot store the preview: " + ec.message();
                // Stamped now, whatever time the transfer gave the file, so
                // the sweep counts its age from this look.
                else DiskCache::Touch(job.previewTarget, std::chrono::seconds(0));
            }
            if (!previewError.empty()) fs::remove(job.argument, ec);
            {
                std::lock_guard<std::mutex> lk(mutex_);
                if (previewError.empty()) previews_.erase(job.path);
                else previews_[job.path] = PreviewState{false, job.previewTarget,
                                                        previewError};
            }
            if (watchesBytes) UltraNet_SetTransferCallbacks(previousCallbacks);
            std::size_t left = 0;
            {
                std::lock_guard<std::mutex> lk(mutex_);
                left = queue_.size();
            }
            if (left == 0) ReportActivity(RemoteActivity{}, /*force=*/true);
            if (UltraCanvasApplicationBase* app = UltraCanvasApplicationBase::GetCurrent()) {
                auto alive = alive_;
                const std::string path = job.path;
                app->PostToUIThread([this, alive, path]() {
                    if (!alive->load()) return;
                    if (onPreviewCopyReady) onPreviewCopyReady(path);
                });
            }
            continue;
        }

        // A change that got as far as the server invalidates the folder it
        // touched, so the refresh below refetches instead of repainting what
        // the cache still holds. Done even on failure: a half-applied change
        // is exactly when the cached listing is least trustworthy.
        std::string changedFolder;
        if (!job.isListing) {
            if (job.operation == RemoteOperation::Download) {
                // A download changes nothing on the drive - the folder that
                // gained a file is the local one the bytes were written to.
                // The cache erase below then finds no such key, which is
                // exactly right: the drive's listing is still good.
                changedFolder = fs::path(job.argument).parent_path().string();
            } else {
                changedFolder = (job.operation == RemoteOperation::MakeDirectory ||
                                 job.operation == RemoteOperation::Upload)
                        ? job.path                       // the folder created / uploaded in
                        : RemoteFilerParent(job.path);   // the entry's own folder
            }
            if (changedFolder.empty()) changedFolder = job.path;
            std::lock_guard<std::mutex> lk(mutex_);
            // The name is the disk's business again: written if the transfer
            // worked, free to be handed out again if it did not.
            if (job.operation == RemoteOperation::Download)
                promisedDownloads_.erase(job.argument);
            cache_.erase(changedFolder);
            if (operationError.empty() && !lastOperationError_.empty())
                operationError = lastOperationError_;
            lastOperationError_.clear();
        }

        if (watchesBytes) UltraNet_SetTransferCallbacks(previousCallbacks);

        // Idle only when nothing is left: between two files of one drop the
        // status line should say what is still coming, not blink back to
        // nothing and out again.
        {
            std::size_t left = 0;
            {
                std::lock_guard<std::mutex> lk(mutex_);
                left = queue_.size();
            }
            if (left == 0 && !job.isPrefetch)
                ReportActivity(RemoteActivity{}, /*force=*/true);
        }

        // Tell the window on the UI thread. Posted rather than called: this is
        // a worker, and everything it would touch in the display belongs to
        // the UI thread.
        // A prefetch nobody was waiting for that failed changed nothing on
        // screen, and telling the window would have the tree take the empty
        // answer for a folder with nothing in it.
        if (job.isListing && !notifyListing) continue;
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

bool UltraFilerRemoteDrives::RecordListingFailureLocked(const std::string& path,
                                                        bool isPrefetch,
                                                        const std::string& error) {
    auto it = cache_.find(path);
    if (isPrefetch && it != cache_.end()) {
        // Kept from the last run: still the best there is to show, and the
        // folder is asked again when it is opened.
        if (it->second.state == CacheState::Stale) {
            it->second.revalidating = false;
            return false;
        }
        // Fetched ahead and nobody has asked for it meanwhile: forget the
        // failure, so opening the folder asks the server afresh rather than
        // showing an error from a moment the user never saw. Someone who did
        // ask (List stamped lastUsed while it was loading) is owed the
        // answer, and gets it below like any other listing.
        if (it->second.state == CacheState::Loading && it->second.lastUsed == 0) {
            cache_.erase(it);
            return false;
        }
    }
    CacheEntry failed;
    failed.state = CacheState::Failed;
    failed.error = error;
    failed.lastUsed = it != cache_.end() ? it->second.lastUsed : 0;
    cache_[path] = std::move(failed);
    return true;
}

bool UltraFilerRemoteDrives::FetchListing(const std::string& path, bool isPrefetch) {
#ifndef ULTRAFILER_HAS_ULTRACLOUD
    (void)isPrefetch;
    std::lock_guard<std::mutex> lk(mutex_);
    cache_[path] = CacheEntry{CacheState::Failed, {},
                              "this build of UltraFiler carries no cloud support"};
    return true;
#else
    std::string accountId, remotePath;
    if (!SplitRemoteFilerPath(path, accountId, remotePath)) return false;

    // Whether this drive can be changed decides the read-only badge on every
    // entry of it, so it is read once here rather than per entry.
    bool canModify = false;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        DrivePrefetchesLocked(accountId, &canModify);
    }

    std::vector<UltraCloud::Entry> entries;
    const UltraCloud::Result r =
            impl_->service->List(accountId, remotePath, entries);

    if (!r.IsOk()) {
        // The provider's own words: "530 Login incorrect" tells the user what
        // to change, where "could not list" tells them nothing.
        std::lock_guard<std::mutex> lk(mutex_);
        return RecordListingFailureLocked(
                path, isPrefetch,
                r.message.empty() ? "cannot list this folder"
                                  : "cannot list this folder: " + r.message);
    }

    CacheEntry result;
    result.state = CacheState::Ready;
    result.entries.reserve(entries.size());
    for (const UltraCloud::Entry& e : entries) {
        FilerEntry f;
        f.name = e.name;
        f.path = MakeRemoteFilerPath(accountId, e.path);
        f.isDirectory = e.isDirectory;
        // The Unix convention, which is what a server lists: a dot-entry is
        // hidden the way a local one is (Display > Hidden files shows it),
        // rather than shown on a drive and hidden on a disk. The rule itself
        // lives next to the path scheme, where it can be tested without a
        // server.
        f.isHidden = IsHiddenRemoteFilerName(e.name);
        f.size = e.isDirectory ? 0 : static_cast<uint64_t>(e.size < 0 ? 0 : e.size);
        f.modifiedTime = ParseRemoteFilerTime(e.modified);
        // The display draws its read-only badge from this, so it has to
        // follow what the drive can actually do: an FTP drive can be
        // changed, a Nextcloud or Dropbox one cannot (yet) and says so on
        // every entry rather than only when a command is tried.
        f.isReadOnly = !canModify;
        result.entries.push_back(std::move(f));
    }

    std::lock_guard<std::mutex> lk(mutex_);
    auto it = cache_.find(path);
    // A prefetch keeps whatever use stamp the entry had: fetching ahead is
    // not the user using it. A listing they asked for is fresh use.
    result.lastUsed = isPrefetch ? (it != cache_.end() ? it->second.lastUsed : 0)
                                 : ++useCounter_;
    // One level ahead of what the user opens, never a prefetch of a prefetch:
    // that is how "the subfolders" stays a couple of dozen listings instead
    // of a crawl of the whole server.
    if (!isPrefetch) QueuePrefetchLocked(path, result.entries);
    cache_[path] = std::move(result);
    return true;
#endif
}

} // namespace UltraCanvas
