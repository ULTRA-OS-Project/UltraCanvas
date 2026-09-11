// Apps/UltraMail/engine/UltraMailSyncService.h
// Orchestrates a full account sync over the SyncEngine (folders, then the
// inbox's envelopes + bodies) and can run it on a background worker thread. The
// completion callback fires on the worker thread — the app marshals it to the
// UI with UltraCanvasApplication::PostToUIThread.
// Version: 0.2.0 - background sync with a worker-thread prepare step
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMailSyncEngine.h"

#include <functional>
#include <string>

namespace UltraMail {

class SyncService {
public:
    SyncService(LocalStore& store, IMailboxProtocolPlugin& mailbox, std::string emlDir)
        : engine_(store, mailbox, std::move(emlDir)) {}

    // Synchronous full sync for one account: LIST folders, then fetch the inbox
    // envelopes + bodies. Returns the combined outcome.
    SyncOutcome SyncNow(const std::string& accountId, const std::string& serverUrl,
                        const UltraNetMailOptions& options);

    // Run SyncNow on a detached worker thread; onDone fires on that thread.
    void SyncInBackground(const std::string& accountId, const std::string& serverUrl,
                          const UltraNetMailOptions& options,
                          std::function<void(SyncOutcome)> onDone);
    // Same, with `prepare` run on the worker first — for work that must not
    // block the UI thread, such as refreshing an OAuth2 token into
    // options.credentials. A failed prepare is the outcome; nothing is fetched.
    using PrepareFn = std::function<UltraNetResult(UltraNetMailOptions& options)>;
    void SyncInBackground(const std::string& accountId, const std::string& serverUrl,
                          const UltraNetMailOptions& options, PrepareFn prepare,
                          std::function<void(SyncOutcome)> onDone);

private:
    SyncEngine engine_;
};

} // namespace UltraMail
