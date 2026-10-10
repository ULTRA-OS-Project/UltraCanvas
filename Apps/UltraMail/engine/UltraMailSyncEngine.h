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
// Version: 0.5.0 - CreateFolder / DeleteFolder
// Version: 0.4.0 - RescanStaleVerdicts (verdicts of older rules scanned again)
// Version: 0.3.0 - SyncFolders: the server's separator kept, folders it no
//                  longer lists dropped
// Version: 0.2.0 - RefreshFolder: new mail, the reconcile with the server's
//                  list, and the mail an incremental fetch can no longer reach
//                  (skipped by an interrupted sync, stored blank, or below a
//                  stale highest UID); a cache whose UIDs the mailbox has not
//                  handed out yet (UIDNEXT) is dropped like a UIDVALIDITY change
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
#include <vector>

namespace UltraMail {

struct SyncStats {
    int folders  = 0;   // folders upserted
    int foldersRemoved = 0;   // folders the server no longer lists, dropped with their mail
    // The folder asked for could not be opened because the server no longer
    // has it (deleted or renamed there); it was dropped. Not a failure.
    bool folderGone = false;
    int messages = 0;   // envelopes upserted
    int bodies   = 0;   // full bodies fetched + cached
    int reconciled = 0; // existing messages whose flags were corrected from server
    int expunged   = 0; // local messages dropped because the server no longer has them
    int bodiesRemoved = 0; // cached .eml files deleted with them, or left over from before
    int repaired   = 0; // messages the server lists that the store lacked (or held blank)
    // What the server's STATUS said the folder holds; -1 when it was not read.
    int serverMessages = -1;
    // The folder's cache was dropped and fetched again: the server renumbered
    // the mailbox, or the cache held UIDs the mailbox has not handed out.
    bool cacheReset = false;
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
    // detection and the hierarchy separator carried through from the
    // plug-in). A folder stored before that the list no longer names - deleted
    // or renamed on the server - is dropped with its messages and cached
    // bodies; never on an empty list, and never INBOX.
    SyncOutcome SyncFolders(const std::string& accountId,
                            const std::string& serverUrl,
                            const UltraNetMailOptions& options);

    // After `folder` could not be opened: read the folder list again (which
    // drops a folder the server no longer names) and say whether the folder is
    // still there. True when the list cannot be read - only a list that no
    // longer names it says it is gone.
    bool FolderStillListed(const std::string& accountId, const std::string& folder,
                           const std::string& serverUrl, const UltraNetMailOptions& options);

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

    // The whole refresh of one folder, what a sync runs for it: new mail
    // (SyncMessages), then the reconcile with the server's full list
    // (ReconcileFlags: read state, mail gone from the server), then the
    // envelopes (and, with fetchBodies, bodies) of every message the server
    // lists that the store does not hold or holds blank (FetchMissing) - mail
    // an incremental fetch by the highest UID can never reach again. Fails
    // only when the new-mail step fails; the reconcile and the repair are
    // best-effort.
    SyncOutcome RefreshFolder(const std::string& accountId,
                              const std::string& folder,
                              const std::string& serverUrl,
                              const UltraNetMailOptions& options,
                              bool fetchBodies = false,
                              const std::function<void(const MessageEnvelope&)>& onMessageStored = {});

    // Fetch and store the envelopes of the named messages (newest first), and
    // with fetchBodies their bodies; `onMessageStored` as for SyncMessages.
    // A UID the server no longer has is skipped.
    SyncOutcome FetchMissing(const std::string& accountId,
                             const std::string& folder,
                             const std::vector<uint32_t>& uids,
                             const std::string& serverUrl,
                             const UltraNetMailOptions& options,
                             bool fetchBodies = false,
                             const std::function<void(const MessageEnvelope&)>& onMessageStored = {});

    // Download the bodies the cache lacks for the folder's newest `limit`
    // messages: a body whose download failed (FetchMessageBodies is best-
    // effort) was otherwise never fetched again, and the reading pane said
    // "not downloaded yet" for good. Returns how many were cached.
    int FetchMissingBodies(const std::string& accountId, const std::string& folder,
                           const std::string& serverUrl, const UltraNetMailOptions& options,
                           int limit = 100);

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

    // Scan again the cached bodies whose verdict came from older rules
    // (kThreatRulesRevision; at most `limit`, newest first), so the list's
    // badges follow the current rules without each message being opened.
    // Runs beside CountStoredAttachments. Returns how many were re-scanned.
    int RescanStaleVerdicts(const std::string& accountId, const std::string& folder,
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
    // is expunged and the call still reports success. `missing`, when given,
    // receives the UIDs the server listed that the store does not hold (empty
    // when the server's list could not be read).
    SyncOutcome ReconcileFlags(const std::string& accountId, const std::string& folder,
                               const std::string& serverUrl,
                               const UltraNetMailOptions& options,
                               std::vector<uint32_t>* missing = nullptr);

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

    // Make `folder` on the server (CREATE; its full name in wire form, see
    // NewFolderName) and read the folder list again, so it enters the tree.
    SyncOutcome CreateFolder(const std::string& accountId, const std::string& folder,
                             const std::string& serverUrl, const UltraNetMailOptions& options);

    // Delete `folder` and the mail in it on the server (DELETE), then drop it
    // here - its messages and cached bodies with it - and read the folder list
    // again. Never INBOX.
    SyncOutcome DeleteFolder(const std::string& accountId, const std::string& folder,
                             const std::string& serverUrl, const UltraNetMailOptions& options);

private:
    // An envelope from the server as the store keeps it (decoded headers,
    // parsed date, local flags).
    MessageEnvelope ToStored(const std::string& accountId, const std::string& folder,
                             const UltraNetMailEnvelope& e) const;

    LocalStore&             store_;
    IMailboxProtocolPlugin& mailbox_;
    std::string             emlDir_;
};

// Path where a message body is (or would be) cached under `emlDir`
// (emlDir/<accountId>/<folder>/<uid>.eml), for callers without a SyncEngine.
std::string CachedBodyPath(const std::string& emlDir, const std::string& accountId,
                           const std::string& folder, int64_t uid);

} // namespace UltraMail
