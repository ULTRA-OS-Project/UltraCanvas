// Apps/UltraMail/engine/UltraMailLocalStore.h
// The UltraMail local store: the account / folder / message index built on the
// UltraDatabase module (a SQLite connection). Message bodies live as .eml
// files on disk; this class owns the fast, queryable metadata — including the
// "needs answer" state and the per-account rollups behind the info-tile bar.
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMailTypes.h"

#include <UltraDatabase/UltraDatabaseCore.h>

#include <string>
#include <vector>

namespace UltraMail {

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

    // Messages awaiting a reply for one account (most recent first).
    UltraDbResult ListNeedsAnswer(const std::string& accountId,
                                  std::vector<MessageEnvelope>& out) const;

    // Set or clear flag bits on a message; recomputes the needs-answer bit.
    UltraDbResult SetFlags(const std::string& accountId, const std::string& folder,
                           int64_t uid, uint32_t flags, bool set);

    // Convenience: mark a message answered (sets \Answered, clears needs-answer).
    UltraDbResult MarkAnswered(const std::string& accountId, const std::string& folder,
                               int64_t uid) {
        return SetFlags(accountId, folder, uid, Flag_Answered, true);
    }

    // ---- Rollups (info-tile bar) ------------------------------------------
    // One row per account: short name, unread and needs-answer counts.
    UltraDbResult GetAccountStatus(std::vector<AccountStatus>& out) const;

private:
    std::string connection_;
};

} // namespace UltraMail
