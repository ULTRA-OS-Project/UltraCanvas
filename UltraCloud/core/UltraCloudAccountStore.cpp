// UltraCloud/core/UltraCloudAccountStore.cpp
// Version: 0.1.0
// Last Modified: 2026-09-03
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
