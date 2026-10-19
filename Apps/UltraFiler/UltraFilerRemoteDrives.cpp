// Apps/UltraFiler/UltraFilerRemoteDrives.cpp
// Version: 1.11.0 - one abort check per thread, the preview's cancel in it;
//                   Revalidate goes ahead of the queue, and beside a running
//                   transfer on a thread of its own
// Version: 1.10.0 - PendingTransfers; Stop() calls a running transfer off; a
//                   failed download leaves no half-written file; an upload
//                   under a name of its own; CachedListing, Revalidate
// Version: 1.9.0 - a running preview copy can be called off (CancelActivePreviewLocked)
// Version: 1.8.0
// Last Modified: 2026-10-19
// Author: UltraCanvas Framework
#include "UltraFilerRemoteDrives.h"
#include "UltraFilerRemoteCache.h"

#include "UltraFilerSettings.h"        // GetConfigDirectory

#include "UltraCanvasApplication.h"    // PostToUIThread
#include "UltraCanvasDiskCache.h"      // the preview copies' cache root

#include "UltraNet/UltraNetCore.h"   // the transfer callbacks a job reports through
#include "UltraNet/UltraNetFtp.h"    // the FTP session log a job records

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "UltraCanvasPathUtf8.h"

#ifdef ULTRAFILER_HAS_ULTRACLOUD
#include <UltraCloud/UltraCloud.h>
#include <UltraCloud/UltraCloudAccounts.h>
#include <UltraCloud/UltraCloudHttp.h>   // RateLimitPolicy
#include <UltraCloud/UltraCloudLog.h>
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
    // Shared rather than owned: the service keeps the account store and the
    // secret store alive, and the add-drive dialog's sign-in thread keeps the
    // service alive - a browser sign-in can still be ending after the window
    // that started it has closed and this object is gone.
    std::shared_ptr<UltraCloud::AccountStore> accounts =
            std::make_shared<UltraCloud::AccountStore>();
    // UltraFiler's own vault: ultrafiler.vault + device.key under the config
    // directory, unlocked without a prompt (UltraVault::DeviceKeyVault). The
    // secret store writes into it under "cloud.<accountId>.*" - through
    // UltraVault's process-wide store, which this object only opens, so the
    // secret store does not depend on it staying.
    UltraVault::DeviceKeyVault vault;
    std::shared_ptr<UltraCloud::ISecretStore> secrets;
    std::shared_ptr<UltraCloud::CloudService> service;
    bool opened = false;

    // Opens the account store, the vault and the secret store once. The
    // accounts are an UltraDatabase file beside UltraFiler's settings; the
    // secrets are in the vault beside it. Earlier builds kept them in
    // obfuscated files under remote-drive-secrets/ - or, once UltraVault was
    // built, in a VaultSecretStore that nothing had ever opened, so they were
    // never saved at all; the files are carried into the vault here.
    //
    // A failure is not remembered: the next Reload - "+ Drive" makes one
    // before it opens the add dialog - tries again, so a folder that could
    // not be written at start-up does not cost the drives for the rest of
    // the session. Trying again is safe: re-registering the database
    // connection replaces the earlier one, and the vault is made afresh.
    bool Open(std::string& error) {
        if (opened) return true;

        UltraCloud::RegisterBuiltInProviders();

        const std::string dir = UltraFilerSettings::GetConfigDirectory();
        if (dir.empty()) {
            error = "no configuration directory to keep the drive list in";
            return false;
        }
        // On a fresh profile nothing has written UltraFiler's folder yet at
        // start-up - the settings, History and Favorites files appear only
        // once something is saved - and SQLite does not create the folders
        // above a database file. Without this the drive list could not be
        // opened on a first run, and the failure being kept made the remote
        // drives - "+ Drive" included - unavailable until a restart. The
        // vault makes its own folder (DeviceKeyVault creates `vault/` when
        // it first writes).
        std::error_code ec;
        fs::create_directories(PathFromUtf8(dir), ec);
        if (ec) {
            error = "cannot create the configuration folder " + dir + ": " + ec.message();
            return false;
        }
        const UltraCloud::Result r =
                accounts->Open("ultrafiler-cloud", dir + "/remote-drives.db");
        if (!r.IsOk()) {
            error = "cannot open the drive list: " + r.message;
            return false;
        }
        vault = UltraVault::DeviceKeyVault(dir + "/vault",
                                           {"ultrafiler.vault", "files.ultrafiler."});
        if (!vault.TryAutoUnlock()) {
            // Why, not only that: "this build has no encryption library" is
            // a different fix from a folder that cannot be written.
            error = "cannot open the credential vault in " + dir + "/vault: " +
                    UltraVault::DeviceKeyVault::DescribeUnlockStatus(
                            vault.GetLastUnlockStatus());
            return false;
        }
        secrets = std::make_shared<UltraCloud::VaultSecretStore>();
        std::vector<UltraCloud::Account> known;
        accounts->List(known);
        UltraCloud::MigrateLegacyFileSecrets(dir + "/remote-drive-secrets", known, *secrets);
        service = std::make_shared<UltraCloud::CloudService>(accounts, secrets);
        opened = true;
        return true;
    }
#endif
};

