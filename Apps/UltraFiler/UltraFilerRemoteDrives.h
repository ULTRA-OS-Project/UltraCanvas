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
// Version: 1.3.0
// Last Modified: 2026-09-24
// Author: UltraCanvas Framework
#pragma once

#include "UltraFilerRemotePath.h"

#include "UltraCanvasFilerWidget.h"   // FilerEntry

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
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
    bool canUpload = true;     // ... and its ProviderCapabilities::upload
};

// What the toolbar's "+ Drive" button offers. The two differ only in which
// providers the add-account dialog is allowed to show: an FTP server is
// configured with a host and a password, a cloud account usually by signing
// in through the browser, and mixing the two in one list makes neither clear.
enum class RemoteDriveKind {
    FtpOrSftp,      // the "ftp" provider: FTP, FTPS, FTPES, SFTP
    CloudStorage    // everything else UltraCloud knows
};

// A change to what is on a drive. Each maps onto one UltraCloud provider verb.
enum class RemoteOperation {
    Delete,          // a file or a folder: the provider picks DELE over RMD
    Rename,          // in place; the argument is a bare name
    MakeDirectory,   // the argument is the new folder's name
    Upload,          // `path` is the folder uploaded INTO, the argument the
                     // local file's full path; queued by Upload(), not Submit
    Download         // the other direction: `path` is the remote FILE, the
                     // argument the full local path to save it as; queued by
                     // Download(), not Submit
};

// What the drives are doing right now, for the status line.
//
// A drive is the one place in this file manager where the answer to "why is
// nothing happening?" is "a server is thinking about it", and the only honest
// answer is to say so. Every job the worker runs reports itself here as it
// starts, and Idle when the queue drains.
struct RemoteActivity {
    enum class Kind {
        Idle,
        Listing,          // opening a folder: asking the server for it
        Deleting,
        Renaming,
        MakingDirectory,
        Uploading,
        Downloading       // a file coming off the drive onto this disk
    };

    Kind kind = Kind::Idle;
    // What is being worked on, as a name rather than a path: the folder being
    // opened, the file going up. Empty for Idle.
    std::string what;
    // Jobs still waiting behind this one, so "1 of 4" can be said without the
    // window keeping its own count of what it queued.
    std::size_t queued = 0;
    // Transfers only, and only while the server said how big the file is:
    // bytesTotal of 0 means "no total known", which is a busy bar rather than
    // a percentage. An FTP server does not always say.
    uint64_t bytesDone = 0;
    uint64_t bytesTotal = 0;

    bool IsBusy() const { return kind != Kind::Idle; }
    // The two kinds that move bytes, and so the two that have a length to
    // draw a bar of. Everything else is one round trip with nothing to count.
    bool IsTransfer() const {
        return kind == Kind::Uploading || kind == Kind::Downloading;
    }
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

    // What the filer widget's remoteListingStatus hook calls: one line about
    // a listing that List() answered "nothing yet" for. Empty once the data
    // is in (or the fetch failed, or the path was never asked for); while
    // the fetch is queued it counts the requests ahead of it, and while the
    // worker is on it it names the server and the folder, with how long the
    // server has been keeping it waiting. Never blocks: a lock and a lookup.
    std::string ListingStatus(const std::string& path) const;

    // Fires on the UI THREAD when a queued listing has arrived (or failed),
    // naming the path that changed. The window refreshes the display from it.
    std::function<void(const std::string& path)> onListingArrived;

    // ---- Changing what is on a drive --------------------------------------
    // Queues one change and answers at once: the work happens on the worker,
    // so a slow server never holds the UI thread, and onOperationFinished
    // fires when it is done. `path` is the entry acted on (for MakeDirectory
    // and Upload, the folder to put the new thing in); `argument` is the new
    // name for Rename and MakeDirectory, the local file for Upload, and is
    // ignored by Delete; `isDirectory` is what the caller already knows from
    // the entry, which is what lets the FTP provider pick DELE over RMD
    // without a probe.
    //
    // Returns false with a message for what can be refused outright: a path
    // that is not a remote path, a drive that is gone, a provider that cannot
    // write at all (ProviderCapabilities::modify), a name that is really a
    // path, or the drive's own root.
    // Whether files can be put onto the drive `path` is on: the provider's
    // upload capability (a Nextcloud or Dropbox drive takes uploads although
    // it cannot be changed in place). False for a path that is not a remote
    // path or a drive that is gone.
    bool CanUpload(const std::string& path) const;

