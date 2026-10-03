// Apps/UltraMail/engine/UltraMailSyncEngine.h
// The headless sync engine: drives an IMAP-class mailbox (UltraNet's
// IMailboxProtocolPlugin) into the LocalStore. It lists folders, fetches new
// envelopes incrementally, optionally caches raw message bodies as .eml files
// (for the reading view + attachments via MimeCodec), and mirrors flag changes
// to the server.
//
// It is driven synchronously on the caller's thread; the app runs it on a
// per-account worker and marshals results to the UI. Because it depends only on
// the IMailboxProtocolPlugin interface, it is fully testable with a fake
// mailbox — no live server required.
// Version: 0.1.2 - EmptyFolder (Empty Trash)
// Version: 0.1.1 - DeleteForGood: \Deleted, then UID EXPUNGE of that message
// Version: 0.1.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMailTypes.h"
#include "UltraMailLocalStore.h"

#include <UltraNet/UltraNetPlugins.h>

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_set>

namespace UltraMail {

struct SyncStats {
    int folders  = 0;   // folders upserted
    int messages = 0;   // envelopes upserted
    int bodies   = 0;   // full bodies fetched + cached
    int reconciled = 0; // existing messages whose flags were corrected from server
    int expunged   = 0; // local messages dropped because the server no longer has them
    int bodiesRemoved = 0; // cached .eml files deleted with them, or left over from before
};

struct SyncOutcome {
    bool        ok = true;
    std::string message;
    SyncStats   stats;
    // On a network failure, the connection chain the plug-in reported
    // (UltraNetResult::diagnostics): component, server, TLS, libraries, roots.
    std::string diagnostics;
    // Why it failed, as UltraNet classified it (Success when ok, Unknown for
    // a failure that did not come from UltraNet). The app reads it to tell
    // "the network is not there" from "the server said no".
    UltraNetResultCode code = UltraNetResultCode::Success;

    explicit operator bool() const { return ok; }
    static SyncOutcome Fail(const std::string& m) {
        return SyncOutcome{false, m, {}, {}, UltraNetResultCode::Unknown};
    }
    static SyncOutcome Fail(const UltraNetResult& r) {
        return SyncOutcome{false, r.message, {}, r.diagnostics, r.code};
    }

    // True when the failure means the server could not be reached at all -
    // no name resolution, no route, nobody listening, or the connection timed
    // out - which right after boot, or on a laptop between two networks,
    // usually means the network is not up yet rather than that anything is
    // wrong with the account. A failure the server itself produced (a rejected
    // password, an untrusted certificate, a refused mailbox) is never this.
    bool NetworkUnreachable() const {
        switch (code) {
            case UltraNetResultCode::HostNotFound:
            case UltraNetResultCode::ConnectionRefused:
            case UltraNetResultCode::ConnectionReset:
            case UltraNetResultCode::ConnectionTimeout:
            case UltraNetResultCode::Timeout:
                return !ok;
            default:
                return false;
        }
    }
};

class SyncEngine {
public:
    // `emlDir` is the root under which raw message bodies are cached
    // (emlDir/<accountId>/<folder>/<uid>.eml).
    SyncEngine(LocalStore& store, IMailboxProtocolPlugin& mailbox, std::string emlDir)
        : store_(store), mailbox_(mailbox), emlDir_(std::move(emlDir)) {}

    // LIST folders on the server and upsert them for the account (with role
    // detection carried through from the plug-in).
    SyncOutcome SyncFolders(const std::string& accountId,
                            const std::string& serverUrl,
                            const UltraNetMailOptions& options);

    // Fetch envelopes with UID greater than the highest already stored, upsert
    // them, and — when fetchBodies is true — cache each new message's raw body.
    // `onMessageStored`, when set, is invoked (on the caller's thread) with each
    // decoded envelope right after it is upserted, so a caller can stream new
    // messages into the UI as their headers arrive instead of waiting for the
    // whole mailbox. Bodies are still fetched in one batch afterwards.
    SyncOutcome SyncMessages(const std::string& accountId,
                             const std::string& folder,
                             const std::string& serverUrl,
                             const UltraNetMailOptions& options,
                             bool fetchBodies = false,
                             const std::function<void(const MessageEnvelope&)>& onMessageStored = {});

