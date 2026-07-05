// Apps/UltraMail/engine/UltraMailLocalStore.cpp
// LocalStore implementation on top of UltraDatabase.
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailLocalStore.h"

#include <UltraDatabase/UltraDatabase.h>

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>

namespace UltraMail {

namespace {

std::string Lower(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return r;
}

std::string Join(const std::vector<std::string>& parts, char sep) {
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) out.push_back(sep);
        out += parts[i];
    }
    return out;
}

std::vector<std::string> Split(const std::string& s, char sep) {
    std::vector<std::string> out;
    if (s.empty()) return out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, sep)) out.push_back(item);
    return out;
}

// Read one message row (columns in the fixed order used by the SELECTs below).
MessageEnvelope RowToEnvelope(const UltraDbRow& row) {
    MessageEnvelope m;
    m.accountId = row["account_id"].AsString();
    m.folder    = row["folder"].AsString();
    m.uid       = row["uid"].AsInt64();
    m.messageId = row["message_id"].AsString();
    m.inReplyTo = row["in_reply_to"].AsString();
    m.subject   = row["subject"].AsString();
    m.fromName  = row["from_name"].AsString();
    m.fromAddr  = row["from_addr"].AsString();
    m.to        = Split(row["to_addrs"].AsString(), '\n');
    m.date      = row["date"].AsInt64();
    m.flags     = row["flags"].AsU32();
    return m;
}

const char* kMsgColumns =
    "account_id, folder, uid, message_id, in_reply_to, subject, "
    "from_name, from_addr, to_addrs, date, flags";

} // namespace

// ---- Open / schema ---------------------------------------------------------

UltraDbResult LocalStore::Open(const std::string& connectionName,
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
          "  display_name TEXT,"
          "  email TEXT,"
          "  short_name TEXT);"
          "CREATE TABLE folders("
          "  account_id TEXT NOT NULL,"
          "  name TEXT NOT NULL,"
          "  role TEXT,"
          "  uidvalidity INTEGER DEFAULT 0,"
          "  uidnext INTEGER DEFAULT 0,"
          "  PRIMARY KEY(account_id, name));"
          "CREATE TABLE messages("
          "  account_id TEXT NOT NULL,"
          "  folder TEXT NOT NULL,"
          "  uid INTEGER NOT NULL,"
          "  message_id TEXT,"
          "  in_reply_to TEXT,"
          "  subject TEXT,"
          "  from_name TEXT,"
          "  from_addr TEXT,"
          "  to_addrs TEXT,"
          "  date INTEGER DEFAULT 0,"
          "  flags INTEGER DEFAULT 0,"
          "  eligible INTEGER DEFAULT 0,"
          "  needs_answer INTEGER DEFAULT 0,"
          "  PRIMARY KEY(account_id, folder, uid));"
          "CREATE INDEX idx_messages_folder ON messages(account_id, folder, date DESC);"
          "CREATE INDEX idx_messages_needs ON messages(account_id, needs_answer);" },
    };
    return UltraDb_Migrate(connection_, steps);
}

// ---- Accounts --------------------------------------------------------------

UltraDbResult LocalStore::UpsertAccount(const Account& a) {
    if (a.accountId.empty())
        return UltraDbResult::Error(UltraDbResultCode::InvalidArgument,
                                    "account id must not be empty");
    return UltraDb_Exec(connection_,
        "INSERT INTO accounts(account_id, display_name, email, short_name) "
        "VALUES(?, ?, ?, ?) "
        "ON CONFLICT(account_id) DO UPDATE SET "
        "display_name=excluded.display_name, email=excluded.email, "
        "short_name=excluded.short_name",
        { a.accountId, a.displayName, a.email, a.shortName });
}

UltraDbResult LocalStore::ListAccounts(std::vector<Account>& out) const {
    out.clear();
    UltraDbResultSet rs;
    UltraDbResult q = UltraDb_Query(connection_,
        "SELECT account_id, display_name, email, short_name FROM accounts "
        "ORDER BY short_name", rs);
    if (!q) return q;
    for (const auto& row : rs) {
        Account a;
        a.accountId   = row["account_id"].AsString();
        a.displayName = row["display_name"].AsString();
        a.email       = row["email"].AsString();
        a.shortName   = row["short_name"].AsString();
        out.push_back(std::move(a));
    }
    return UltraDbResult::Ok();
}

