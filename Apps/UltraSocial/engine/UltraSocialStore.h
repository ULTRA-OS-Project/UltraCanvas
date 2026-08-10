// Apps/UltraSocial/engine/UltraSocialStore.h
// The UltraSocial local store on UltraDatabase: connected accounts and the
// post history (what was published where, with the resulting permalink or
// the error). Credentials are NOT here — they live in the CredentialVault.
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraSocialTypes.h"

#include <UltraDatabase/UltraDatabaseCore.h>

#include <string>
#include <vector>

namespace UltraSocial {

// One queued (scheduled) post for one account. The raw draft is stored —
// adaptation happens at publish time, so capability changes apply late.
struct OutboxEntry {
    int64_t       id = 0;             // assigned by the store
    std::string   accountId;
    SocialNetwork network = SocialNetwork::Mastodon;
    PostDraft     draft;
    int64_t       scheduledAt = 0;    // epoch seconds; <= now means due
    int           attempts = 0;
    std::string   lastError;
};

// One row of the post history: a single publish attempt to one account.
struct HistoryEntry {
    int64_t       id = 0;             // assigned by the store
    std::string   accountId;
    SocialNetwork network = SocialNetwork::Mastodon;
    std::string   text;               // the adapted text that was sent
    int           mediaCount = 0;
    bool          succeeded = false;
    std::string   postId;             // network-native id (when succeeded)
    std::string   url;                // permalink (when derivable)
    std::string   error;              // failure message (when !succeeded)
    int64_t       createdAt = 0;      // epoch seconds
};

class Store {
public:
    // Register an UltraDatabase connection and bring the schema up to date.
    // `databasePath` is a file path (created if absent) or ":memory:".
    UltraDbResult Open(const std::string& connectionName,
                       const std::string& databasePath);

    bool IsOpen() const { return !connection_.empty(); }
    const std::string& Connection() const { return connection_; }

    // ---- Accounts ----------------------------------------------------------
    UltraDbResult UpsertAccount(const Account& account);
    UltraDbResult ListAccounts(std::vector<Account>& out) const;
    UltraDbResult RemoveAccount(const std::string& accountId);

    // ---- Outbox (scheduled posts) ------------------------------------------
    // Inserts the entry and fills entry.id.
    UltraDbResult EnqueuePost(OutboxEntry& entry);

    // All queued entries, soonest first.
    UltraDbResult ListOutbox(std::vector<OutboxEntry>& out) const;

    // Entries whose scheduledAt is <= now, soonest first.
    UltraDbResult DueOutbox(int64_t now, std::vector<OutboxEntry>& out) const;

    // Cancel / complete an entry.
    UltraDbResult RemoveOutbox(int64_t id);

    // Failed attempt: bump the attempt count, push scheduledAt out, record
    // the error (shown in the scheduled list).
    UltraDbResult RescheduleOutbox(int64_t id, int64_t newScheduledAt,
                                   int attempts, const std::string& lastError);

    // ---- History -----------------------------------------------------------
    // Inserts the entry and fills entry.id.
    UltraDbResult AddHistory(HistoryEntry& entry);

    // Most-recent-first (limit 0 = all); accountId empty = every account.
    UltraDbResult ListHistory(const std::string& accountId,
                              int limit,
                              std::vector<HistoryEntry>& out) const;

private:
    std::string connection_;
};

} // namespace UltraSocial