    // Fetch and cache one message body; returns the .eml path (empty on failure).
    std::string FetchBody(const std::string& accountId, const std::string& folder,
                          int64_t uid, const std::string& serverUrl,
                          const UltraNetMailOptions& options);

    // Path where a message body is (or would be) cached; same as the free
    // CachedBodyPath below.
    std::string BodyPath(const std::string& accountId, const std::string& folder,
                         int64_t uid) const;

    // Write a raw message body to its cache path; returns the path (empty on
    // failure or empty input). Shared by FetchBody and the batched body sync.
    std::string WriteBody(const std::string& accountId, const std::string& folder,
                          int64_t uid, const std::string& raw) const;

    // Count the attachments of cached bodies that have no count yet (at most
    // `limit`, newest first) - for mail downloaded before counting existed.
    // Runs at the end of SyncMessages. Returns how many were counted.
    int CountStoredAttachments(const std::string& accountId, const std::string& folder,
                               int limit = 300);

    // Set/clear a flag on the server (UID STORE) and in the local index.
    SyncOutcome SetFlag(const std::string& accountId, const std::string& folder,
                        int64_t uid, uint32_t ultramailFlag, bool set,
                        const std::string& serverUrl,
                        const UltraNetMailOptions& options);

    // Reconcile the read/deleted state of messages already stored for a folder
    // with the server (UID FETCH 1:* (FLAGS)): correct flags that were changed on
    // another client and expunge locally-held messages the server no longer
    // lists. New UIDs are left to SyncMessages. Non-fatal: if the server flag
    // list cannot be fetched (unsupported backend or a transient error), nothing
    // is expunged and the call still reports success.
    SyncOutcome ReconcileFlags(const std::string& accountId, const std::string& folder,
                               const std::string& serverUrl,
                               const UltraNetMailOptions& options);

    // Drop a message from the local index AND its cached body - the two always
    // go together, or the body cache only ever grows (and another reader of it,
    // such as EmailCleaner, keeps finding mail that is gone). Local only.
    UltraDbResult ForgetMessage(const std::string& accountId, const std::string& folder,
                                int64_t uid);

    // Delete the cached bodies of a folder the index no longer holds, with a
    // UID no higher than `maxUid` - bodies left behind by versions that dropped
    // only the index row. The bound keeps a body a concurrent sync is writing
    // (always a newer, higher UID) safe. Returns how many were deleted.
    int PruneBodies(const std::string& accountId, const std::string& folder,
                    const std::unordered_set<int64_t>& keep, int64_t maxUid);

    // Move a message to another folder on the server (UID MOVE) and drop it from
    // the local index for the source folder — used by Delete (to Trash) and Junk
    // (to the Junk mailbox). Its cached body goes too.
    SyncOutcome MoveMessage(const std::string& accountId, const std::string& srcFolder,
                            int64_t uid, const std::string& dstFolder,
                            const std::string& serverUrl,
                            const UltraNetMailOptions& options);

    // Delete a message permanently - Delete in a folder with no Trash to move
    // it to, or in Trash itself: flag it \Deleted, expunge that one message
    // (UID EXPUNGE; never the others flagged \Deleted in the folder), and drop
    // it from the local index with its cached body. A server without UIDPLUS
    // refuses the expunge; the message then stays flagged \Deleted (deleted,
    // to be removed by the server or another client), which is no failure.
    SyncOutcome DeleteForGood(const std::string& accountId, const std::string& folder,
                              int64_t uid, const std::string& serverUrl,
                              const UltraNetMailOptions& options);

    // Empty a folder for good - Empty Trash: every message in it is flagged
    // \Deleted and expunged on the server (IMailboxProtocolPlugin::
    // EmptyFolder), then dropped from the local index with its cached body.
    // One that arrives meanwhile stays on the server and comes back with the
    // next sync.
    SyncOutcome EmptyFolder(const std::string& accountId, const std::string& folder,
                            const std::string& serverUrl, const UltraNetMailOptions& options);

private:
    LocalStore&             store_;
    IMailboxProtocolPlugin& mailbox_;
    std::string             emlDir_;
};

// Path where a message body is (or would be) cached under `emlDir`
// (emlDir/<accountId>/<folder>/<uid>.eml), for callers without a SyncEngine.
std::string CachedBodyPath(const std::string& emlDir, const std::string& accountId,
                           const std::string& folder, int64_t uid);

} // namespace UltraMail