UltraDbResult LocalStore::RemoveAccount(const std::string& accountId) {
    UltraDbHandle tx = UltraDb_Begin(connection_);
    if (tx == UltraDbInvalidHandle)
        return UltraDbResult::Error(UltraDbResultCode::Internal, "begin failed");
    UltraDb_ExecInTx(tx, "DELETE FROM messages WHERE account_id=?", { accountId });
    UltraDb_ExecInTx(tx, "DELETE FROM folders WHERE account_id=?", { accountId });
    UltraDb_ExecInTx(tx, "DELETE FROM accounts WHERE account_id=?", { accountId });
    return UltraDb_Commit(tx);
}

// ---- Folders ---------------------------------------------------------------

UltraDbResult LocalStore::UpsertFolder(const Folder& f) {
    return UltraDb_Exec(connection_,
        "INSERT INTO folders(account_id, name, role, uidvalidity, uidnext) "
        "VALUES(?, ?, ?, ?, ?) "
        "ON CONFLICT(account_id, name) DO UPDATE SET "
        "role=excluded.role, uidvalidity=excluded.uidvalidity, "
        "uidnext=excluded.uidnext",
        { f.accountId, f.name, ToString(f.role), f.uidValidity, f.uidNext });
}

UltraDbResult LocalStore::ListFolders(const std::string& accountId,
                                      std::vector<Folder>& out) const {
    out.clear();
    UltraDbResultSet rs;
    UltraDbResult q = UltraDb_Query(connection_,
        "SELECT account_id, name, role, uidvalidity, uidnext FROM folders "
        "WHERE account_id=? ORDER BY name", { accountId }, rs);
    if (!q) return q;
    for (const auto& row : rs) {
        Folder f;
        f.accountId   = row["account_id"].AsString();
        f.name        = row["name"].AsString();
        f.role        = FolderRoleFromString(row["role"].AsString());
        f.uidValidity = row["uidvalidity"].AsInt64();
        f.uidNext     = row["uidnext"].AsInt64();
        out.push_back(std::move(f));
    }
    return UltraDbResult::Ok();
}

// ---- Messages --------------------------------------------------------------

UltraDbResult LocalStore::UpsertMessage(const MessageEnvelope& m) {
    // Resolve the owning account's address and the folder role to decide
    // eligibility for "needs answer".
    std::string accountEmail;
    {
        UltraDbResultSet rs;
        UltraDbResult q = UltraDb_Query(connection_,
            "SELECT email FROM accounts WHERE account_id=?", { m.accountId }, rs);
        if (!q) return q;
        if (!rs.Empty()) accountEmail = rs.Row(0)["email"].AsString();
    }
    FolderRole role = FolderRole::Normal;
    {
        UltraDbResultSet rs;
        UltraDbResult q = UltraDb_Query(connection_,
            "SELECT role FROM folders WHERE account_id=? AND name=?",
            { m.accountId, m.folder }, rs);
        if (!q) return q;
        if (!rs.Empty()) role = FolderRoleFromString(rs.Row(0)["role"].AsString());
    }

    const std::string ownLower = Lower(accountEmail);
    bool toMe = false;
    for (const auto& addr : m.to)
        if (!ownLower.empty() && Lower(addr) == ownLower) { toMe = true; break; }
    const bool fromSelf = !ownLower.empty() && Lower(m.fromAddr) == ownLower;

    const bool eligible = toMe && !m.automated && role == FolderRole::Inbox && !fromSelf;
    const bool needsAnswer = eligible && (m.flags & Flag_Answered) == 0;

    return UltraDb_Exec(connection_,
        "INSERT INTO messages(account_id, folder, uid, message_id, in_reply_to, "
        "subject, from_name, from_addr, to_addrs, date, flags, eligible, needs_answer) "
        "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(account_id, folder, uid) DO UPDATE SET "
        "message_id=excluded.message_id, in_reply_to=excluded.in_reply_to, "
        "subject=excluded.subject, from_name=excluded.from_name, "
        "from_addr=excluded.from_addr, to_addrs=excluded.to_addrs, "
        "date=excluded.date, flags=excluded.flags, eligible=excluded.eligible, "
        "needs_answer=excluded.needs_answer",
        { m.accountId, m.folder, m.uid, m.messageId, m.inReplyTo, m.subject,
          m.fromName, m.fromAddr, Join(m.to, '\n'), m.date, m.flags,
          eligible ? 1 : 0, needsAnswer ? 1 : 0 });
}

