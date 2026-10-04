// Apps/UltraMail/engine/UltraMailSyncService.cpp
// Version: 0.3.0 - the inbox and an opened folder go through RefreshFolder: the
//                  reconcile and the repair of missed mail on every sync
// Version: 0.2.0 - background sync with a worker-thread prepare step
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailSyncService.h"

#include <thread>
#include <utility>

namespace UltraMail {

SyncOutcome SyncService::SyncNow(const std::string& accountId, const std::string& serverUrl,
                                 const UltraNetMailOptions& options, ProgressFn onProgress) {
    SyncOutcome folders = engine_.SyncFolders(accountId, serverUrl, options);
    if (!folders.ok) return folders;

    // The whole refresh, not just "UIDs above the highest held": mail deleted
    // or read on another computer follows here too, and a message an earlier
    // sync missed is fetched now instead of never.
    SyncOutcome inbox = engine_.RefreshFolder(accountId, "INBOX", serverUrl, options,
                                              /*fetchBodies=*/true, onProgress);
    // Combine the stats regardless of the inbox outcome's ok flag. A failed
    // inbox fetch keeps its reason, code and connection details: the app
    // decides from the code whether the failure is worth an alert.
    SyncOutcome out;
    out.ok = folders.ok && inbox.ok;
    out.message = inbox.ok ? "" : inbox.message;
    out.code = inbox.code;
    out.diagnostics = inbox.diagnostics;
    out.stats = inbox.stats;
    out.stats.folders = folders.stats.folders;
    return out;
}

void SyncService::SyncInBackground(const std::string& accountId, const std::string& serverUrl,
                                   const UltraNetMailOptions& options,
                                   std::function<void(SyncOutcome)> onDone,
                                   ProgressFn onProgress) {
    SyncInBackground(accountId, serverUrl, options, nullptr, std::move(onDone),
                     std::move(onProgress));
}

void SyncService::SyncInBackground(const std::string& accountId, const std::string& serverUrl,
                                   const UltraNetMailOptions& options, PrepareFn prepare,
                                   std::function<void(SyncOutcome)> onDone,
                                   ProgressFn onProgress) {
    // `opts` is the worker's own mutable copy (a plain capture of the const
    // reference would stay const and could not be prepared in place).
    std::thread([this, accountId, serverUrl, opts = options, prepare = std::move(prepare),
                 onDone = std::move(onDone), onProgress = std::move(onProgress)]() mutable {
        SyncOutcome result;
        UltraNetResult prepared = prepare ? prepare(opts) : UltraNetResult::Ok();
        result = prepared ? SyncNow(accountId, serverUrl, opts, std::move(onProgress))
                          : SyncOutcome::Fail(prepared);
        if (onDone) onDone(result);
    }).detach();
}

void SyncService::SyncFolderInBackground(const std::string& accountId, const std::string& folder,
                                         const std::string& serverUrl,
                                         const UltraNetMailOptions& options, PrepareFn prepare,
                                         std::function<void(SyncOutcome)> onDone,
                                         ProgressFn onProgress) {
    std::thread([this, accountId, folder, serverUrl, opts = options, prepare = std::move(prepare),
                 onDone = std::move(onDone), onProgress = std::move(onProgress)]() mutable {
        UltraNetResult prepared = prepare ? prepare(opts) : UltraNetResult::Ok();
        // New mail, then the reconcile of read/deleted state for the messages
        // we already had — this is what surfaces changes made on another client
        // (e.g. Gmail's web UI) — and the mail an earlier sync missed. Only the
        // new-mail step can fail the call.
        SyncOutcome result = prepared
            ? engine_.RefreshFolder(accountId, folder, serverUrl, opts,
                                    /*fetchBodies=*/true, onProgress)
            : SyncOutcome::Fail(prepared);
        if (onDone) onDone(result);
    }).detach();
}

} // namespace UltraMail
