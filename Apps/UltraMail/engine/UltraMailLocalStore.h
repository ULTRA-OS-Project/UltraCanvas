// Apps/UltraMail/engine/UltraMailLocalStore.h
// The UltraMail local store: the account / folder / message index built on the
// UltraDatabase module (a SQLite connection). Message bodies live as .eml
// files on disk; this class owns the fast, queryable metadata — including the
// "needs answer" state and the per-account rollups behind the account bar.
// Version: 0.12.1 - MarkVerdictsStale (the scam warnings changed)
// Version: 0.12.0 - schema 11: the codes of a verdict's findings
//                   (MessageSecurity::findings, HasFinding)
// Version: 0.11.0 - schema 10: the verified sender domain with each verdict
//                   (MessageSecurity::verifiedDomain / verifiedBy);
//                   ListStaleVerdicts (verdicts of older rules, to re-scan)
// Version: 0.10.0 - schema 9: the folder's hierarchy separator; RemoveFolder;
//                   UpsertFolder keeps the stored UIDVALIDITY / UIDNEXT
// Version: 0.9.0 - the written-to rule counts "Name <address>" recipients and is
//                  worked out once per change of the Sent mail (it was an
//                  EXISTS over every Sent message for each message counted:
//                  half a second per count on a large mailbox)
// Version: 0.8.0 - ListUids / ListBlankUids (the sync's repair of mail it
//                  missed), ClearAccountMail (another server's mail)
// Version: 0.7.0 - WeighSentRecipients: how often - and how lately - each address
//                  was written to
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
#include <memory>
#include <string>
#include <unordered_set>
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
    // The From domain the receiving server proved genuine (DKIM / DMARC), and
    // how; "" when nothing proved it (ThreatReport::verifiedDomain).
    std::string verifiedDomain;
    std::string verifiedBy;
    // The codes of the findings (ThreatReport::Codes), comma-separated: what
    // the reading pane names the scam by ("romance-scam", "crypto-content").
    std::string findings;

    bool Scanned() const { return level != ThreatLevel::Unscanned; }
    bool HasFinding(const std::string& code) const { return HasFindingCode(findings, code); }
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
    // Insert a folder, or update its role, selectable flag and separator. An
    // existing folder keeps its UIDVALIDITY / UIDNEXT (SetFolderUidState's).
    UltraDbResult UpsertFolder(const Folder& folder);
    UltraDbResult ListFolders(const std::string& accountId,
                              std::vector<Folder>& out) const;
    // Forget a folder the server no longer has (deleted or renamed there):
    // its row, its messages and their scan verdicts. The cached bodies are
    // the sync engine's to delete.
    UltraDbResult RemoveFolder(const std::string& accountId, const std::string& folder);

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

    // Every UID stored for a folder, ascending - what the sync compares with
    // the server's own list to find the mail it has not fetched.
    UltraDbResult ListUids(const std::string& accountId, const std::string& folder,
                           std::vector<int64_t>& out) const;

    // UIDs of the folder's blank rows: no sender, subject, date or Message-ID.
    // Earlier versions stored a message whose header could not be read that
    // way, and never asked for it again; the sync fetches them anew.
    UltraDbResult ListBlankUids(const std::string& accountId, const std::string& folder,
                                std::vector<int64_t>& out) const;

    // How much the user writes to each address: every stored message in a
    // Sent folder (every account; deleted ones left out) adds a weight to
    // each of its To: recipients - 1 for a message sent at `now`, halving
    // every `halfLifeDays` it is older, so recent mail counts more than old
    // mail (a message from a year ago, at 90 days, adds 1/16). Keys are bare
    // addresses, lower case.
    static constexpr double kSentHalfLifeDays = 90.0;
    UltraDbResult WeighSentRecipients(std::map<std::string, double>& out, int64_t now,
                                      double halfLifeDays = kSentHalfLifeDays) const;

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

    // Drop everything downloaded for an account - its messages, their scan
    // verdicts and its folders - but keep the account. For a change of the
    // incoming server: the mail held came from another mailbox, and its UIDs
    // mean nothing on the new one (an incremental sync would even skip the new
    // server's mail below the old highest UID).
    UltraDbResult ClearAccountMail(const std::string& accountId);

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
    // Every stored verdict judged again: the warnings the scan gives changed
    // (Settings > Spam/scam warnings). Each counts as scanned before any rules
    // revision, so ListStaleVerdicts and the reading pane scan it anew.
    UltraDbResult MarkVerdictsStale();
    // Messages of a folder scanned before `rulesRevision` (an epoch second:
    // their verdict came from older rules), newest first, at most `limit`.
    UltraDbResult ListStaleVerdicts(const std::string& accountId, const std::string& folder,
                                    int64_t rulesRevision, int limit,
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
    // The SQL condition the age rule adds for the messages row `m` (an alias
    // of messages), at wall-clock time `now`. The written-to rule is applied
    // in code (PassesWrittenTo), from the sets WrittenToSets returns.
    std::string NeedsAnswerRulesSql(int64_t now) const;

    // Per account with Sent mail stored: the addresses that mail went to
    // ("Name <address>" or bare, lower case). An account without Sent mail
    // has no entry - the rule is left out for it.
    using AddressSet = std::unordered_set<std::string>;
    std::map<std::string, std::shared_ptr<const AddressSet>> WrittenToSets() const;
    // Whether a waiting message from `fromAddr` counts under the written-to
    // rule: always with the user's own mark, or when the account has no Sent
    // mail stored, else when the address was written to.
    static bool PassesWrittenTo(const std::map<std::string, std::shared_ptr<const AddressSet>>& sets,
                                const std::string& accountId, const std::string& fromAddr,
                                int64_t answerMark);

    std::string      connection_;
    NeedsAnswerRules needsAnswerRules_;
    // WrittenToSets' sets, each kept with a stamp of the Sent mail it was read
    // from (count, highest row, UIDs, size) and read again only when that
    // changed. Shared, so a copy of the store (the same database) shares it;
    // locked, because the worker store serves several sync threads.
    struct WrittenToCache;
    std::shared_ptr<WrittenToCache> writtenTo_;
};

} // namespace UltraMail
