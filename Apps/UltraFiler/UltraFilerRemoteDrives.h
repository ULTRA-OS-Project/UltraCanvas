// Apps/UltraFiler/UltraFilerRemoteDrives.h
// UltraFiler's remote drives: the UltraCloud accounts - an FTP / SFTP server,
// a Nextcloud, a WebDAV share - carried as places you can browse, and the
// listing cache that lets the folder display show them without ever waiting
// on a server while it paints.
//
// The division of labour matters here. The filer widget's `remoteListing`
// hook is called on the UI thread, inside the folder scan, so this class
// never goes to the network from List(): it answers from its cache, and a
// miss only queues the fetch. When the worker has the answer it posts back to
// the UI thread and the display refreshes. A blocking List() would freeze the
// window for as long as an unreachable server takes to time out, which is the
// one failure mode a file manager must not have.
//
// The path scheme lives next door in UltraFilerRemotePath.h, dependency-free
// and tested on its own.
//
// Builds without UltraCloud: Available() answers false, the drive list is
// empty and every call fails with a message saying so, so UltraFiler still
// compiles and runs when the module is not built.
// Version: 1.0.0
// Last Modified: 2026-09-17
// Author: UltraCanvas Framework
#pragma once

#include "UltraFilerRemotePath.h"

#include "UltraCanvasFilerWidget.h"   // FilerEntry

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// Forward-declared, not included: the add-account dialog needs the service to
// store into, and that is the only thing about UltraCloud that has to escape
// this class. A pointer to an incomplete type costs no include.
namespace UltraCloud { class CloudService; }

namespace UltraCanvas {

// One remote drive, as the folder tree and the Computer page see it.
struct RemoteDrive {
    std::string accountId;     // UltraCloud's stable slug
    std::string providerId;    // "ftp", "nextcloud", "webdav", ...
    std::string displayName;   // what the row is called
    std::string serverUrl;     // shown under the name; empty for OAuth providers
    std::string rootPath;      // MakeRemoteFilerPath(accountId, "/")
    bool canModify = false;    // the provider's ProviderCapabilities::modify
};

// What the toolbar's "+ Drive" button offers. The two differ only in which
// providers the add-account dialog is allowed to show: an FTP server is
// configured with a host and a password, a cloud account usually by signing
// in through the browser, and mixing the two in one list makes neither clear.
enum class RemoteDriveKind {
    FtpOrSftp,      // the "ftp" provider: FTP, FTPS, FTPES, SFTP
    CloudStorage    // everything else UltraCloud knows
};

class UltraFilerRemoteDrives {
public:
    UltraFilerRemoteDrives();
    ~UltraFilerRemoteDrives();

    UltraFilerRemoteDrives(const UltraFilerRemoteDrives&) = delete;
    UltraFilerRemoteDrives& operator=(const UltraFilerRemoteDrives&) = delete;

    // False in a build without UltraCloud: there is nothing to carry drives
    // with, and the "+ Drive" button says so rather than doing nothing.
    static bool Available();

    // The provider ids a given kind may offer, for filtering the add-account
    // dialog. Static and free of UltraCloud, so the UI can ask before the
    // module is initialised.
    static bool ProviderBelongsToKind(const std::string& providerId,
                                      RemoteDriveKind kind);

    // Reads the configured accounts into the drive list. Safe to call again -
    // that is how the list picks up an account the add dialog just created.
    // False with a reason when the accounts could not be read.
    bool Reload(std::string& error);

    // The drives, in the order they are shown.
    std::vector<RemoteDrive> Drives() const;

    // One drive by account id. Returns false when no such drive is configured
    // (an account removed while its folder was still open).
    bool Find(const std::string& accountId, RemoteDrive& out) const;

    // ---- What the filer widget's remoteListing hook calls ------------------
    // Answers `path` from the cache. A cache miss queues the fetch and
    // answers true with an empty listing - "nothing yet" - so the UI thread
    // is never held. `onListingArrived` fires once the data is in, and the
    // Refresh it triggers comes back here to a cache hit.
    //
    // Returns false with a message only for a real failure: a path that is
    // not a remote path, an account that no longer exists, or a listing the
    // server refused.
    bool List(const std::string& path, std::vector<FilerEntry>& out,
              std::string& error);

    // Fires on the UI THREAD when a queued listing has arrived (or failed),
    // naming the path that changed. The window refreshes the display from it.
    std::function<void(const std::string& path)> onListingArrived;

    // Forgets what is cached, so the next List fetches again. Invalidate() is
    // what a manual Refresh on a remote folder means.
    void Invalidate(const std::string& path);
    void InvalidateAll();

    // The service the shared add-account dialog stores into. Null until
    // Reload() has opened the account store, and always null in a build
    // without UltraCloud.
    UltraCloud::CloudService* Service();

    // Joins the worker. Must run before the owner is destroyed - the worker
    // posts into it. Idempotent.
    void Stop();

private:
    enum class CacheState { Loading, Ready, Failed };
    struct CacheEntry {
        CacheState state = CacheState::Loading;
        std::vector<FilerEntry> entries;
        std::string error;
    };

    void EnsureWorker();
    void WorkerMain();
    // Runs on the worker thread: the actual UltraCloud call.
    void FetchListing(const std::string& path);

    // The UltraCloud objects (account store, secret store, service) live
    // here rather than in this header: UltraFiler must build when the module
    // is not, and nothing that includes this file should need its headers.
    struct Impl;
    std::unique_ptr<Impl> impl_;

    mutable std::mutex mutex_;
    std::vector<RemoteDrive> drives_;
    std::unordered_map<std::string, CacheEntry> cache_;
    std::deque<std::string> queue_;
    std::condition_variable cond_;
    std::thread worker_;
    bool shutdown_ = false;
    bool workerStarted_ = false;

    // Neutralises a queued UI-thread callback when this object is gone, the
    // way UltraFilerWindow's probeAlive does for its own posted tasks.
    std::shared_ptr<std::atomic<bool>> alive_ =
            std::make_shared<std::atomic<bool>>(true);
};

} // namespace UltraCanvas