namespace {

// The name an upload gets on the drive: the one it was given, or the local
// file's own.
std::string UploadedName(const std::string& localFile, const std::string& remoteName) {
    return !remoteName.empty() ? remoteName : PathToUtf8(PathFromUtf8(localFile).filename());
}

#ifdef ULTRAFILER_HAS_ULTRACLOUD
// UltraCloud's class of a failure, in the words the connection log shows,
// with the number it has in the module - the error code a bug report quotes.
std::string DescribeResultCode(UltraCloud::ResultCode code) {
    std::string words;
    switch (code) {
        case UltraCloud::ResultCode::Ok:              words = "No error"; break;
        case UltraCloud::ResultCode::NotFound:        words = "Not found"; break;
        case UltraCloud::ResultCode::AuthFailed:
            words = "Sign-in refused - the server did not accept the credentials"; break;
        case UltraCloud::ResultCode::Unsupported:     words = "Not supported by this drive"; break;
        case UltraCloud::ResultCode::Network:
            words = "Network - the server could not be reached, or stopped answering"; break;
        case UltraCloud::ResultCode::Server:
            words = "Server - the server answered with a refusal"; break;
        case UltraCloud::ResultCode::InvalidArgument:
            words = "Invalid request - an address, path or name it cannot use"; break;
        case UltraCloud::ResultCode::IoError:         words = "Local file error"; break;
        case UltraCloud::ResultCode::Unknown:         words = "Unknown"; break;
        case UltraCloud::ResultCode::RateLimited:
            words = "Rate limited - the service asks to wait before more requests"; break;
        case UltraCloud::ResultCode::Cancelled:
            words = "Cancelled"; break;
    }
    return words + " (UltraCloud code " + std::to_string(static_cast<int>(code)) + ")";
}
#endif

// A line of UltraNet's FTP session log as the connection log keeps it.
RemoteLogLine ToRemoteLogLine(const UltraNetFtpLogLine& in) {
    RemoteLogLine line;
    switch (in.kind) {
        case UltraNetFtpLogKind::Step:     line.kind = RemoteLogLine::Kind::Step; break;
        case UltraNetFtpLogKind::Command:  line.kind = RemoteLogLine::Kind::Command; break;
        case UltraNetFtpLogKind::Response: line.kind = RemoteLogLine::Kind::Response; break;
        case UltraNetFtpLogKind::Error:    line.kind = RemoteLogLine::Kind::Error; break;
    }
    line.text = in.text;
    line.replyCode = in.replyCode;
    line.transportCode = in.transportCode;
    return line;
}

#ifdef ULTRAFILER_HAS_ULTRACLOUD
// A line of a cloud provider's log (UltraCloudLog.h) as the connection log
// keeps it: a request is the session's command, the HTTP status the reply's
// code.
RemoteLogLine ToRemoteLogLine(const UltraCloud::LogLine& in) {
    RemoteLogLine line;
    switch (in.kind) {
        case UltraCloud::LogKind::Step:     line.kind = RemoteLogLine::Kind::Step; break;
        case UltraCloud::LogKind::Request:  line.kind = RemoteLogLine::Kind::Command; break;
        case UltraCloud::LogKind::Response: line.kind = RemoteLogLine::Kind::Response; break;
        case UltraCloud::LogKind::Error:    line.kind = RemoteLogLine::Kind::Error; break;
    }
    line.text = in.text;
    line.replyCode = in.httpStatus;
    return line;
}
#endif

// Whether a drive of this provider speaks HTTP - every one but FTP / SFTP.
bool ProviderSpeaksHttp(const std::string& providerId) {
    return providerId != "ftp";
}

// The same line as the status bar's step: a status as it is, the rest with
// what kind of line it is in front, the way an FTP client's log reads.
// `http`: a cloud drive's line, whose commands are requests.
std::string StepText(const RemoteLogLine& line, bool http) {
    switch (line.kind) {
        case RemoteLogLine::Kind::Step:     return line.text;
        // A request's URL is long; the status line gets its host and the end
        // of its path, the connection log the whole of it.
        case RemoteLogLine::Kind::Command:
            return http ? "Request: " + ShortHttpRequestText(line.text) : "Command: " + line.text;
        case RemoteLogLine::Kind::Response: return "Response: " + line.text;
        case RemoteLogLine::Kind::Error:    return "Error: " + line.text;
    }
    return line.text;
}

} // namespace

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
    const UltraCloud::Result r = impl_->accounts->List(accounts);
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
            d.partialUploadsRemain = p->Capabilities().partialUploadsRemain;
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
                    CancelActivePreviewLocked(PreviewYield);   // the user's request first
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
    CancelActivePreviewLocked(PreviewYield);   // the user's request first
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

    // Fetched beside the transfer the worker is in the middle of.
    if (sideListingPaths_.count(path)) return "Reading " + folder + " from " + server;

    if (activeJobPath_ == path) {
        const auto waited = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - activeJobSince_).count();
        // The step the connection is at, once it has logged one: where a
        // stalled connection stalls ("Command: MLSD" - 25 s) is the thing
        // the user needs to read off this notice.
        std::string line = activeJobStep_.empty()
                ? "Connecting to " + server + " and reading " + folder
                : "Reading " + folder + " from " + server + " - " + activeJobStep_;
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

bool UltraFilerRemoteDrives::CachedListing(const std::string& path,
                                           std::vector<FilerEntry>& out) const {
    out.clear();
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = cache_.find(path);
    if (it == cache_.end()) return false;
    if (it->second.state != CacheState::Ready && it->second.state != CacheState::Stale)
        return false;
    out = it->second.entries;
    return true;
}

void UltraFilerRemoteDrives::Revalidate(const std::string& path) {
    if (!IsRemoteFilerPath(path)) return;
    std::unique_lock<std::mutex> lk(mutex_);
    if (shutdown_) return;
    // Being fetched right now, by the worker or beside it: that answer is
    // the fresh one.
    if ((activeIsListing_ && activeJobPath_ == path) || sideListingPaths_.count(path)) return;
    auto it = cache_.find(path);
    if (it != cache_.end()) {
        CacheEntry& entry = it->second;
        if (entry.state == CacheState::Failed) {
            entry = CacheEntry{};   // asked again from nothing, as a miss is
        } else if (entry.state != CacheState::Loading) {
            entry.state = CacheState::Stale;
            entry.revalidating = true;
        }
    } else {
        cache_[path] = CacheEntry{};
    }
    // Waiting in the queue - behind an upload, perhaps, which is what the
    // caller cannot wait for: taken out, and fetched now instead.
    queue_.erase(std::remove_if(queue_.begin(), queue_.end(),
                                [&path](const Job& j) { return j.isListing && j.path == path; }),
                 queue_.end());
    if (activeIsTransfer_) {
        StartSideListingLocked(path);
        return;
    }
    // Anything else the worker is doing is over in a moment (a preview copy
    // gives way): this goes next.
    Job job;
    job.isListing = true;
    job.path = path;
    queue_.push_front(std::move(job));
    CancelActivePreviewLocked(PreviewYield);
    EnsureWorker();
    lk.unlock();
    cond_.notify_one();
}

