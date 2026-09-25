// Apps/UltraMail/engine/UltraMailSyncService.cpp
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

    SyncOutcome inbox = engine_.SyncMessages(accountId, "INBOX", serverUrl, options,
                                             /*fetchBodies=*/true, onProgress);
    // Combine the stats regardless of the inbox outcome's ok flag.
    SyncOutcome out;
    out.ok = folders.ok && inbox.ok;
    out.message = inbox.ok ? "" : inbox.message;
    out.stats.folders  = folders.stats.folders;
    out.stats.messages = inbox.stats.messages;
    out.stats.bodies   = inbox.stats.bodies;
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
                          : SyncOutcome::Fail(prepared.message);
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
        SyncOutcome result = prepared
            ? engine_.SyncMessages(accountId, folder, serverUrl, opts,
                                   /*fetchBodies=*/true, onProgress)
            : SyncOutcome::Fail(prepared.message);
        // Once the new mail is in, reconcile read/deleted state for the messages
        // we already had — this is what surfaces changes made on another client
        // (e.g. Gmail's web UI). It is non-fatal, so it never turns a successful
        // fetch into a failure.
        if (result.ok) {
            SyncOutcome rec = engine_.ReconcileFlags(accountId, folder, serverUrl, opts);
            result.stats.reconciled = rec.stats.reconciled;
            result.stats.expunged   = rec.stats.expunged;
        }
        if (onDone) onDone(result);
    }).detach();
}

} // namespace UltraMail