    // Queues the upload of one local file into the remote folder `remoteFolder`
    // (an ultracloud:// folder path) under the file's own name, and answers at
    // once; onOperationFinished fires for that folder when the server has
    // taken it or refused it. Returns false with a message for what can be
    // refused outright: a path that is not a remote path, a drive that is
    // gone or cannot take uploads, a local path that is not a file (folders
    // are not uploaded - a transfer of a tree is not one provider verb).
    bool Upload(const std::string& remoteFolder, const std::string& localFile,
                std::string& error);

    // Queues the download of the remote file `remoteFile` into the local
    // folder `localFolder`, under its own name, and answers at once;
    // onOperationFinished fires for `localFolder` when the file is there or
    // the server refused it. The name is settled here rather than on the
    // worker: a file already in that folder is never overwritten, the copy
    // lands beside it as "name (2)", and `savedAs` says which. Returns false
    // with a message for what can be refused outright: a source that is not a
    // remote path, a drive that is gone, a folder (a tree is not one
    // transfer), or a destination that is not a writable local folder.
    bool Download(const std::string& remoteFile, const std::string& localFolder,
                  std::string& savedAs, std::string& error);

    bool Submit(RemoteOperation operation, const std::string& path,
                const std::string& argument, bool isDirectory,
                std::string& error);

    // Fires on the UI THREAD whenever what the drives are doing changes: a job
    // starting, a transfer moving, the queue draining to Idle. Reported rather
    // than polled, so the window can say what is happening without a timer.
    //
    // A transfer reports often - libcurl counts bytes, not milestones - so the
    // worker thins these down to one every few dozen milliseconds before
    // posting. The last one of a job always gets through.
    std::function<void(const RemoteActivity&)> onActivityChanged;

    // Fires on the UI THREAD when a queued change has finished. `message` is
    // empty on success and carries the provider's reason on failure;
    // `folderPath` is the folder whose listing changed, already invalidated,
    // so the window can refresh it either way. For a download that folder is
    // the LOCAL one the file landed in - nothing on the drive changed - so a
    // handler that refreshes has to ask which kind of path it was given.
    std::function<void(const std::string& folderPath,
                       const std::string& message)> onOperationFinished;

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

    // One thing for the worker to do. A listing and a change queue together
    // and are carried out in order, which is what makes a delete followed by a
    // refresh behave: the refetch cannot overtake the delete it is showing.
    struct Job {
        bool isListing = true;
        std::string path;
        // Change jobs only.
        RemoteOperation operation = RemoteOperation::Delete;
        std::string argument;
        bool isDirectory = false;
    };

    void EnsureWorker();
    void WorkerMain();
    // Posts one activity report to the UI thread. `force` sends it even when
    // the thinning interval has not elapsed - used for the first and last
    // report of a job, which are the two nobody may miss.
    void ReportActivity(const RemoteActivity& activity, bool force);
    // Turns the kind of a job into the kind of activity it is.
    static RemoteActivity::Kind ActivityKindFor(const Job& job);
    // Both run on the worker thread and make the actual UltraCloud call.
    void FetchListing(const std::string& path);
    void RunOperation(const Job& job);

    // The UltraCloud objects (account store, secret store, service) live
    // here rather than in this header: UltraFiler must build when the module
    // is not, and nothing that includes this file should need its headers.
    struct Impl;
    std::unique_ptr<Impl> impl_;

    mutable std::mutex mutex_;
    std::vector<RemoteDrive> drives_;
    std::unordered_map<std::string, CacheEntry> cache_;
    std::deque<Job> queue_;
    // The job the worker is carrying out right now, for ListingStatus: its
    // path (empty between jobs) and when the worker took it off the queue.
    // Written by the worker under the lock.
    std::string activeJobPath_;
    std::chrono::steady_clock::time_point activeJobSince_{};
    std::condition_variable cond_;
    std::thread worker_;
    bool shutdown_ = false;
    // The local paths downloads have been promised but not yet written. A
    // queued download has nothing on the disk to collide with, so without
    // this every file of one name queued together would be given that same
    // free name and the last to land would be the only one kept. A path
    // leaves when its job ends - failed or not, because a failure wrote
    // nothing and the name is free again.
    std::unordered_set<std::string> promisedDownloads_;
    // When the last activity report went out, so a transfer counting bytes
    // does not post one per chunk. Touched only by the worker thread.
    std::chrono::steady_clock::time_point lastActivityPost_{};
    bool workerStarted_ = false;
    // Where RunOperation leaves a provider's refusal for the worker loop to
    // report. Written and read on the worker thread only, under the lock.
    std::string lastOperationError_;

    // Neutralises a queued UI-thread callback when this object is gone, the
    // way UltraFilerWindow's probeAlive does for its own posted tasks.
    std::shared_ptr<std::atomic<bool>> alive_ =
            std::make_shared<std::atomic<bool>>(true);
};

} // namespace UltraCanvas
