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
//
// On an FTP drive the cache also works ahead and outlives the session. When a
// folder the user opened arrives, its subfolders are queued as background
// listings behind everything the user asks for, so opening one of them next
// is a cache hit rather than a request. And the listings are written to disk
// when the window closes and read back at start-up: a folder opened in an
// earlier session is shown at once from what it held then, while the server
// is asked again behind it - when it is opened, not ahead of time. The rules
// and the file are in UltraFilerRemoteCache.h.
//
// The jobs run one after another on one worker thread, and UltraNet keeps the
// FTP connection a job leaves open for the next (UltraNet_FtpCloseIdleConnections):
// a folder and its subfolders fetched ahead are one login, not one each. The
// worker lets the connection go once the drive has been quiet for
// kRemoteConnectionIdleClose.
//
// A file shown in the preview pane is fetched too, on any drive: the media
// viewer reads local files, so RequestPreviewCopy downloads a picture, a
// vector drawing or a 3D model into a disk cache first and the copy is what
// is shown. The copy is kept, keyed by the file's path, size and date, so a
// second look costs nothing and a changed file is fetched again.
//
// Every job is also a session in the connection log (ConnectionLog(),
// UltraFilerConnectionLog.h): on an FTP or SFTP drive each step UltraNet
// takes - resolving, connecting, every command and reply - and on a cloud
// drive each request UltraCloud sends with the service's answer, a renewed
// sign-in and the pages of a long listing (UltraCloudLog.h). Each is recorded
// there and reported to the status line as it happens
// (RemoteActivity::step), and a failure keeps its codes and diagnostics for
// the log window.
// Version: 1.8.0
// Last Modified: 2026-10-09
// Author: UltraCanvas Framework
#pragma once

#include "UltraFilerRemotePath.h"
#include "UltraFilerConnectionLog.h"

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
    // Whether opening a folder fetches its subfolders ahead and the listings
    // are kept between runs (RemoteProviderPrefetches: FTP only).
    bool prefetches = false;
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
    // The latest step of the connection, as the connection log has it:
    // "Connecting to 203.0.113.7:21...", "Command: PASV", "Response: 227
    // Entering Passive Mode (...)". Empty until the first one, and for a
    // drive whose provider logs no steps (only FTP and SFTP do).
    std::string step;

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
    // False with a reason when the accounts could not be read; the first call
    // creates UltraFiler's configuration folder if it is not there yet, and a
    // call that failed is tried afresh by the next one rather than
    // remembered.
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

    // Why the listing of `path` failed, as the folder display shows it; empty
    // when it did not fail (or was never asked for). For the status line,
    // which otherwise goes back to "0 items" once the drive falls idle.
    std::string ListingError(const std::string& path) const;

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

    // ---- Preview copies ----------------------------------------------------
    enum class PreviewCopy {
        Ready,      // `localPath` is the copy; show it
        Pending,    // being fetched; onPreviewCopyReady fires when it is in
        Failed,     // `error` says why; asked again only after a Refresh
        TooLarge    // over kRemotePreviewMaxBytes: not fetched for a preview
    };
    // A local copy of the remote file `entry` for the preview pane. Answers
    // at once from the disk cache when the copy is there; otherwise queues
    // the download AHEAD of everything waiting (the user is looking at the
    // selection now) and replaces an earlier preview download that has not
    // started - the selection has moved on from it. Never blocks.
    PreviewCopy RequestPreviewCopy(const FilerEntry& entry, std::string& localPath,
                                   std::string& error);
    // Fires on the UI THREAD when a copy asked for above is in or has
    // failed, naming the remote file. The window asks again, and this time
    // gets Ready (or Failed).
    std::function<void(const std::string& remoteFile)> onPreviewCopyReady;
    // Where the copies live: "remote-previews" under UltraCanvas's per-user
    // cache root (DiskCache::Directory). Empty when there is nowhere
    // writable, which switches previews of remote files off.
    static std::string PreviewCacheDirectory();

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

    // ---- The connection log ------------------------------------------------
    // Every job the worker has run in this session, with its steps and its
    // outcome. Thread-safe; read it from the UI thread at will.
    RemoteConnectionLog& ConnectionLog() { return log_; }
    const RemoteConnectionLog& ConnectionLog() const { return log_; }

    // Fires on the UI THREAD when the connection log has changed: a session
    // started, a line came in, a session ended. Coalesced - many lines in a
    // burst are one call - so a handler can afford to read the whole log.
    std::function<void()> onConnectionLogChanged;

    // How many subfolder listings are waiting to be fetched ahead. Tests and
    // diagnostics; the UI does not show it - prefetching is meant to be
    // invisible.
    std::size_t PrefetchQueueSize() const;

    // Forgets what is cached, so the next List fetches again. Invalidate() is
    // what a manual Refresh on a remote folder means.
    void Invalidate(const std::string& path);
    void InvalidateAll();

    // The service the shared add-account dialog stores into. Null until
    // Reload() has opened the account store, and always null in a build
    // without UltraCloud.
    UltraCloud::CloudService* Service();

    // Joins the worker, then writes the prefetching drives' listings to disk
    // for the next run. Must run before the owner is destroyed - the worker
    // posts into it. Idempotent.
    void Stop();

    // Where the listings are kept between runs: remote-listings.cache in
    // UltraFiler's config directory.
    static std::string DiskCachePath();

