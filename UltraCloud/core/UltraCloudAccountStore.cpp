// UltraCloud/core/UltraCloudAccountStore.cpp
// Version: 0.3.0 - a pending upload's completedAt (migration 4)
// Version: 0.2.0 - pending uploads; accounts that upload straight to the name
// Last Modified: 2026-10-19
// Author: UltraCanvas Framework / ULTRA OS
#include <UltraCloud/UltraCloudAccounts.h>

#include <UltraDatabase/UltraDatabase.h>

#include <cctype>
#include <string>
#include <vector>

namespace UltraCloud {

namespace {

Result FromDb(const UltraDbResult& r, const std::string& what) {
    if (r) return Result::Ok();
    return Result::Error(ResultCode::IoError, what + ": " + r.message);
}

Account RowToAccount(const UltraDbRow& row) {
    Account a;
    a.accountId     = row["account_id"].AsString();
    a.providerId    = row["provider_id"].AsString();
    a.displayName   = row["display_name"].AsString();
    a.serverUrl     = row["server_url"].AsString();
    a.username      = row["username"].AsString();
    a.publicBaseUrl = row["public_base_url"].AsString();
    a.remoteFolder  = row["remote_folder"].AsString();
    a.isDefault     = row["is_default"].AsInt() != 0;
    return a;
}

const char* kColumns =
    "account_id, provider_id, display_name, server_url, username, "
    "public_base_url, remote_folder, is_default";

} // namespace

std::string MakeAccountId(const std::string& providerId, const std::string& username,
                          const std::string& serverUrl) {
    // Host without scheme / path, so the id reads "nextcloud-erika-cloud-example-com".
    std::string host = serverUrl;
    if (auto p = host.find("://"); p != std::string::npos) host = host.substr(p + 3);
    if (auto p = host.find('/'); p != std::string::npos) host = host.substr(0, p);

    std::string id;
    auto push = [&id](const std::string& part) {
        if (part.empty()) return;
        if (!id.empty()) id.push_back('-');
        for (char c : part)
            id.push_back(std::isalnum(static_cast<unsigned char>(c))
                             ? static_cast<char>(std::tolower(static_cast<unsigned char>(c)))
                             : '-');
    };
    push(providerId);
    push(username);
    push(host);
    return id;
}

Result AccountStore::Open(const std::string& connectionName, const std::string& databasePath) {
    UltraDbConnectionConfig cfg;
    cfg.name     = connectionName;
    cfg.driver   = "sqlite";
    cfg.database = databasePath;
    UltraDbResult reg = UltraDb_RegisterConnection(cfg);
    if (!reg) return FromDb(reg, "open cloud account store");
    connection_ = connectionName;

    std::vector<UltraDbMigration> steps = {
        { 1, "initial schema",
          "CREATE TABLE cloud_accounts("
          "  account_id TEXT PRIMARY KEY,"
          "  provider_id TEXT NOT NULL,"
          "  display_name TEXT,"
          "  server_url TEXT,"
          "  username TEXT,"
          "  public_base_url TEXT,"
          "  remote_folder TEXT,"
          "  is_default INTEGER DEFAULT 0,"
          "  created_at INTEGER DEFAULT (strftime('%s','now')));" },
        { 2, "uploads to servers that write them as they arrive",
          "CREATE TABLE cloud_pending_uploads("
          "  account_id TEXT NOT NULL,"
          "  remote_path TEXT NOT NULL,"
          "  part_name INTEGER NOT NULL DEFAULT 0,"
          "  file_size INTEGER NOT NULL DEFAULT 0,"
          "  started_at INTEGER NOT NULL DEFAULT 0,"
          "  PRIMARY KEY(account_id, remote_path));" },
        // One statement per step: a step runs as one prepared statement.
        { 3, "accounts whose uploads go straight to the name",
          "CREATE TABLE cloud_direct_uploads("
          "  account_id TEXT PRIMARY KEY,"
          "  server_url TEXT NOT NULL,"
          "  since INTEGER NOT NULL DEFAULT 0);" },
        { 4, "a pending upload that was complete when the connection went",
          "ALTER TABLE cloud_pending_uploads"
          "  ADD COLUMN completed_at INTEGER NOT NULL DEFAULT 0;" },
    };
    return FromDb(UltraDb_Migrate(connection_, steps), "migrate cloud account store");
}

Result AccountStore::Upsert(const Account& account) {
    if (account.accountId.empty() || account.providerId.empty())
        return Result::Error(ResultCode::InvalidArgument, "account needs an id and a provider");

    // The first account stored is the default; otherwise keep the stored flag
    // unless the caller asks for default explicitly (SetDefault clears others).
    UltraDbResultSet rs;
    UltraDbResult q = UltraDb_Query(connection_,
        "SELECT COUNT(*) AS n FROM cloud_accounts WHERE account_id<>?", { account.accountId }, rs);
    if (!q) return FromDb(q, "count cloud accounts");
    const bool others = !rs.Empty() && rs.Row(0)["n"].AsInt() > 0;
    const int isDefault = (!others || account.isDefault) ? 1 : 0;

    UltraDbResult r = UltraDb_Exec(connection_,
        "INSERT INTO cloud_accounts(" + std::string(kColumns) + ") VALUES(?,?,?,?,?,?,?,?) "
        "ON CONFLICT(account_id) DO UPDATE SET provider_id=excluded.provider_id, "
        "display_name=excluded.display_name, server_url=excluded.server_url, "
        "username=excluded.username, public_base_url=excluded.public_base_url, "
        "remote_folder=excluded.remote_folder, "
        "is_default=CASE WHEN excluded.is_default=1 THEN 1 ELSE cloud_accounts.is_default END",
        { account.accountId, account.providerId, account.displayName, account.serverUrl,
          account.username, account.publicBaseUrl, account.remoteFolder, isDefault });
    if (!r) return FromDb(r, "store cloud account");
    if (isDefault) return SetDefault(account.accountId);
    return Result::Ok();
}

Result AccountStore::Remove(const std::string& accountId) {
    Account removed;
    Result got = Get(accountId, removed);
    if (!got) return got;
    UltraDbResult r = UltraDb_Exec(connection_,
        "DELETE FROM cloud_accounts WHERE account_id=?", { accountId });
    if (!r) return FromDb(r, "remove cloud account");
    // What was kept about its uploads goes with it: a new account of the
    // same id is another server, or the same one changed.
    UltraDb_Exec(connection_, "DELETE FROM cloud_pending_uploads WHERE account_id=?",
                 { accountId });
    UltraDb_Exec(connection_, "DELETE FROM cloud_direct_uploads WHERE account_id=?",
                 { accountId });
    // Hand the default to the first remaining account.
    if (removed.isDefault) {
        std::vector<Account> rest;
        if (List(rest) && !rest.empty()) return SetDefault(rest.front().accountId);
    }
    return Result::Ok();
}

Result AccountStore::Get(const std::string& accountId, Account& out) const {
    UltraDbResultSet rs;
    UltraDbResult q = UltraDb_Query(connection_,
        "SELECT " + std::string(kColumns) + " FROM cloud_accounts WHERE account_id=?",
        { accountId }, rs);
    if (!q) return FromDb(q, "read cloud account");
    if (rs.Empty()) return Result::Error(ResultCode::NotFound, "no cloud account " + accountId);
    out = RowToAccount(rs.Row(0));
    return Result::Ok();
}

Result AccountStore::List(std::vector<Account>& out) const {
    out.clear();
    UltraDbResultSet rs;
    UltraDbResult q = UltraDb_Query(connection_,
        "SELECT " + std::string(kColumns) + " FROM cloud_accounts "
        "ORDER BY is_default DESC, display_name COLLATE NOCASE, account_id", rs);
    if (!q) return FromDb(q, "list cloud accounts");
    for (const auto& row : rs) out.push_back(RowToAccount(row));
    return Result::Ok();
}

Result AccountStore::SetDefault(const std::string& accountId) {
    Account a;
    Result got = Get(accountId, a);
    if (!got) return got;
    UltraDbResult r1 = UltraDb_Exec(connection_, "UPDATE cloud_accounts SET is_default=0");
    if (!r1) return FromDb(r1, "clear default cloud account");
    UltraDbResult r2 = UltraDb_Exec(connection_,
        "UPDATE cloud_accounts SET is_default=1 WHERE account_id=?", { accountId });
    return FromDb(r2, "set default cloud account");
}

Result AccountStore::AddPendingUpload(const PendingUpload& upload) {
    if (upload.accountId.empty() || upload.remotePath.empty())
        return Result::Error(ResultCode::InvalidArgument, "a pending upload needs an account and a path");
    UltraDbResult r = UltraDb_Exec(connection_,
        "INSERT INTO cloud_pending_uploads(account_id, remote_path, part_name, file_size, "
        "started_at, completed_at) VALUES(?,?,?,?,?,?) ON CONFLICT(account_id, remote_path) "
        "DO UPDATE SET part_name=excluded.part_name, file_size=excluded.file_size, "
        "started_at=excluded.started_at, completed_at=excluded.completed_at",
        { upload.accountId, upload.remotePath, upload.partName ? 1 : 0,
          static_cast<int64_t>(upload.fileSize), upload.startedAt, upload.completedAt });
    return FromDb(r, "note a pending upload");
}

Result AccountStore::RemovePendingUpload(const std::string& accountId,
                                         const std::string& remotePath) {
    UltraDbResult r = UltraDb_Exec(connection_,
        "DELETE FROM cloud_pending_uploads WHERE account_id=? AND remote_path=?",
        { accountId, remotePath });
    return FromDb(r, "forget a pending upload");
}

Result AccountStore::ListPendingUploads(const std::string& accountId, int64_t now,
                                        std::vector<PendingUpload>& out) {
    out.clear();
    UltraDb_Exec(connection_,
        "DELETE FROM cloud_pending_uploads WHERE account_id=? AND started_at<?",
        { accountId, now - kPendingUploadMaxAgeSeconds });
    UltraDbResultSet rs;
    UltraDbResult q = UltraDb_Query(connection_,
        "SELECT account_id, remote_path, part_name, file_size, started_at, completed_at "
        "FROM cloud_pending_uploads WHERE account_id=? ORDER BY started_at, remote_path",
        { accountId }, rs);
    if (!q) return FromDb(q, "list pending uploads");
    for (const auto& row : rs) {
        PendingUpload u;
        u.accountId = row["account_id"].AsString();
        u.remotePath = row["remote_path"].AsString();
        u.partName = row["part_name"].AsInt() != 0;
        const int64_t size = row["file_size"].AsInt64();
        u.fileSize = size > 0 ? static_cast<uint64_t>(size) : 0;
        u.startedAt = row["started_at"].AsInt64();
        u.completedAt = row["completed_at"].AsInt64();
        out.push_back(std::move(u));
    }
    return Result::Ok();
}

Result AccountStore::SetUploadsDirect(const std::string& accountId,
                                      const std::string& serverUrl, int64_t since) {
    UltraDbResult r = UltraDb_Exec(connection_,
        "INSERT INTO cloud_direct_uploads(account_id, server_url, since) VALUES(?,?,?) "
        "ON CONFLICT(account_id) DO UPDATE SET server_url=excluded.server_url, "
        "since=excluded.since",
        { accountId, serverUrl, since });
    return FromDb(r, "note an account that uploads straight to the name");
}

bool AccountStore::UploadsDirect(const std::string& accountId, const std::string& serverUrl,
                                 int64_t now) {
    UltraDbResultSet rs;
    UltraDbResult q = UltraDb_Query(connection_,
        "SELECT server_url, since FROM cloud_direct_uploads WHERE account_id=?",
        { accountId }, rs);
    if (!q || rs.Empty()) return false;
    if (rs.Row(0)["server_url"].AsString() == serverUrl &&
        now - rs.Row(0)["since"].AsInt64() <= kPendingUploadMaxAgeSeconds)
        return true;
    UltraDb_Exec(connection_, "DELETE FROM cloud_direct_uploads WHERE account_id=?",
                 { accountId });
    return false;
}

Result AccountStore::GetDefault(Account& out) const {
    UltraDbResultSet rs;
    UltraDbResult q = UltraDb_Query(connection_,
        "SELECT " + std::string(kColumns) + " FROM cloud_accounts WHERE is_default=1 LIMIT 1", rs);
    if (!q) return FromDb(q, "read default cloud account");
    if (rs.Empty()) return Result::Error(ResultCode::NotFound, "no default cloud account");
    out = RowToAccount(rs.Row(0));
    return Result::Ok();
}

} // namespace UltraCloud