UltraDbResult LocalStore::ListMessages(const std::string& accountId,
                                       const std::string& folder, int limit,
                                       std::vector<MessageEnvelope>& out) const {
    out.clear();
    std::string sql = std::string("SELECT ") + kMsgColumns +
        " FROM messages WHERE account_id=? AND folder=? ORDER BY date DESC";
    UltraDbParams params = { accountId, folder };
    if (limit > 0) { sql += " LIMIT ?"; params.push_back(limit); }

    UltraDbResultSet rs;
    UltraDbResult q = UltraDb_Query(connection_, sql, params, rs);
    if (!q) return q;
    for (const auto& row : rs) out.push_back(RowToEnvelope(row));
    return UltraDbResult::Ok();
}

UltraDbResult LocalStore::ListNeedsAnswer(const std::string& accountId,
                                          std::vector<MessageEnvelope>& out) const {
    out.clear();
    std::string sql = std::string("SELECT ") + kMsgColumns +
        " FROM messages WHERE account_id=? AND needs_answer=1 AND (flags & " +
        std::to_string(Flag_Deleted) + ")=0 ORDER BY date DESC";
    UltraDbResultSet rs;
    UltraDbResult q = UltraDb_Query(connection_, sql, { accountId }, rs);
    if (!q) return q;
    for (const auto& row : rs) out.push_back(RowToEnvelope(row));
    return UltraDbResult::Ok();
}

UltraDbResult LocalStore::SetFlags(const std::string& accountId,
                                   const std::string& folder, int64_t uid,
                                   uint32_t flags, bool set) {
    UltraDbResultSet rs;
    UltraDbResult q = UltraDb_Query(connection_,
        "SELECT flags, eligible FROM messages "
        "WHERE account_id=? AND folder=? AND uid=?",
        { accountId, folder, uid }, rs);
    if (!q) return q;
    if (rs.Empty())
        return UltraDbResult::Error(UltraDbResultCode::NotFound, "message not found");

    uint32_t current = rs.Row(0)["flags"].AsU32();
    bool eligible = rs.Row(0)["eligible"].AsInt64() != 0;
    uint32_t updated = set ? (current | flags) : (current & ~flags);
    bool needsAnswer = eligible && (updated & Flag_Answered) == 0;

    return UltraDb_Exec(connection_,
        "UPDATE messages SET flags=?, needs_answer=? "
        "WHERE account_id=? AND folder=? AND uid=?",
        { updated, needsAnswer ? 1 : 0, accountId, folder, uid });
}

// ---- Rollups ---------------------------------------------------------------

UltraDbResult LocalStore::GetAccountStatus(std::vector<AccountStatus>& out) const {
    out.clear();
    // Flag constants are internal literals (safe to inline into the SQL).
    const std::string seen = std::to_string(Flag_Seen);
    const std::string del  = std::to_string(Flag_Deleted);

    std::string sql =
        "SELECT a.account_id AS account_id, a.short_name AS short_name, "
        "COALESCE(SUM(CASE WHEN f.role='inbox' AND (m.flags & " + seen + ")=0 "
        "  AND (m.flags & " + del + ")=0 THEN 1 ELSE 0 END), 0) AS unread, "
        "COALESCE(SUM(CASE WHEN m.needs_answer=1 AND (m.flags & " + del + ")=0 "
        "  THEN 1 ELSE 0 END), 0) AS needs "
        "FROM accounts a "
        "LEFT JOIN messages m ON m.account_id = a.account_id "
        "LEFT JOIN folders f ON f.account_id = m.account_id AND f.name = m.folder "
        "GROUP BY a.account_id, a.short_name "
        "ORDER BY a.short_name";

    UltraDbResultSet rs;
    UltraDbResult q = UltraDb_Query(connection_, sql, rs);
    if (!q) return q;
    for (const auto& row : rs) {
        AccountStatus s;
        s.accountId   = row["account_id"].AsString();
        s.shortName   = row["short_name"].AsString();
        s.unread      = row["unread"].AsInt();
        s.needsAnswer = row["needs"].AsInt();
        out.push_back(std::move(s));
    }
    return UltraDbResult::Ok();
}

} // namespace UltraMail
