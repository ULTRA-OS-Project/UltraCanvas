// Apps/UltraMail/engine/UltraMailLocalStore.h
// The UltraMail local store: the account / folder / message index built on the
// UltraDatabase module (a SQLite connection). Message bodies live as .eml
// files on disk; this class owns the fast, queryable metadata — including the
// "needs answer" state and the per-account rollups behind the account bar.
// Version: 0.4.0 - schema 4: message_security, the per-message verdict of the
//                  content scan (its own table, so an envelope upsert cannot
//                  reset a scan that has already run)
// Last Modified: 2026-09-19
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMailTypes.h"
#include "UltraMailThreatScan.h"

#include <UltraDatabase/UltraDatabaseCore.h>

#include <map>
#include <string>
#include <vector>

namespace UltraMail {

// A stored scan verdict: what the content scan made of a message's body, and
// when. `reason` is the user-facing text (ThreatReport::Summary()), so the
// badge tooltip and the reading pane's warning can show it without re-scanning.
struct MessageSecurity {
    ThreatLevel level = ThreatLevel::Unscanned;
    int         score = 0;
    bool        bulk  = false;
    std::string reason;
    int64_t     scannedAt = 0;   // epoch seconds

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
    UltraDbResult UpsertAccount(const Account& account);
    UltraDbResult ListAccounts(std::vector<Account>& out) const;
    UltraDbResult RemoveAccount(const std::string& accountId);

    // ---- Folders -----------------------------------------------------------
    UltraDbResult UpsertFolder(const Folder& folder);
    UltraDbResult ListFolders(const std::string& accountId,
                              std::vector<Folder>& out) const;

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

    // Messages awaiting a reply for one account (most recent first).
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

    // ---- Rollups (account bar) --------------------------------------------
    // One row per account: short name, email, unread (total / today / older)
    // and needs-answer counts. `todayStart` is the epoch second local midnight
    // began; pass -1 (the default) to take it from the wall clock.
    UltraDbResult GetAccountStatus(std::vector<AccountStatus>& out,
                                   int64_t todayStart = -1) const;

    // Epoch second at which the current local day began.
    static int64_t StartOfToday();

private:
    std::string connection_;
};

} // namespace UltraMail