void UltraFilerRemoteDrives::AddUploadsUnderWay(const std::string& path,
                                                std::vector<FilerEntry>& entries) const {
    std::string accountId, folder;
    if (!SplitRemoteFilerPath(path, accountId, folder)) return;
    auto sameFolder = [&accountId, &folder](const std::string& other) {
        std::string otherId, otherFolder;
        return SplitRemoteFilerPath(other, otherId, otherFolder) && otherId == accountId &&
               otherFolder == folder;
    };
    // Name and local file, collected under the lock; the disk is asked after.
    std::vector<std::pair<std::string, std::string>> uploads;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (!activeUploadFile_.empty() && sameFolder(activeJobPath_))
            uploads.emplace_back(activeTransfer_.name, activeUploadFile_);
        for (const Job& job : queue_) {
            if (job.isListing || job.isPreview || job.operation != RemoteOperation::Upload ||
                !sameFolder(job.path))
                continue;
            uploads.emplace_back(UploadedName(job.argument, job.remoteName), job.argument);
        }
    }
    for (const auto& [name, local] : uploads) {
        const bool listed = std::any_of(entries.begin(), entries.end(),
                                        [&name](const FilerEntry& e) { return e.name == name; });
        if (listed) continue;
        FilerEntry f;
        f.name = name;
        f.path = MakeRemoteFilerPath(accountId, (folder == "/" ? std::string() : folder) + "/" + name);
        f.isHidden = IsHiddenRemoteFilerName(name);
        std::error_code ec;
        const fs::path file = UltraCanvas::PathFromUtf8(local);
        const std::uintmax_t size = fs::file_size(file, ec);
        f.size = ec ? 0 : static_cast<uint64_t>(size);
        const fs::file_time_type t = fs::last_write_time(file, ec);
        if (!ec) {
            using namespace std::chrono;
            f.modifiedTime = system_clock::to_time_t(time_point_cast<system_clock::duration>(
                    t - fs::file_time_type::clock::now() + system_clock::now()));
        }
        entries.push_back(std::move(f));
    }
}

void UltraFilerRemoteDrives::StartSideListingLocked(const std::string& path) {
    // The threads of earlier ones that have ended are joined first, so they
    // do not pile up over a long session.
    for (auto i = sideListings_.begin(); i != sideListings_.end();) {
        if (!i->done->load()) { ++i; continue; }
        if (i->thread.joinable()) i->thread.join();
        i = sideListings_.erase(i);
    }
    sideListingPaths_.insert(path);
    SideListing side;
    side.done = std::make_shared<std::atomic<bool>>(false);
    auto done = side.done;
    side.thread = std::thread([this, path, done]() {
        SideListingMain(path);
        done->store(true);
    });
    sideListings_.push_back(std::move(side));
}

void UltraFilerRemoteDrives::SideListingMain(const std::string& path) {
    // Called off with everything else when the window closes.
    UltraNet_SetThreadAbortCheck([this]() { return stopping_.load(); });
    // Logged as the worker logs a listing: a session of its own, every step
    // of the connection in it.
    Job job;
    job.isListing = true;
    job.path = path;
    const uint64_t logId = BeginLogSession(job);
    auto onLine = [this, logId](const RemoteLogLine& line) {
        log_.Append(logId, line);
        NotifyLogChanged();
    };
    UltraNet_SetThreadFtpLog(
            [onLine](const UltraNetFtpLogLine& in) { onLine(ToRemoteLogLine(in)); });
#ifdef ULTRAFILER_HAS_ULTRACLOUD
    UltraCloud::SetThreadLog(
            [onLine](const UltraCloud::LogLine& in) { onLine(ToRemoteLogLine(in)); });
    // A service limiting requests is not waited out here, where nothing can
    // interrupt the wait: the listing goes to the queue instead, whose worker
    // waits as long as the service asks.
    UltraCloud::RateLimitPolicy rateLimit;
    rateLimit.maxRetries = 0;
    UltraCloud::SetThreadRateLimitPolicy(std::move(rateLimit));
#endif
    JobOutcome outcome;
    try {
        FetchListing(path, /*isPrefetch=*/false, outcome, /*recordFailure=*/false);
    } catch (const std::exception& e) {
        outcome.failed = true;
        outcome.message = std::string("listing failed: ") + e.what();
    } catch (...) {
        outcome.failed = true;
        outcome.message = "listing failed";
    }
    // The connection this thread opened goes with it - closed here, while
    // the thread still runs: on Windows a thread's last rites run under the
    // loader lock, no place for a QUIT.
    UltraNet_FtpCloseIdleConnections();

    bool queued = false;
    bool again = false;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        sideListingPaths_.erase(path);
        // The transfer under way changed the folder while this was read - the
        // upload landed: what it read may be from before, so the folder is
        // read again (Revalidate, below) rather than answered from it.
        again = sideListingsOverrun_.erase(path) > 0 && !shutdown_;
        // Not fetched - a server that takes one connection at a time, or none
        // at all just now: queued as before, behind the transfer, so the
        // answer still comes (or the failure is said) when that ends.
        if (outcome.failed && !shutdown_ && !again) {
            queue_.push_back(job);
            EnsureWorker();
            queued = true;
        }
    }
    // Queued again, or - fetched - with its subfolders queued to be read
    // ahead (QueuePrefetchLocked) for a worker that may be idle by now.
    cond_.notify_one();
    if (queued) outcome.message += " - asked again once the transfer under way ends";
    if (again) outcome.message += " - the folder changed meanwhile: read again";
    log_.Finish(logId, outcome.failed, outcome.message, outcome.category, outcome.diagnostics);
    NotifyLogChanged();
    if (again) {
        Revalidate(path);
        return;
    }
    if (outcome.failed || stopping_.load()) return;

    if (UltraCanvasApplicationBase* app = UltraCanvasApplicationBase::GetCurrent()) {
        auto alive = alive_;
        app->PostToUIThread([this, alive, path]() {
            if (!alive->load()) return;   // owner destroyed meanwhile
            if (onListingArrived) onListingArrived(path);
        });
    }
}