private:
    // Stale: read from the disk cache at start-up, and shown as it is until
    // the server has been asked again. `revalidating` says that question has
    // been queued, so a folder scanned twice asks once.
    enum class CacheState { Loading, Ready, Failed, Stale };
    struct CacheEntry {
        CacheState state = CacheState::Loading;
        std::vector<FilerEntry> entries;
        std::string error;
        bool revalidating = false;
        // When this listing was last asked for or fetched, from useCounter_:
        // what decides which listings the disk cache keeps when it is full.
        uint64_t lastUsed = 0;
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
        // A listing fetched ahead rather than asked for: it reports no
        // activity, starts no prefetch of its own (one level only), and a
        // failure is forgotten rather than cached, so opening the folder
        // later asks again and shows the server's answer then.
        bool isPrefetch = false;
        // A Download into the preview cache: `argument` is the ".part" file
        // it is written to and `previewTarget` the name it is renamed to
        // once complete, so the viewer can never be handed half a picture.
        // It changes no folder anybody is looking at, so it ends in
        // onPreviewCopyReady rather than onOperationFinished.
        bool isPreview = false;
        std::string previewTarget;
    };

    // How one job ended, for the connection log: the message the window is
    // given, the error class in words, and the transport's diagnostics.
    struct JobOutcome {
        bool failed = false;
        std::string message;
        std::string category;
        std::string diagnostics;
    };

    void EnsureWorker();
    void WorkerMain();
    // Starts the connection-log session of `job`, named the way the user
    // knows it (the drive, the operation, the folder or file).
    uint64_t BeginLogSession(const Job& job);
    // Tells the UI thread the log changed, unless a notice is already on its
    // way.
    void NotifyLogChanged();
    // Posts one activity report to the UI thread. `force` sends it even when
    // the thinning interval has not elapsed - used for the first and last
    // report of a job, which are the two nobody may miss.
    void ReportActivity(const RemoteActivity& activity, bool force);
    // Turns the kind of a job into the kind of activity it is.
    static RemoteActivity::Kind ActivityKindFor(const Job& job);
    // Both run on the worker thread and make the actual UltraCloud call.
    // FetchListing answers whether the window should hear about the path
    // (onListingArrived): false for a prefetch that failed with nobody
    // waiting on it.
    bool FetchListing(const std::string& path, bool isPrefetch, JobOutcome& outcome);
    // Records a listing that could not be fetched, with the lock held, and
    // answers the same question.
    bool RecordListingFailureLocked(const std::string& path, bool isPrefetch,
                                    const std::string& error);
    void RunOperation(const Job& job, JobOutcome& outcome);
    // Called with the lock held, after a listing the user asked for arrived:
    // puts its subfolders at the front of the prefetch queue.
    void QueuePrefetchLocked(const std::string& folderPath,
                             const std::vector<FilerEntry>& entries);
    // Takes the next prefetch that is still worth doing, with the lock held.
    // False when there is none.
    bool TakePrefetchLocked(Job& job);
    // The disk cache. Load runs once, from the first Reload that knows the
    // drives; Save once, from Stop.
    void LoadDiskCacheLocked();
    void SaveDiskCache();
    // Whether `accountId` is a configured drive that prefetches. Lock held.
    bool DrivePrefetchesLocked(const std::string& accountId,
                               bool* canModify = nullptr) const;

    // The UltraCloud objects (account store, secret store, service) live
    // here rather than in this header: UltraFiler must build when the module
    // is not, and nothing that includes this file should need its headers.
    struct Impl;
    std::unique_ptr<Impl> impl_;

    mutable std::mutex mutex_;
    std::vector<RemoteDrive> drives_;
    std::unordered_map<std::string, CacheEntry> cache_;
    std::deque<Job> queue_;
    // Preview copies by remote path: in flight (no entry in the map means
    // "not asked for"), or failed with the reason. A Ready copy needs no
    // entry - the file on disk is the record.
    struct PreviewState {
        bool pending = false;
        std::string localPath;
        std::string error;
    };
    std::unordered_map<std::string, PreviewState> previews_;
    // Old copies are swept once per run, before the first is asked for.
    bool previewCacheSwept_ = false;
    // Subfolders waiting to be fetched ahead, newest folder's first, and the
    // same paths as a set so a folder is never queued twice. Only folders not
    // in the cache at all: a listing kept from the last run is checked when
    // its folder is opened. Taken by the
    // worker only when queue_ is empty: a prefetch never makes the user wait
    // for more than the one already on the wire.
    std::deque<std::string> prefetchQueue_;
    std::unordered_set<std::string> prefetchQueued_;
    uint64_t useCounter_ = 0;
    bool diskCacheLoaded_ = false;
    bool diskCacheSaved_ = false;
    // The job the worker is carrying out right now, for ListingStatus: its
    // path (empty between jobs) and when the worker took it off the queue.
    // Written by the worker under the lock.
    std::string activeJobPath_;
    std::chrono::steady_clock::time_point activeJobSince_{};
    // ... and the step its connection is at (RemoteActivity::step), for the
    // same notice.
    std::string activeJobStep_;
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

    RemoteConnectionLog log_;
    // Set while a "log changed" notice is queued for the UI thread.
    std::atomic<bool> logChangePosted_{false};

    // Neutralises a queued UI-thread callback when this object is gone, the
    // way UltraFilerWindow's probeAlive does for its own posted tasks.
    std::shared_ptr<std::atomic<bool>> alive_ =
            std::make_shared<std::atomic<bool>>(true);
};

} // namespace UltraCanvas
