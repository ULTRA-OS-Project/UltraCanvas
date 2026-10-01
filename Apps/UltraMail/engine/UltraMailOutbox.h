// Apps/UltraMail/engine/UltraMailOutbox.h
// The persistent send queue. Drafts are queued to an UltraDatabase-backed store
// (surviving restarts); Flush attempts to send each pending item via the SMTP
// plug-in, removing successes and recording failures for retry. Keeps sending
// off the compose path so Send never blocks and survives being offline.
// While a message waits, a copy of it is kept in the account's Drafts folder
// on the server (DraftsKeeper): it is there on every device until the message
// has gone out, and is deleted from Drafts once it has.
// Version: 0.5.0 - a copy in the Drafts folder until the message is sent; the
//                  message keeps its Message-ID and its reply headers
// Version: 0.4.0 - Flush with per-account session options (credentials, username, TLS)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMailComposer.h"
#include "UltraMailSender.h"

#include <UltraDatabase/UltraDatabaseCore.h>
#include <UltraNet/UltraNetPlugins.h>

#include <functional>
#include <string>
#include <vector>

namespace UltraMail {

struct OutboxItem {
    int64_t     id = 0;
    std::string accountId;
    std::string serverUrl;
    Draft       draft;
    int         attempts = 0;
    std::string lastError;
    // The Message-ID of the copy in the Drafts folder ("<…@…>"), given when
    // the message is queued; how the copy is found again to delete it.
    std::string messageId;
    // The folder the copy was saved to; empty while no copy has been saved.
    std::string draftsFolder;
    bool HasDraftCopy() const { return !draftsFolder.empty(); }
};

// A new Message-ID for a message from `fromAddr`: "<time.random@domain>".
std::string NewMessageId(const std::string& fromAddr);

// The message as it is kept in the Drafts folder: a complete RFC 5322 message
// with the item's Message-ID, its reply headers and an X-UltraMail-Outbox
// header (it is waiting to be sent, not a draft being written).
std::string BuildDraftCopy(const OutboxItem& item);

// Keeps the Drafts copies, through the account's IMAP server.
struct DraftsKeeper {
    IMailboxProtocolPlugin* imap = nullptr;
    // For an account: its IMAP server URL, its Drafts folder and the session
    // options (credentials, TLS). A failure means the message has no copy in
    // Drafts this time; it is still sent, and still safe in the outbox.
    std::function<UltraNetResult(const std::string& accountId, std::string& serverUrl,
                                 std::string& draftsFolder, UltraNetMailOptions& options)> prepare;
};

// Persistent queue storage (contacts-style, on its own UltraDatabase connection).
class OutboxStore {
public:
    UltraDbResult Open(const std::string& connectionName, const std::string& databasePath);

    // Mirrors LocalStore / ContactStore, so the UI can tell "the queue is
    // unavailable" from "the queue is empty" and say so.
    bool IsOpen() const { return !connection_.empty(); }

    UltraDbResult Enqueue(const std::string& accountId, const std::string& serverUrl,
                          const Draft& draft, int64_t& outId);
    UltraDbResult ListPending(std::vector<OutboxItem>& out) const;
    UltraDbResult Remove(int64_t id);
    UltraDbResult MarkFailed(int64_t id, const std::string& error);
    // Gives a message queued before migration 3 its Message-ID.
    UltraDbResult SetMessageId(int64_t id, const std::string& messageId);
    // The message's copy is in `folder` now.
    UltraDbResult MarkDraftSaved(int64_t id, const std::string& folder);
    UltraDbResult PendingCount(int& out) const;

private:
    UltraDbResult LoadAttachments(OutboxItem& item) const;
    std::string connection_;
};

// The queue operator: enqueue + flush.
class Outbox {
public:
    explicit Outbox(OutboxStore& store) : store_(store) {}

    UltraDbResult Queue(const std::string& accountId, const std::string& serverUrl,
                        const Draft& draft, int64_t& outId) {
        return store_.Enqueue(accountId, serverUrl, draft, outId);
    }

    // `lastFailure` is the result of the most recent failed send, so the UI can
    // tell the user *why* a message stayed in the queue instead of only that it
    // did. Meaningless when `failed` is 0.
    struct FlushStats {
        int             sent = 0;
        int             failed = 0;
        UltraNetResult  lastFailure;
        // Messages whose copy was saved to the Drafts folder in this pass,
        // and the copies that could not be saved (or, once sent, removed).
        int             draftsSaved = 0;
        int             draftFailures = 0;
        UltraNetResult  lastDraftFailure;
    };

    // Saves a Drafts copy of every waiting message that has none yet. A
    // message whose copy cannot be saved stays in the outbox all the same.
    FlushStats SaveDraftCopies(const DraftsKeeper& drafts);

    // Attempt to send every pending item through `smtp`. `credentialFor` maps an
    // account id to its password (resolved from the credential vault).
    FlushStats Flush(IMailProtocolPlugin& smtp,
                     const std::function<std::string(const std::string&)>& credentialFor);
    // Same, with the session options prepared per account: the credentials
    // (password or OAuth2 bearer token), the username, and the TLS mode of the
    // account's outgoing server. A failed preparation counts as a failed send
    // of that item and is recorded on it, so the reason reaches the user. A
    // serverUrl the resolver sets replaces the one stored with the message.
    using OptionsResolver =
        std::function<UltraNetResult(const std::string& accountId, UltraNetMailOptions& options)>;
    // With `drafts`, each message's Drafts copy is saved before it is sent
    // (when it has none yet) and deleted from Drafts once it has been sent; a
    // message that is not sent keeps its copy.
    FlushStats Flush(IMailProtocolPlugin& smtp, const OptionsResolver& prepare,
                     const DraftsKeeper* drafts = nullptr);

private:
    // Saves `item`'s copy; true when it has one afterwards.
    bool SaveDraftCopy(OutboxItem& item, const DraftsKeeper& drafts, FlushStats& stats);
    // Deletes the sent `item`'s copy from its folder (flags it \Deleted).
    void RemoveDraftCopy(const OutboxItem& item, const DraftsKeeper& drafts, FlushStats& stats);
    OutboxStore& store_;
};

} // namespace UltraMail
