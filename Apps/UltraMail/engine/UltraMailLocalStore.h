// Apps/UltraMail/engine/UltraMailLocalStore.h
// The UltraMail local store: the account / folder / message index built on the
// UltraDatabase module (a SQLite connection). Message bodies live as .eml
// files on disk; this class owns the fast, queryable metadata — including the
// "needs answer" state and the per-account rollups behind the account bar.
// Version: 0.7.0 - CountSentRecipients: how often each address was written to
// Version: 0.6.0 - NeedsAnswerRules: which unanswered mail counts as waiting for
//                  a reply (its age, a sender written to), applied when counted
// Version: 0.5.0 - schema 8: the account's signature (SetAccountSignature)
// Version: 0.4.0 - schema 4: message_security, the per-message verdict of the
//                  content scan (its own table, so an envelope upsert cannot
//                  reset a scan that has already run)
// Last Modified: 2026-09-30
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMailTypes.h"
#include "UltraMailThreatScan.h"

#include <UltraDatabase/UltraDatabaseCore.h>

#include <map>
#include <string>
#include <vector>

namespace UltraMail {

// Which of the unanswered mail sent to the user counts as waiting for a reply.
// The stored needs_answer bit says only that a message *could* be (personal
// mail to the account, in the inbox, not answered); these narrow it when it
// is counted or listed, so changing them needs no re-sync. A message the user
// marked "needs an answer" always counts. The defaults narrow nothing; the
// app passes the reader's choice (Settings > Reading > Waiting for reply).
struct NeedsAnswerRules {
    // Only mail from the last this-many days; 0 = any age.
    int  maxAgeDays = 0;
    // Only senders the user has written to (an address in the recipients of
    // the account's Sent mail). Ignored while the account has no Sent mail in
    // the store, so an unsynced Sent folder does not hide everything.
    bool onlyWrittenTo = false;
};

// A stored scan verdict: what the content scan made of a message's body, and
// when. `reason` is the user-facing text (ThreatReport::Summary()), so the
// badge tooltip and the reading pane's warning can show it without re-scanning.
struct MessageSecurity {
    ThreatLevel level = ThreatLevel::Unscanned;
    int         score = 0;
    bool        bulk  = false;
    std::string reason;
    int64_t     scannedAt = 0;   // epoch seconds
    // Attachments the message carries (what the reading pane lists as
    // chips), counted from the body; -1 while no body has been counted.
    int         attachments = -1;

    bool Scanned() const { return level != ThreatLevel::Unscanned; }
};

class LocalStore {
public:
    // Register an UltraDatabase connection for the store and bring the schema
    // up to date. `databasePath` is a file path (created if absent) or
    // ":memory:". Safe to call again with the same connection name.
    UltraDbResult Open(const std::string& connectionName,
                       const std::string& databasePath);

    bool IsOpen() const { return !connection_.empty(); }
    const std::string& Connection() const { return connection_; }

    // ---- Accounts ----------------------------------------------------------
    // Everything but the signature: re-adding an address, or saving its
    // servers, keeps the signature it has.
    UltraDbResult UpsertAccount(const Account& account);
    UltraDbResult ListAccounts(std::vector<Account>& out) const;
    // Replaces the account's signature. Succeeds without effect for an
    // unknown account id.
    UltraDbResult SetAccountSignature(const std::string& accountId,
                                      const Signature& signature);
    UltraDbResult RemoveAccount(const std::string& accountId);

    // ---- Folders -----------------------------------------------------------
    UltraDbResult UpsertFolder(const Folder& folder);
    UltraDbResult ListFolders(const std::string& accountId,
                              std::vector<Folder>& out) const;

    // The stored IMAP UIDVALIDITY for a folder (0 when unknown / never synced) —
    // the basis for detecting a server-side mailbox renumber.
    UltraDbResult GetFolderUidValidity(const std::string& accountId,
                                       const std::string& folder, int64_t& out) const;
    // Persist the folder's UIDVALIDITY / UIDNEXT after a sync, without disturbing
    // its role/selectable (a targeted update, unlike UpsertFolder).
    UltraDbResult SetFolderUidState(const std::string& accountId, const std::string& folder,
                                    int64_t uidValidity, int64_t uidNext);

    // ---- Messages ----------------------------------------------------------
    // Insert or update an envelope. The store computes and caches whether the
    // message is eligible for "needs answer" (addressed to the account owner,
    // not automated, in the inbox) and the current needs-answer bit.
    UltraDbResult UpsertMessage(const MessageEnvelope& message);

    // Most-recent-first list for a folder (limit 0 = all).
    UltraDbResult ListMessages(const std::string& accountId,
                               const std::string& folder,
                               int limit,
                               std::vector<MessageEnvelope>& out) const;

