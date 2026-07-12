// Apps/UltraMail/engine/UltraMailSyncService.cpp
// Version: 0.1.0 (Phase 2)
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
    std::thread([this, accountId, serverUrl, options, onDone = std::move(onDone)]() {
        SyncOutcome result = SyncNow(accountId, serverUrl, options);
        if (onDone) onDone(result);
    }).detach();
}

} // namespace UltraMail
