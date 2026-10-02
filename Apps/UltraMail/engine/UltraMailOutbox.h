// Apps/UltraMail/engine/UltraMailOutbox.h
// The persistent send queue. Drafts are queued to an UltraDatabase-backed store
// (surviving restarts); Flush attempts to send each pending item via the SMTP
// plug-in, removing successes and recording failures for retry. Keeps sending
// off the compose path so Send never blocks and survives being offline.
// While a message waits, a copy of it is kept in the account's Drafts folder
// on the server (ServerCopies): it is there on every device until the message
// has gone out, and is deleted from Drafts once it has - when a copy goes to
// the Sent folder instead (unless the server files sent mail itself).
// Version: 0.8.0 - withdrawn messages: a Drafts copy that cannot be deleted
//                  now is deleted by a later pass; copies are expunged
// Version: 0.7.0 - a copy in the Sent folder once sent; ServerCopies (was
//                  DraftsKeeper); DeleteMessage and held messages for the
//                  outbox window
// Version: 0.6.0 - OutboxRetryClock: when the outbox tries again by itself
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
#include <memory>
#include <mutex>
#include <set>
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
    // Deleted from the outbox (or replaced by a corrected version) but its
    // Drafts copy is not deleted yet: never sent, and not listed as waiting.
    bool withdrawn = false;
};

// A new Message-ID for a message from `fromAddr`: "<time.random@domain>".
std::string NewMessageId(const std::string& fromAddr);

// The message as it is kept on the server: a complete RFC 5322 message with
// the item's Message-ID (the one it is sent with) and its reply headers. The
// Drafts copy (`waiting`) also carries an X-UltraMail-Outbox header: it is
// waiting to be sent, not a draft being written.
std::string BuildMessageCopy(const OutboxItem& item, bool waiting);
inline std::string BuildDraftCopy(const OutboxItem& item) { return BuildMessageCopy(item, true); }

// True for the servers that file what is sent through their SMTP server in
// the Sent folder by themselves (Gmail, Outlook.com / Microsoft 365): a copy
// saved there as well would be a second one.
bool ServerFilesSentMail(const std::string& imapHost);

// Where an account's copies go, and how to sign in there.
struct ServerFolders {
    std::string serverUrl;          // the IMAP server
    std::string draftsFolder;       // "" = no Drafts copy
    std::string sentFolder;         // "" = no Sent copy (or the server files it itself)
    UltraNetMailOptions options;    // credentials, TLS
};

// Keeps the copies on the account's IMAP server: the Drafts copy while a
// message waits, the Sent copy once it has gone out.
struct ServerCopies {
    IMailboxProtocolPlugin* imap = nullptr;
    // For an account: its server, folders and sign-in. A failure means no
    // copies this time; the message is still sent, and still safe in the outbox.
    std::function<UltraNetResult(const std::string& accountId, ServerFolders& out)> prepare;
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
    // The messages waiting to be sent (not the withdrawn ones).
    UltraDbResult ListPending(std::vector<OutboxItem>& out) const;
    // The withdrawn messages, whose Drafts copies are still to be deleted
    // (without their attachments: they are never sent).
    UltraDbResult ListWithdrawn(std::vector<OutboxItem>& out) const;
    UltraDbResult Remove(int64_t id);
    UltraDbResult MarkFailed(int64_t id, const std::string& error);
    // Gives a message queued before migration 3 its Message-ID.
    UltraDbResult SetMessageId(int64_t id, const std::string& messageId);
    // The message's copy is in `folder` now.
    UltraDbResult MarkDraftSaved(int64_t id, const std::string& folder);
    // It will not be sent; the row stays until its Drafts copy is deleted.
    UltraDbResult MarkWithdrawn(int64_t id);
    UltraDbResult PendingCount(int& out) const;     // waiting to be sent
    UltraDbResult WithdrawnCount(int& out) const;   // copies still to delete

