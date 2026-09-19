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

    // Fired (on the worker thread) with each inbox message as its header lands,
    // so the app can stream new rows into the UI instead of waiting for the whole
    // mailbox. Marshal to the UI thread before touching any widget.
    using ProgressFn = std::function<void(const MessageEnvelope&)>;

    // Synchronous full sync for one account: LIST folders, then fetch the inbox
    // envelopes + bodies. Returns the combined outcome.
    SyncOutcome SyncNow(const std::string& accountId, const std::string& serverUrl,
                        const UltraNetMailOptions& options, ProgressFn onProgress = {});

    // Run SyncNow on a detached worker thread; onDone fires on that thread.
    void SyncInBackground(const std::string& accountId, const std::string& serverUrl,
                          const UltraNetMailOptions& options,
                          std::function<void(SyncOutcome)> onDone,
                          ProgressFn onProgress = {});
    // Same, with `prepare` run on the worker first — for work that must not
    // block the UI thread, such as refreshing an OAuth2 token into
    // options.credentials. A failed prepare is the outcome; nothing is fetched.
    using PrepareFn = std::function<UltraNetResult(UltraNetMailOptions& options)>;
    void SyncInBackground(const std::string& accountId, const std::string& serverUrl,
                          const UltraNetMailOptions& options, PrepareFn prepare,
                          std::function<void(SyncOutcome)> onDone,
                          ProgressFn onProgress = {});

    // Sync one folder's messages (envelopes + bodies) on a detached worker,
    // skipping the folder LIST — the folder set is already known from the last
    // full sync. `prepare` runs first (e.g. refresh an OAuth2 token). onDone and
    // onProgress fire on the worker thread; marshal to the UI before touching a
    // widget. This backs the lazy per-folder load when a folder is first opened.
    void SyncFolderInBackground(const std::string& accountId, const std::string& folder,
                                const std::string& serverUrl,
                                const UltraNetMailOptions& options, PrepareFn prepare,
                                std::function<void(SyncOutcome)> onDone,
                                ProgressFn onProgress = {});

private:
    SyncEngine engine_;
};

} // namespace UltraMail
