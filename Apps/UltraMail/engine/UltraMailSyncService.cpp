// Apps/UltraMail/engine/UltraMailSyncService.cpp
// Version: 0.2.0 - background sync with a worker-thread prepare step
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailSyncService.h"

#include <thread>
#include <utility>

namespace UltraMail {

SyncOutcome SyncService::SyncNow(const std::string& accountId, const std::string& serverUrl,
                                 const UltraNetMailOptions& options) {
    SyncOutcome folders = engine_.SyncFolders(accountId, serverUrl, options);
    if (!folders.ok) return folders;

    SyncOutcome inbox = engine_.SyncMessages(accountId, "INBOX", serverUrl, options,
                                             /*fetchBodies=*/true);
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
                                   std::function<void(SyncOutcome)> onDone) {
    SyncInBackground(accountId, serverUrl, options, nullptr, std::move(onDone));
}

void SyncService::SyncInBackground(const std::string& accountId, const std::string& serverUrl,
                                   const UltraNetMailOptions& options, PrepareFn prepare,
                                   std::function<void(SyncOutcome)> onDone) {
    // `opts` is the worker's own mutable copy (a plain capture of the const
    // reference would stay const and could not be prepared in place).
    std::thread([this, accountId, serverUrl, opts = options, prepare = std::move(prepare),
                 onDone = std::move(onDone)]() mutable {
        SyncOutcome result;
        UltraNetResult prepared = prepare ? prepare(opts) : UltraNetResult::Ok();
        result = prepared ? SyncNow(accountId, serverUrl, opts)
                          : SyncOutcome::Fail(prepared.message);
        if (onDone) onDone(result);
    }).detach();
}

} // namespace UltraMail