    // A message being corrected in a compose window (the outbox window's
    // Edit) is held: Flush leaves it alone, so the old version is not sent
    // while the new one is written. In memory only - after a restart no
    // compose window is open. Safe to call from any thread.
    void SetHeld(int64_t id, bool held);
    bool IsHeld(int64_t id) const;
    int  HeldCount() const;

private:
    UltraDbResult LoadAttachments(OutboxItem& item) const;
    UltraDbResult List(std::vector<OutboxItem>& out, bool withdrawn) const;
    UltraDbResult Count(int& out, bool withdrawn) const;
    std::string connection_;
    // Behind a pointer so the store stays movable.
    struct Held { std::mutex mutex; std::set<int64_t> ids; };
    std::shared_ptr<Held> held_ = std::make_shared<Held>();
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
        // Sent messages filed in the Sent folder, and the ones that could not be.
        int             sentCopies = 0;
        int             sentCopyFailures = 0;
        UltraNetResult  lastSentCopyFailure;
    };

    // Saves a Drafts copy of every waiting message that has none yet. A
    // message whose copy cannot be saved stays in the outbox all the same.
    // Deletes the copies of withdrawn messages first.
    FlushStats SaveDraftCopies(const ServerCopies& copies);

    // What DeleteMessage did.
    enum class DeleteOutcome {
        Deleted,            // out of the outbox, its Drafts copy deleted (or none)
        CopyLeftForLater,   // out of the outbox (withdrawn); the server could not
                            // be reached, so a later pass deletes the copy
        AlreadyGone,        // not waiting any more (sent meanwhile)
    };
    // Takes a waiting message out of the outbox for good - it will not be
    // sent - and deletes its Drafts copy. When `copies` is null or cannot
    // reach the server, the message is withdrawn instead: never sent, and
    // its copy is deleted by the next pass that reaches the server (Flush,
    // SaveDraftCopies). The outbox window's Delete, and Edit's replacement.
    UltraDbResult DeleteMessage(int64_t id, const ServerCopies* copies,
                                DeleteOutcome* outcome = nullptr);

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
    // With `copies`, each message's Drafts copy is saved before it is sent
    // (when it has none yet); once it has been sent a copy goes to the Sent
    // folder and the Drafts copy is deleted. A message not sent keeps its copy.
    // A held message (OutboxStore::SetHeld) is skipped: neither sent nor failed.
    // With `copies`, the copies of withdrawn messages are deleted first.
    FlushStats Flush(IMailProtocolPlugin& smtp, const OptionsResolver& prepare,
                     const ServerCopies* copies = nullptr);

private:
    // Saves `item`'s copy; true when it has one afterwards.
    bool SaveDraftCopy(OutboxItem& item, const ServerCopies& copies, FlushStats& stats);
    // Deletes `item`'s Drafts copy from its folder (flags it \Deleted, then
    // expunges just that message); true when no copy is left there.
    bool RemoveDraftCopy(const OutboxItem& item, const ServerCopies& copies, FlushStats& stats);
    // Deletes the Drafts copies of withdrawn messages, and the rows of those
    // whose copy is gone; the number done.
    int RemoveWithdrawnCopies(const ServerCopies& copies, FlushStats& stats);
    // Files the sent `item` in the Sent folder.
    void SaveSentCopy(const OutboxItem& item, const ServerCopies& copies, FlushStats& stats);
    OutboxStore& store_;
};

// When the outbox tries again by itself, for messages a pass left unsent.
// Each failed pass waits longer before the next - 1, 2, 5 and 10 minutes,
// then every 30 - so a server that is down is not hammered and a short drop
// in the connection is over quickly. A pass that sends everything ends the
// retries; a reason to think the connection is back (a wake from sleep, a
// mail check that reached the server, a start with messages waiting) makes
// the next pass due soon, without forgetting how many have failed.
// Times are seconds on any clock that only moves forward.
class OutboxRetryClock {
public:
    // A pass left messages unsent at `now`: the next one is due later.
    void Failed(int64_t now);
    // Nothing waits any more (or the waiting messages were all sent).
    void Succeeded();
    // The connection is probably back: the next pass is due at `when`
    // (sooner than scheduled, never later).
    void RetryAt(int64_t when);
    // A pass should run now: one is scheduled and its time has come.
    bool Due(int64_t now) const { return nextAt_ > 0 && now >= nextAt_; }
    bool Scheduled() const { return nextAt_ > 0; }
    int64_t NextAt() const { return nextAt_; }   // 0 = none scheduled
    int Failures() const { return failures_; }

    // The wait after the n-th failed pass in a row (n >= 1), in seconds.
    static int64_t DelayAfter(int failures);

private:
    int     failures_ = 0;
    int64_t nextAt_ = 0;
};

} // namespace UltraMail
