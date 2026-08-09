// Apps/UltraSocial/engine/UltraSocialStore.cpp
// Store implementation on top of UltraDatabase.
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraSocialStore.h"

#include <UltraDatabase/UltraDatabase.h>

#include <ctime>

namespace UltraSocial {

namespace {

HistoryEntry RowToHistory(const UltraDbRow& row) {
    HistoryEntry e;
    e.id         = row["id"].AsInt64();
    e.accountId  = row["account_id"].AsString();
    e.network    = SocialNetworkFromString(row["network"].AsString());
    e.text       = row["text"].AsString();
    e.mediaCount = static_cast<int>(row["media_count"].AsInt64());
    e.succeeded  = row["succeeded"].AsInt64() != 0;
    e.postId     = row["post_id"].AsString();
    e.url        = row["url"].AsString();
    e.error      = row["error"].AsString();
    e.createdAt  = row["created_at"].AsInt64();
    return e;
}

const char* kHistoryColumns =
    "id, account_id, network, text, media_count, succeeded, post_id, url, "
    "error, created_at";

} // namespace

UltraDbResult Store::Open(const std::string& connectionName,
                          const std::string& databasePath) {
    UltraDbConnectionConfig cfg;
    cfg.name     = connectionName;
    cfg.driver   = "sqlite";
    cfg.database = databasePath;
    UltraDbResult reg = UltraDb_RegisterConnection(cfg);
    if (!reg) return reg;

    connection_ = connectionName;

    std::vector<UltraDbMigration> steps = {
        { 1, "initial schema",
          "CREATE TABLE accounts("
          "  account_id TEXT PRIMARY KEY,"
          "  network TEXT NOT NULL,"
          "  display_name TEXT,"
          "  handle TEXT,"
          "  server TEXT);"
          "CREATE TABLE history("
          "  id INTEGER PRIMARY KEY,"
          "  account_id TEXT NOT NULL,"
          "  network TEXT NOT NULL,"
          "  text TEXT,"
          "  media_count INTEGER DEFAULT 0,"
          "  succeeded INTEGER DEFAULT 0,"
          "  post_id TEXT,"
          "  url TEXT,"
          "  error TEXT,"
          "  created_at INTEGER DEFAULT 0);"
          "CREATE INDEX idx_history_account ON history(account_id, id DESC);" },
    };
    return UltraDb_Migrate(connection_, steps);
}

// ---- Accounts --------------------------------------------------------------

UltraDbResult Store::UpsertAccount(const Account& a) {
    if (a.accountId.empty())
        return UltraDbResult::Error(UltraDbResultCode::InvalidArgument,
                                    "account id must not be empty");
    return UltraDb_Exec(connection_,
        "INSERT INTO accounts(account_id, network, display_name, handle, server) "
        "VALUES(?, ?, ?, ?, ?) "
        "ON CONFLICT(account_id) DO UPDATE SET "
        "network=excluded.network, display_name=excluded.display_name, "
        "handle=excluded.handle, server=excluded.server",
        { a.accountId, ToString(a.network), a.displayName, a.handle, a.server });
}

UltraDbResult Store::ListAccounts(std::vector<Account>& out) const {
    out.clear();
    UltraDbResultSet rs;
    UltraDbResult q = UltraDb_Query(connection_,
        "SELECT account_id, network, display_name, handle, server "
        "FROM accounts ORDER BY network, handle", rs);
    if (!q) return q;
    for (const auto& row : rs) {
        Account a;
        a.accountId   = row["account_id"].AsString();
        a.network     = SocialNetworkFromString(row["network"].AsString());
        a.displayName = row["display_name"].AsString();
        a.handle      = row["handle"].AsString();
        a.server      = row["server"].AsString();
        out.push_back(std::move(a));
    }
    return q;
}

UltraDbResult Store::RemoveAccount(const std::string& accountId) {
    return UltraDb_Exec(connection_,
        "DELETE FROM accounts WHERE account_id = ?", { accountId });
}

// ---- History ---------------------------------------------------------------

UltraDbResult Store::AddHistory(HistoryEntry& entry) {
    if (entry.createdAt == 0) {
        entry.createdAt = static_cast<int64_t>(std::time(nullptr));
    }
    UltraDbResult ins = UltraDb_Exec(connection_,
        "INSERT INTO history(account_id, network, text, media_count, "
        "succeeded, post_id, url, error, created_at) "
        "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?)",
        { entry.accountId, ToString(entry.network), entry.text,
          entry.mediaCount, entry.succeeded ? 1 : 0, entry.postId,
          entry.url, entry.error, entry.createdAt });
    if (ins) entry.id = ins.lastInsertId;
    return ins;
}

UltraDbResult Store::ListHistory(const std::string& accountId,
                                 int limit,
                                 std::vector<HistoryEntry>& out) const {
    out.clear();
    std::string sql = std::string("SELECT ") + kHistoryColumns +
                      " FROM history";
    UltraDbParams params;
    if (!accountId.empty()) {
        sql += " WHERE account_id = ?";
        params.push_back(accountId);
    }
    sql += " ORDER BY id DESC";
    if (limit > 0) {
        sql += " LIMIT ?";
        params.push_back(limit);
    }
    UltraDbResultSet rs;
    UltraDbResult q = UltraDb_Query(connection_, sql, params, rs);
    if (!q) return q;
    for (const auto& row : rs) out.push_back(RowToHistory(row));
    return q;
}

} // namespace UltraSocial
