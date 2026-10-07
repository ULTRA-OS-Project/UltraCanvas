// Apps/UltraMail/engine/UltraMailSyncService.cpp
// Version: 0.3.1 - each step of a background sync in the timing trace (sign-in,
//                  folder list, the folder's refresh), with what it brought
// Version: 0.3.0 - the inbox and an opened folder go through RefreshFolder: the
//                  reconcile and the repair of missed mail on every sync
// Version: 0.2.0 - background sync with a worker-thread prepare step
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailSyncService.h"
#include "UltraMailTrace.h"

#include <optional>
#include <thread>
#include <utility>

namespace UltraMail {

namespace {

// What a sync brought, for the trace: "3 new, 2 bodies, 1 flag change".
void TraceOutcome(const SyncOutcome& outcome) {
    if (!Trace::Enabled()) return;
    const SyncStats& st = outcome.stats;
    std::string text = outcome.ok ? "result: " : "FAILED: " + outcome.message + "; so far: ";
    text += std::to_string(st.messages) + " message(s) stored, " + std::to_string(st.bodies) +
            " bodies, " + std::to_string(st.reconciled) + " flag change(s), " +
            std::to_string(st.expunged) + " removed";
    if (st.serverMessages >= 0) text += "; the server holds " + std::to_string(st.serverMessages);
    if (st.cacheReset) text += "; the cache was fetched again";
    Trace::Line(text);
}

} // namespace

SyncOutcome SyncService::SyncNow(const std::string& accountId, const std::string& serverUrl,
                                 const UltraNetMailOptions& options, ProgressFn onProgress) {
    std::optional<Trace::Stage> step;
    step.emplace("Folder list from the server", 0);
    SyncOutcome folders = engine_.SyncFolders(accountId, serverUrl, options);
    if (!folders.ok) return folders;

    // The whole refresh, not just "UIDs above the highest held": mail deleted
    // or read on another computer follows here too, and a message an earlier
    // sync missed is fetched now instead of never.
    step.emplace("Inbox: new mail, flags, missing bodies", 0);
    SyncOutcome inbox = engine_.RefreshFolder(accountId, "INBOX", serverUrl, options,
                                              /*fetchBodies=*/true, onProgress);
    step.reset();
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
        {
            // One block in the trace when it is over, every step timed.
            Trace::Stage trace("Background mail check of " + accountId, 0);
            std::optional<Trace::Stage> step;
            step.emplace("Sign-in credentials (vault, OAuth token)", 0);
            UltraNetResult prepared = prepare ? prepare(opts) : UltraNetResult::Ok();
            step.reset();
            result = prepared ? SyncNow(accountId, serverUrl, opts, std::move(onProgress))
                              : SyncOutcome::Fail(prepared);
            TraceOutcome(result);
        }
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
        SyncOutcome result;
        {
            // One block in the trace when it is over, every step timed.
            Trace::Stage trace("Background update of " + folder + " of " + accountId, 0);
            std::optional<Trace::Stage> step;
            step.emplace("Sign-in credentials (vault, OAuth token)", 0);
            UltraNetResult prepared = prepare ? prepare(opts) : UltraNetResult::Ok();
            // New mail, then the reconcile of read/deleted state for the messages
            // we already had — this is what surfaces changes made on another client
            // (e.g. Gmail's web UI) — and the mail an earlier sync missed. Only the
            // new-mail step can fail the call.
            step.emplace(folder + ": new mail, flags, missing bodies", 0);
            result = prepared
                ? engine_.RefreshFolder(accountId, folder, serverUrl, opts,
                                        /*fetchBodies=*/true, onProgress)
                : SyncOutcome::Fail(prepared);
            step.reset();
            // The server would not open it: deleted or renamed there since the
            // folder list was read? Then it leaves the tree, and that is no error.
            if (prepared && !result.ok && !result.NetworkUnreachable() &&
                !engine_.FolderStillListed(accountId, folder, serverUrl, opts)) {
                result = SyncOutcome{};
                result.stats.folderGone = true;
            }
            TraceOutcome(result);
        }
        if (onDone) onDone(result);
    }).detach();
}

} // namespace UltraMail