    // Highest UID already stored for a folder (0 if none) — the basis for
    // incremental sync.
    UltraDbResult GetMaxUid(const std::string& accountId, const std::string& folder,
                            int64_t& out) const;

    // How many stored messages in Sent folders (every account; deleted ones
    // left out) list each address as a To: recipient - how often the user
    // writes to it. Keys are bare addresses, lower case.
    UltraDbResult CountSentRecipients(std::map<std::string, int>& out) const;

    // The rules ListNeedsAnswer and GetAccountStatus count by.
    void SetNeedsAnswerRules(const NeedsAnswerRules& rules) { needsAnswerRules_ = rules; }
    const NeedsAnswerRules& GetNeedsAnswerRules() const { return needsAnswerRules_; }

    // Messages awaiting a reply for one account (most recent first), by the
    // needs-answer rules.
    UltraDbResult ListNeedsAnswer(const std::string& accountId,
                                  std::vector<MessageEnvelope>& out) const;

    // Set or clear flag bits on a message; recomputes the needs-answer bit.
    UltraDbResult SetFlags(const std::string& accountId, const std::string& folder,
                           int64_t uid, uint32_t flags, bool set);

    // Overwrite a message's flags with the exact value the server reported (used
    // by the folder-switch flag reconcile, which cannot express "these flags and
    // no others" through the bit-mask SetFlags); recomputes the needs-answer bit.
    UltraDbResult ReplaceFlags(const std::string& accountId, const std::string& folder,
                               int64_t uid, uint32_t flags);

    // The user's own "needs an answer" choice, overriding the automatic rule
    // (addressed to me, in the inbox, not automated, not \Answered): true keeps
    // the message on the list until it is answered, false keeps it off. Local
    // only - IMAP has no standard flag for it. Answering it (MarkAnswered, or
    // \Answered set from the server) ends a "true" choice.
    UltraDbResult SetNeedsAnswer(const std::string& accountId, const std::string& folder,
                                 int64_t uid, bool needsAnswer);

    // Convenience: mark a message answered (sets \Answered, clears needs-answer).
    UltraDbResult MarkAnswered(const std::string& accountId, const std::string& folder,
                               int64_t uid) {
        return SetFlags(accountId, folder, uid, Flag_Answered, true);
    }

    // Drop a message from the index (its envelope row and any stored scan
    // verdict). Used after a Delete/Junk move takes it out of this folder — the
    // message list does not filter deleted rows, so it must actually be removed.
    UltraDbResult RemoveMessage(const std::string& accountId, const std::string& folder,
                                int64_t uid);

    // Drop every message (and its scan verdict) for a folder — used when the
    // server renumbered the mailbox (UIDVALIDITY changed), so the stale cached
    // UIDs are discarded before a fresh fetch from UID 0.
    UltraDbResult ClearFolderMessages(const std::string& accountId, const std::string& folder);

    // ---- Sender security verdicts -----------------------------------------
    // The content scan's verdict for one message, kept in its own table so an
    // envelope upsert (which happens on every header sync, long before a body
    // is available) can never overwrite a scan that has already run.
    UltraDbResult SetSecurity(const std::string& accountId, const std::string& folder,
                              int64_t uid, const MessageSecurity& security);

    // The verdict for one message; `out` stays Unscanned when none is stored.
    UltraDbResult GetSecurity(const std::string& accountId, const std::string& folder,
                              int64_t uid, MessageSecurity& out) const;

    // Every stored verdict of a folder, keyed by UID — one query per message
    // list rather than one per row.
    UltraDbResult ListSecurity(const std::string& accountId, const std::string& folder,
                               std::map<int64_t, MessageSecurity>& out) const;

    // Record a message's attachment count (see MessageSecurity::attachments)
    // without touching its scan verdict; creates the row when there is none.
    UltraDbResult SetAttachmentCount(const std::string& accountId, const std::string& folder,
                                     int64_t uid, int count);
    // Messages of a folder whose attachments have not been counted yet, newest
    // first, at most `limit`.
    UltraDbResult ListUncountedAttachments(const std::string& accountId,
                                           const std::string& folder, int limit,
                                           std::vector<int64_t>& uids) const;

    // ---- Rollups (account bar) --------------------------------------------
    // One row per account: short name, email, unread (total / today / older)
    // and needs-answer counts. `todayStart` is the epoch second local midnight
    // began; pass -1 (the default) to take it from the wall clock.
    UltraDbResult GetAccountStatus(std::vector<AccountStatus>& out,
                                   int64_t todayStart = -1) const;

    // Epoch second at which the current local day began.
    static int64_t StartOfToday();

private:
    // The SQL condition the needs-answer rules add for the messages row `m`
    // (an alias of messages), at wall-clock time `now`.
    std::string NeedsAnswerRulesSql(int64_t now) const;

    std::string      connection_;
    NeedsAnswerRules needsAnswerRules_;
};

} // namespace UltraMail