std::string UltraFilerRemoteDrives::ListingError(const std::string& path) const {
    std::lock_guard<std::mutex> lk(mutex_);
    const auto it = cache_.find(path);
    if (it == cache_.end() || it->second.state != CacheState::Failed) return {};
    return it->second.error;
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
        if (std::filesystem::is_directory(UltraCanvas::PathFromUtf8(argument), ec) && !ec) {
            error = "a folder cannot be uploaded from here, only files: " +
                    PathToUtf8(PathFromUtf8(argument).filename());
            return false;
        }
        ec.clear();
        if (!std::filesystem::is_regular_file(UltraCanvas::PathFromUtf8(argument), ec) || ec) {
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
        CancelActivePreviewLocked(PreviewYield);   // the user's request first
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
                                    std::string& error, const std::string& remoteName) {
    // A name, never a path: Keep both puts the file beside the one it would
    // replace, not somewhere else.
    if (!remoteName.empty() &&
        (remoteName.find_first_of("/\\") != std::string::npos || remoteName == "." ||
         remoteName == "..")) {
        error = "not a file name: " + remoteName;
        return false;
    }
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
    if (fs::is_directory(UltraCanvas::PathFromUtf8(localFile), ec)) {
        error = "folders cannot be uploaded - drop the files inside it";
        return false;
    }
    if (!fs::is_regular_file(UltraCanvas::PathFromUtf8(localFile), ec) || ec) {
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
        job.remoteName = remoteName;
        job.isDirectory = false;
        queue_.push_back(std::move(job));
        CancelActivePreviewLocked(PreviewYield);   // the user's request first
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
    if (!fs::is_directory(UltraCanvas::PathFromUtf8(localFolder), ec) || ec) {
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
        CancelActivePreviewLocked(PreviewYield);   // the user's request first
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
    for (fs::directory_iterator it(UltraCanvas::PathFromUtf8(directory), ec), end; it != end && !ec;
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
    const fs::path target = PathFromUtf8(directory) /
            RemotePreviewCacheKey(entry.path, entry.size, entry.modifiedTime) / name;
    const std::string targetPath = PathToUtf8(target);

    // Fetched before - in this run or an earlier one - and unchanged since,
    // since a change would have given it another folder.
    std::error_code ec;
    if (fs::is_regular_file(UltraCanvas::PathFromUtf8(target), ec) && !ec) {
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
        // started is for a file nobody is looking at any more, and so is one
        // that has - it is called off rather than left to hold up the drive
        // for a file that will not be shown.
        for (auto q = queue_.begin(); q != queue_.end();) {
            if (q->isPreview && q->path != entry.path) {
                previews_.erase(q->path);
                q = queue_.erase(q);
            } else {
                ++q;
            }
        }
        if (activePreviewCancel_ && activePreviewPath_ != entry.path)
            CancelActivePreviewLocked(PreviewSuperseded);

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

void UltraFilerRemoteDrives::RunOperation(const Job& job, JobOutcome& outcome) {
#ifndef ULTRAFILER_HAS_ULTRACLOUD
    (void)job;
    std::lock_guard<std::mutex> lk(mutex_);
    lastOperationError_ = "this build of UltraFiler carries no cloud support";
    outcome.failed = true;
    outcome.message = lastOperationError_;
#else
    std::string accountId, remotePath;
    if (!SplitRemoteFilerPath(job.path, accountId, remotePath)) return;

    // A preview copy's folder is made here, off the UI thread, and only once
    // the download is really going to happen.
    if (job.isPreview) {
        std::error_code ec;
        fs::create_directories(PathFromUtf8(job.previewTarget).parent_path(), ec);
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
            const std::string name = UploadedName(job.argument, job.remoteName);
            r = impl_->service->Upload(accountId, job.argument, parent + "/" + name);
            break;
        }
        case RemoteOperation::Download: {
            // The full local path was settled when the job was queued.
            r = impl_->service->Download(accountId, remotePath, job.argument);
            // A download that failed part way - the connection lost, the
            // window closed (Stop) - leaves no half a file behind: an FTP
            // download writes straight into it. The name was a free one when
            // the job was queued, so what is there is this job's own.
            if (!r.IsOk()) {
                std::error_code ec;
                fs::remove(PathFromUtf8(job.argument), ec);
            }
            break;
        }
    }

    std::lock_guard<std::mutex> lk(mutex_);
    // The provider's own words where there are any: "550 Permission denied"
    // tells the user what to change, "the operation failed" tells them nothing.
    lastOperationError_ = r.IsOk() ? std::string()
                        : r.message.empty() ? "the server refused this"
                                            : r.message;
    outcome.failed = !r.IsOk();
    outcome.message = lastOperationError_;
    if (outcome.failed) {
        outcome.category = DescribeResultCode(r.code);
        outcome.diagnostics = r.diagnostics;
    }
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

    // Known: anything cached - answered this session, on its way, or kept
    // from the last run - and anything already queued. A listing kept from
    // the last run is shown the moment its folder is opened and asked for
    // again then (List); fetching it ahead as well re-read every subfolder
    // of every folder opened, on every run, whether or not anybody opened it
    // again.
    std::unordered_set<std::string> known = prefetchQueued_;
    for (const auto& [path, entry] : cache_) known.insert(path);
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
        if (cache_.count(path)) continue;
        cache_[path] = CacheEntry{};   // Loading
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

    std::ifstream in(PathFromUtf8(DiskCachePath()), std::ios::binary);
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
    fs::create_directories(PathFromUtf8(target).parent_path(), ec);
    {
        std::ofstream out(UltraCanvas::PathFromUtf8(temp), std::ios::binary | std::ios::trunc);
        if (!out) return;
        out << SerializeRemoteListings(listings);
        if (!out) { out.close(); fs::remove(UltraCanvas::PathFromUtf8(temp), ec); return; }
    }
    fs::rename(UltraCanvas::PathFromUtf8(temp), UltraCanvas::PathFromUtf8(target), ec);
    if (ec) fs::remove(UltraCanvas::PathFromUtf8(temp), ec);
}

std::shared_ptr<UltraCloud::CloudService> UltraFilerRemoteDrives::Service() {
#ifndef ULTRAFILER_HAS_ULTRACLOUD
    return nullptr;
#else
    return impl_->service;
#endif
}

bool UltraFilerRemoteDrives::DescribeTransferLocked(const Job& job, RemoteTransfer& out) const {
    if (job.isListing || job.isPreview) return false;
    if (job.operation != RemoteOperation::Upload && job.operation != RemoteOperation::Download)
        return false;
    out = RemoteTransfer{};
    out.upload = job.operation == RemoteOperation::Upload;
    // An upload's job names the folder it goes into and the local file; a
    // download's the remote file and where it lands.
    out.name = out.upload ? UploadedName(job.argument, job.remoteName)
                          : RemoteFilerName(job.path);
    const std::string accountId = RemoteFilerAccountId(job.path);
    out.drive = accountId;
    for (const RemoteDrive& d : drives_) {
        if (d.accountId != accountId) continue;
        out.drive = d.displayName;
        out.cleanedUpIfStopped = out.upload && d.partialUploadsRemain;
        break;
    }
    return true;
}

std::vector<RemoteTransfer> UltraFilerRemoteDrives::PendingTransfers() const {
    std::lock_guard<std::mutex> lk(mutex_);
    std::vector<RemoteTransfer> out;
    if (activeIsTransfer_) out.push_back(activeTransfer_);
    for (const Job& job : queue_) {
        RemoteTransfer t;
        if (DescribeTransferLocked(job, t)) out.push_back(t);
    }
    return out;
}

void UltraFilerRemoteDrives::CancelActivePreviewLocked(PreviewCancel why) {
    if (!activePreviewCancel_) return;
    // The strongest reason wins: a copy that yielded to a listing and is then
    // no longer wanted at all is dropped, not fetched again.
    int current = activePreviewCancel_->load();
    while (current < why &&
           !activePreviewCancel_->compare_exchange_weak(current, why)) {
    }
}

void UltraFilerRemoteDrives::Stop() {
    // First, without the lock: the worker's abort check reads it on every
    // progress callback of the transfer it is in the middle of, which ends
    // within a second as Cancelled rather than running to its last byte.
    stopping_.store(true);
    {
        std::lock_guard<std::mutex> lk(mutex_);
        shutdown_ = true;
        // A preview copy is nothing to wait for: the window is going.
        CancelActivePreviewLocked(PreviewShutdown);
        queue_.clear();
        prefetchQueue_.clear();
        prefetchQueued_.clear();
    }
    cond_.notify_all();
    if (worker_.joinable()) worker_.join();
    // A listing fetched beside a transfer is called off by stopping_ as the
    // transfer is. Taken out under the lock, joined without it: the thread
    // takes the lock as it ends.
    std::vector<SideListing> sides;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        sides = std::move(sideListings_);
        sideListings_.clear();
    }
    for (SideListing& side : sides)
        if (side.thread.joinable()) side.thread.join();
    // A sign-in from the add-drive dialog still running is called off - the
    // wait for the browser ends within a fraction of a second - and given a
    // moment to end, so it is over before the application it reports to goes
    // away with the window. One stuck in a request to a server that does not
    // answer is not waited out: it holds the shared service, and through it
    // the stores, so nothing it uses is freed under it, and called off it
    // stores nothing when that request ends.
    signIns_->stopping.store(true);
    {
        std::unique_lock<std::mutex> lk(signIns_->mutex);
        signIns_->ended.wait_for(lk, std::chrono::seconds(2),
                                 [this]() { return signIns_->running == 0; });
    }
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
    // The running preview copy's flag, set for the length of its job (null
    // for every other job). Read only on this thread, by the check below.
    std::shared_ptr<std::atomic<int>> jobCancel;
    // Every call this thread makes - HTTP for the cloud drives, FTP of any
    // kind - asks this as it runs, and ends as Cancelled once it says so:
    // when Stop() has been called (quitting does not wait for a large upload
    // to finish), and when the preview copy being fetched is no longer to
    // go on (CancelActivePreviewLocked). The thread's one such check.
    UltraNet_SetThreadAbortCheck([this, &jobCancel]() {
        return stopping_.load() || (jobCancel && jobCancel->load() != PreviewKeep);
    });
    // Whether a job has run since the FTP connections this thread keeps open
    // were last closed (UltraNet_FtpCloseIdleConnections).
    bool connectionsOpen = false;
    for (;;) {
        Job job;
        std::size_t waiting = 0;
        // Set for a preview copy only: what calls its download off.
        std::shared_ptr<std::atomic<int>> previewCancel;
        {
            std::unique_lock<std::mutex> lk(mutex_);
            auto ready = [this]() {
                return shutdown_ || !queue_.empty() || !prefetchQueue_.empty();
            };
            if (!connectionsOpen) {
                cond_.wait(lk, ready);
            } else if (!cond_.wait_for(lk, kRemoteConnectionIdleClose, ready)) {
                // Gone quiet: the connection the last job left open is
                // closed while the server has only just answered on it. The
                // close says QUIT and waits for the reply, which a connection
                // whose network has since gone away does not send - so the
                // later this happens, the longer it can hold the next job,
                // or the window closing, which joins this thread.
                lk.unlock();
                UltraNet_FtpCloseIdleConnections();
                connectionsOpen = false;
                continue;
            }
            if (shutdown_) {
                // Closed here rather than left to the end of the thread,
                // which would close it too - but on Windows a thread's last
                // rites run under the loader lock, no place for a QUIT.
                lk.unlock();
                if (connectionsOpen) UltraNet_FtpCloseIdleConnections();
                // The check reads jobCancel, which goes with this function.
                UltraNet_SetThreadAbortCheck({});
                return;
            }
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
            activeIsTransfer_ = DescribeTransferLocked(job, activeTransfer_);
            activeTransfer_.running = activeIsTransfer_;
            activeIsListing_ = job.isListing;
            activeUploadFile_ = activeIsTransfer_ && activeTransfer_.upload ? job.argument
                                                                             : std::string();
            if (job.isPreview) {
                activePreviewPath_ = job.path;
                activePreviewCancel_ = std::make_shared<std::atomic<int>>(PreviewKeep);
                previewCancel = activePreviewCancel_;
            }
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
                        ? UploadedName(job.argument, job.remoteName)
                        : job.operation == RemoteOperation::MakeDirectory
                                ? job.argument
                                : RemoteFilerName(job.path);
        // A prefetch is not something the user asked for, so the status line
        // does not mention it: it would read as the drive being busy with
        // folders nobody opened.
        if (!job.isPrefetch) ReportActivity(activity, /*force=*/true);

        // The job as a session of the connection log. On an FTP or SFTP
        // drive UltraNet logs every step of it on this thread, and on a cloud
        // drive UltraCloud logs each request, the service's answer, a renewed
        // sign-in and the pages of a listing - the sinks below catch them,
        // because the call goes through UltraCloud, which builds UltraNet's
        // options itself and has no log to hand on.
        const uint64_t logId = BeginLogSession(job);
        bool http = false;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            const std::string accountId = RemoteFilerAccountId(job.path);
            for (const RemoteDrive& d : drives_) {
                if (d.accountId != accountId) continue;
                http = ProviderSpeaksHttp(d.providerId);
                break;
            }
        }
        // What the status line says about this job, shared by the two things
        // that move it on: each step of the connection, and each chunk of a
        // transfer. Both fire on this thread, inside the provider call, so
        // the copy needs no lock.
        auto current = std::make_shared<RemoteActivity>(activity);
        const bool reportsActivity = !job.isPrefetch;
        auto onLine = [this, logId, current, reportsActivity, http](const RemoteLogLine& line) {
            log_.Append(logId, line);
            NotifyLogChanged();
            if (!reportsActivity) return;
            // Every step, not one in eighty milliseconds: when a connection
            // hangs, the step on screen has to be the one it hangs at.
            current->step = StepText(line, http);
            {
                std::lock_guard<std::mutex> lk(mutex_);
                activeJobStep_ = current->step;
            }
            ReportActivity(*current, /*force=*/true);
        };
        UltraNetFtpLogCallback previousLog = UltraNet_SetThreadFtpLog(
                [onLine](const UltraNetFtpLogLine& in) { onLine(ToRemoteLogLine(in)); });
        // Asked by the thread's abort check as the job's calls run. Only a
        // preview copy has one: everything else is something the user asked
        // for.
        jobCancel = previewCancel;
#ifdef ULTRAFILER_HAS_ULTRACLOUD
        UltraCloud::LogCallback previousCloudLog = UltraCloud::SetThreadLog(
                [onLine](const UltraCloud::LogLine& in) { onLine(ToRemoteLogLine(in)); });
        // A cloud service that limits requests is waited out - as long as it
        // asks, up to a minute - and the request sent again; the wait ends at
        // once when the window closes, which joins this thread. A folder read
        // ahead is not waited for: nobody is looking at it, and reading ahead
        // is what ran into the limit.
        UltraCloud::RateLimitPolicy rateLimit;
        rateLimit.maxRetries = job.isPrefetch ? 0 : 3;
        rateLimit.maxWaitSeconds = kRemoteRateLimitMaxWaitSeconds;
        rateLimit.wait = [this](long long seconds) {
            std::unique_lock<std::mutex> lk(mutex_);
            return !cond_.wait_for(lk, std::chrono::seconds(seconds),
                                   [this]() { return shutdown_; });
        };
        UltraCloud::RateLimitPolicy previousRateLimit =
                UltraCloud::SetThreadRateLimitPolicy(std::move(rateLimit));
#endif

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
            auto count = [this, current](int64_t moved, int64_t total) {
                current->bytesDone = moved > 0 ? static_cast<uint64_t>(moved) : 0;
                // A server that sent no length reports -1; that is the busy
                // case, not a total of zero bytes to move.
                current->bytesTotal = total > 0 ? static_cast<uint64_t>(total) : 0;
                ReportActivity(*current, /*force=*/false);
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
        JobOutcome outcome;
        try {
            if (job.isListing) notifyListing = FetchListing(job.path, job.isPrefetch, outcome);
            else               RunOperation(job, outcome);
        } catch (const std::exception& e) {
            outcome.failed = true;
            if (job.isListing) {
                outcome.message = std::string("listing failed: ") + e.what();
                std::lock_guard<std::mutex> lk(mutex_);
                notifyListing = RecordListingFailureLocked(job.path, job.isPrefetch,
                                                           outcome.message);
            } else {
                operationError = std::string("the operation failed: ") + e.what();
                outcome.message = operationError;
            }
        } catch (...) {
            outcome.failed = true;
            if (job.isListing) {
                outcome.message = "listing failed";
                std::lock_guard<std::mutex> lk(mutex_);
                notifyListing = RecordListingFailureLocked(job.path, job.isPrefetch,
                                                           outcome.message);
            } else {
                operationError = "the operation failed";
                outcome.message = operationError;
            }
        }
        UltraNet_SetThreadFtpLog(std::move(previousLog));
        jobCancel.reset();
        // Why the preview copy was called off, if it was and the download did
        // not finish anyway. Not a failure: the log shows it as called off and
        // the window hears nothing, so it raises no alarm.
        int calledOff = PreviewKeep;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            if (previewCancel && outcome.failed) calledOff = previewCancel->load();
            activePreviewPath_.clear();
            activePreviewCancel_.reset();
        }
        if (calledOff != PreviewKeep) {
            outcome.failed = false;
            outcome.message = calledOff == PreviewYield
                    ? "called off for a request of yours; fetched again after it"
                    : "called off";
            outcome.category.clear();
            outcome.diagnostics.clear();
        }
#ifdef ULTRAFILER_HAS_ULTRACLOUD
        UltraCloud::SetThreadLog(std::move(previousCloudLog));
        UltraCloud::SetThreadRateLimitPolicy(std::move(previousRateLimit));
#endif
        connectionsOpen = true;
        log_.Finish(logId, outcome.failed, outcome.message, outcome.category,
                    outcome.diagnostics);
        NotifyLogChanged();

        {
            std::lock_guard<std::mutex> lk(mutex_);
            activeJobPath_.clear();
            activeJobStep_.clear();
            activeIsTransfer_ = false;
            activeIsListing_ = false;
            activeUploadFile_.clear();
        }

        // A preview copy called off: half a file is thrown away, and the
        // window is told nothing - it either still waits for the copy, which
        // is fetched again behind the request it gave way to, or no longer
        // wants it.
        if (job.isPreview && calledOff != PreviewKeep) {
            std::error_code ec;
            fs::remove(UltraCanvas::PathFromUtf8(job.argument), ec);   // half a file
            if (watchesBytes) UltraNet_SetTransferCallbacks(previousCallbacks);
            std::size_t left = 0;
            {
                std::lock_guard<std::mutex> lk(mutex_);
                lastOperationError_.clear();
                if (calledOff == PreviewYield && !shutdown_) {
                    // Behind what the user asked for, still pending: the pane
                    // is still waiting for it, and onPreviewCopyReady fires
                    // once it is in. Fetched from the start again - the
                    // provider has no way to resume a download.
                    queue_.push_back(job);
                } else {
                    // Asked for again if the selection comes back to it.
                    previews_.erase(job.path);
                }
                left = queue_.size();
            }
            if (left == 0) ReportActivity(RemoteActivity{}, /*force=*/true);
            continue;
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
                fs::rename(UltraCanvas::PathFromUtf8(job.argument), PathFromUtf8(job.previewTarget), ec);
                if (ec) previewError = "cannot store the preview: " + ec.message();
                // Stamped now, whatever time the transfer gave the file, so
                // the sweep counts its age from this look.
                else DiskCache::Touch(job.previewTarget, std::chrono::seconds(0));
            }
            if (!previewError.empty()) fs::remove(UltraCanvas::PathFromUtf8(job.argument), ec);
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
                changedFolder = PathToUtf8(PathFromUtf8(job.argument).parent_path());
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
            // A listing of it being fetched beside this job may have been
            // read before the change: it is not to stand as the fresh one.
            if (sideListingPaths_.count(changedFolder)) sideListingsOverrun_.insert(changedFolder);
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

uint64_t UltraFilerRemoteDrives::BeginLogSession(const Job& job) {
    RemoteLogSession header;
    std::string accountId, remotePath;
    SplitRemoteFilerPath(job.path, accountId, remotePath);
    if (remotePath.empty()) remotePath = "/";
    {
        std::lock_guard<std::mutex> lk(mutex_);
        for (const RemoteDrive& d : drives_) {
            if (d.accountId != accountId) continue;
            header.drive = d.displayName;
            header.server = d.serverUrl;
            header.http = ProviderSpeaksHttp(d.providerId);
            break;
        }
    }
    if (header.drive.empty()) header.drive = accountId;
    // A folder nobody opened yet, or a copy for the preview pane: logged, but
    // not an error anybody saw if it fails.
    header.background = job.isPrefetch || job.isPreview;
    // The folder a new entry goes in, joined to its name.
    auto inFolder = [&remotePath](const std::string& name) {
        return (remotePath == "/" ? std::string() : remotePath) + "/" + name;
    };
    if (job.isListing) {
        header.operation = job.isPrefetch ? "Read folder ahead" : "Open folder";
        header.target = remotePath;
    } else {
        switch (job.operation) {
            case RemoteOperation::Delete:
                header.operation = job.isDirectory ? "Delete folder" : "Delete";
                header.target = remotePath;
                break;
            case RemoteOperation::Rename:
                header.operation = "Rename";
                header.target = remotePath + " to \"" + job.argument + "\"";
                break;
            case RemoteOperation::MakeDirectory:
                header.operation = "Create folder";
                header.target = inFolder(job.argument);
                break;
            case RemoteOperation::Upload:
                header.operation = "Upload";
                header.target = inFolder(UploadedName(job.argument, job.remoteName));
                break;
            case RemoteOperation::Download:
                header.operation = job.isPreview ? "Fetch for preview" : "Download";
                header.target = remotePath;
                break;
        }
    }
    const uint64_t id = log_.Begin(std::move(header));
    NotifyLogChanged();
    return id;
}

void UltraFilerRemoteDrives::NotifyLogChanged() {
    // The handler is read on the UI thread only, inside the posted task: the
    // window clears it from that thread while this worker may still be
    // logging, and a std::function read here as it is assigned there is a
    // data race. One notice in flight at a time: a burst of lines is one
    // refresh.
    if (logChangePosted_.exchange(true)) return;
    UltraCanvasApplicationBase* app = UltraCanvasApplicationBase::GetCurrent();
    if (!app) {
        logChangePosted_.store(false);
        return;
    }
    auto alive = alive_;
    app->PostToUIThread([this, alive]() {
        if (!alive->load()) return;   // owner destroyed meanwhile
        // Cleared before the handler reads the log, so a line that arrives
        // while it does posts a notice of its own.
        logChangePosted_.store(false);
        if (onConnectionLogChanged) onConnectionLogChanged();
    });
}

// ===== The add-drive dialog's sign-ins =====

struct RemoteSignInLog::State {
    // Runs `work` on the UI thread, with the drives - if they still exist by
    // then.
    std::function<void(std::function<void(UltraFilerRemoteDrives&)>)> onDrives;
    // The session the running sign-in writes to: the id is set and read on
    // the UI thread only, by the tasks posted to it in order.
    std::shared_ptr<uint64_t> sessionId;
    // Shared with the drives: whether they are stopping, and how many
    // sign-ins are still running.
    std::shared_ptr<RemoteSignInTracker> tracker;
    UltraNetFtpLogCallback previousFtpLog;
#ifdef ULTRAFILER_HAS_ULTRACLOUD
    UltraCloud::LogCallback previousCloudLog;
#endif
};

std::shared_ptr<RemoteSignInLog> UltraFilerRemoteDrives::MakeSignInLog() {
    auto log = std::make_shared<RemoteSignInLog>();
    log->state_ = std::make_shared<RemoteSignInLog::State>();
    log->state_->tracker = signIns_;
    auto alive = alive_;
    log->state_->onDrives = [this, alive](std::function<void(UltraFilerRemoteDrives&)> work) {
        UltraCanvasApplicationBase* app = UltraCanvasApplicationBase::GetCurrent();
        if (!app) return;
        app->PostToUIThread([this, alive, work]() {
            if (!alive->load()) return;   // the drives are gone
            work(*this);
        });
    };
    return log;
}

void RemoteSignInLog::Begin(const std::string& providerId, const std::string& driveName,
                            const std::string& server) {
    if (!state_) return;
    RemoteLogSession header;
    header.operation = "Add drive";
    header.drive = driveName;
#ifdef ULTRAFILER_HAS_ULTRACLOUD
    if (header.drive.empty())
        if (auto provider = UltraCloud::GetProvider(providerId)) header.drive = provider->DisplayName();
#endif
    if (header.drive.empty()) header.drive = providerId;
    header.server = server;
    header.http = ProviderSpeaksHttp(providerId);

    {
        std::lock_guard<std::mutex> lk(state_->tracker->mutex);
        ++state_->tracker->running;
    }
    auto id = std::make_shared<uint64_t>(0);
    state_->sessionId = id;
    state_->onDrives([id, header](UltraFilerRemoteDrives& drives) {
        *id = drives.log_.Begin(header);
        drives.NotifyLogChanged();
    });
    // Each line is stamped here, when it happened, not when the UI thread
    // gets to it.
    auto onDrives = state_->onDrives;
    auto onLine = [onDrives, id](RemoteLogLine line) {
        line.timeMs = RemoteLogNowMs();
        onDrives([id, line](UltraFilerRemoteDrives& drives) {
            if (*id == 0) return;
            drives.log_.Append(*id, line);
            drives.NotifyLogChanged();
        });
    };
    state_->previousFtpLog = UltraNet_SetThreadFtpLog(
            [onLine](const UltraNetFtpLogLine& in) { onLine(ToRemoteLogLine(in)); });
#ifdef ULTRAFILER_HAS_ULTRACLOUD
    state_->previousCloudLog = UltraCloud::SetThreadLog(
            [onLine](const UltraCloud::LogLine& in) { onLine(ToRemoteLogLine(in)); });
#endif
}

bool RemoteSignInLog::Cancelled() const {
    return state_ && state_->tracker && state_->tracker->stopping.load();
}

void RemoteSignInLog::End(const UltraCloud::Result& result) {
    if (!state_ || !state_->sessionId) return;
    UltraNet_SetThreadFtpLog(std::move(state_->previousFtpLog));
    bool failed = false;
    std::string message, category, diagnostics;
#ifdef ULTRAFILER_HAS_ULTRACLOUD
    UltraCloud::SetThreadLog(std::move(state_->previousCloudLog));
    // Called off - the dialog's Cancel, or the window closing - is not a
    // failure for the log button to count; its last line says so.
    failed = !result && result.code != UltraCloud::ResultCode::Cancelled;
    if (failed) {
        message = result.message;
        category = DescribeResultCode(result.code);
        diagnostics = result.diagnostics;
    }
#else
    (void)result;
#endif
    std::shared_ptr<uint64_t> id = std::move(state_->sessionId);
    {
        std::lock_guard<std::mutex> lk(state_->tracker->mutex);
        --state_->tracker->running;
    }
    state_->tracker->ended.notify_all();
    state_->onDrives([id, failed, message, category, diagnostics](UltraFilerRemoteDrives& drives) {
        if (*id == 0) return;
        drives.log_.Finish(*id, failed, message, category, diagnostics);
        drives.NotifyLogChanged();
    });
}

bool UltraFilerRemoteDrives::RecordListingFailureLocked(const std::string& path,
                                                        bool isPrefetch,
                                                        const std::string& error) {
    auto it = cache_.find(path);
    if (isPrefetch && it != cache_.end()) {
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

bool UltraFilerRemoteDrives::FetchListing(const std::string& path, bool isPrefetch,
                                          JobOutcome& outcome, bool recordFailure) {
#ifndef ULTRAFILER_HAS_ULTRACLOUD
    (void)isPrefetch;
    (void)recordFailure;
    outcome.failed = true;
    outcome.message = "this build of UltraFiler carries no cloud support";
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
        outcome.failed = true;
        outcome.message = r.message.empty() ? "cannot list this folder"
                                            : "cannot list this folder: " + r.message;
        outcome.category = DescribeResultCode(r.code);
        outcome.diagnostics = r.diagnostics;
        if (!recordFailure) return false;
        std::lock_guard<std::mutex> lk(mutex_);
        return RecordListingFailureLocked(path, isPrefetch, outcome.message);
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
