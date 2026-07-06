// Apps/UltraMail/engine/UltraMailOutbox.cpp
// Version: 0.1.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailOutbox.h"

#include <UltraDatabase/UltraDatabase.h>

#include <sstream>
#include <string>

namespace UltraMail {

namespace {

std::string Join(const std::vector<std::string>& v, char sep) {
    std::string out;
    for (std::size_t i = 0; i < v.size(); ++i) { if (i) out.push_back(sep); out += v[i]; }
    return out;
}

std::vector<std::string> Split(const std::string& s, char sep) {
    std::vector<std::string> out;
    if (s.empty()) return out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, sep)) if (!item.empty()) out.push_back(item);
    return out;
}

} // namespace

UltraDbResult OutboxStore::Open(const std::string& connectionName,
                                const std::string& databasePath) {
    UltraDbConnectionConfig cfg;
    cfg.name = connectionName; cfg.driver = "sqlite"; cfg.database = databasePath;
    UltraDbResult reg = UltraDb_RegisterConnection(cfg);
    if (!reg) return reg;
    connection_ = connectionName;

    std::vector<UltraDbMigration> steps = {
        { 1, "outbox schema",
          "CREATE TABLE outbox("
          "  id INTEGER PRIMARY KEY,"
          "  account_id TEXT,"
          "  server_url TEXT,"
          "  from_name TEXT,"
          "  from_addr TEXT,"
          "  to_addrs TEXT,"
          "  cc_addrs TEXT,"
          "  bcc_addrs TEXT,"
          "  subject TEXT,"
          "  body TEXT,"
          "  body_is_html INTEGER DEFAULT 0,"
          "  attempts INTEGER DEFAULT 0,"
          "  last_error TEXT,"
          "  created_at TEXT);"
          "CREATE TABLE outbox_attachments("
          "  id INTEGER PRIMARY KEY,"
          "  outbox_id INTEGER NOT NULL,"
          "  filename TEXT,"
          "  media_type TEXT,"
          "  data BLOB);"
          "CREATE INDEX idx_outbox_att ON outbox_attachments(outbox_id);" },
    };
    return UltraDb_Migrate(connection_, steps);
}

UltraDbResult OutboxStore::Enqueue(const std::string& accountId, const std::string& serverUrl,
                                   const Draft& d, int64_t& outId) {
    UltraDbHandle tx = UltraDb_Begin(connection_);
    if (tx == UltraDbInvalidHandle)
        return UltraDbResult::Error(UltraDbResultCode::Internal, "begin failed");

    UltraDbResult ins = UltraDb_ExecInTx(tx,
        "INSERT INTO outbox(account_id, server_url, from_name, from_addr, to_addrs, "
        "cc_addrs, bcc_addrs, subject, body, body_is_html, attempts, created_at) "
        "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0, datetime('now'))",
        { accountId, serverUrl, d.fromName, d.fromAddr, Join(d.to, '\n'),
          Join(d.cc, '\n'), Join(d.bcc, '\n'), d.subject, d.body, d.bodyIsHtml ? 1 : 0 });
    if (!ins) { UltraDb_Rollback(tx); return ins; }
    outId = ins.lastInsertId;

    for (const auto& a : d.attachments) {
        UltraDbResult ar = UltraDb_ExecInTx(tx,
            "INSERT INTO outbox_attachments(outbox_id, filename, media_type, data) "
            "VALUES(?, ?, ?, ?)",
            { outId, a.filename, a.mediaType, a.data });
        if (!ar) { UltraDb_Rollback(tx); return ar; }
    }
    return UltraDb_Commit(tx);
}

UltraDbResult OutboxStore::LoadAttachments(OutboxItem& item) const {
    UltraDbResultSet rs;
    UltraDbResult q = UltraDb_Query(connection_,
        "SELECT filename, media_type, data FROM outbox_attachments WHERE outbox_id=? ORDER BY id",
        { item.id }, rs);
    if (!q) return q;
    for (const auto& row : rs) {
        Attachment a;
        a.filename = row["filename"].AsString();
        a.mediaType = row["media_type"].AsString();
        a.data = row["data"].AsBlob();
        item.draft.attachments.push_back(std::move(a));
    }
    return UltraDbResult::Ok();
}

UltraDbResult OutboxStore::ListPending(std::vector<OutboxItem>& out) const {
    out.clear();
    UltraDbResultSet rs;
    UltraDbResult q = UltraDb_Query(connection_,
        "SELECT id, account_id, server_url, from_name, from_addr, to_addrs, cc_addrs, "
        "bcc_addrs, subject, body, body_is_html, attempts, last_error "
        "FROM outbox ORDER BY id", rs);
    if (!q) return q;
    for (const auto& row : rs) {
        OutboxItem it;
        it.id        = row["id"].AsInt64();
        it.accountId = row["account_id"].AsString();
        it.serverUrl = row["server_url"].AsString();
        it.attempts  = row["attempts"].AsInt();
        it.lastError = row["last_error"].AsString();
        it.draft.fromName = row["from_name"].AsString();
        it.draft.fromAddr = row["from_addr"].AsString();
        it.draft.to  = Split(row["to_addrs"].AsString(), '\n');
        it.draft.cc  = Split(row["cc_addrs"].AsString(), '\n');
        it.draft.bcc = Split(row["bcc_addrs"].AsString(), '\n');
        it.draft.subject = row["subject"].AsString();
        it.draft.body = row["body"].AsString();
        it.draft.bodyIsHtml = row["body_is_html"].AsInt64() != 0;
        UltraDbResult ar = LoadAttachments(it);
        if (!ar) return ar;
        out.push_back(std::move(it));
    }
    return UltraDbResult::Ok();
}

UltraDbResult OutboxStore::Remove(int64_t id) {
    UltraDbHandle tx = UltraDb_Begin(connection_);
    if (tx == UltraDbInvalidHandle)
        return UltraDbResult::Error(UltraDbResultCode::Internal, "begin failed");
    UltraDb_ExecInTx(tx, "DELETE FROM outbox_attachments WHERE outbox_id=?", { id });
    UltraDb_ExecInTx(tx, "DELETE FROM outbox WHERE id=?", { id });
    return UltraDb_Commit(tx);
}

UltraDbResult OutboxStore::MarkFailed(int64_t id, const std::string& error) {
    return UltraDb_Exec(connection_,
        "UPDATE outbox SET attempts = attempts + 1, last_error = ? WHERE id = ?",
        { error, id });
}

UltraDbResult OutboxStore::PendingCount(int& out) const {
    out = 0;
    UltraDbResultSet rs;
    UltraDbResult q = UltraDb_Query(connection_, "SELECT COUNT(*) AS n FROM outbox", rs);
    if (!q) return q;
    if (!rs.Empty()) out = rs.Row(0)["n"].AsInt();
    return UltraDbResult::Ok();
}

Outbox::FlushStats Outbox::Flush(IMailProtocolPlugin& smtp,
                                 const std::function<std::string(const std::string&)>& credentialFor) {
    FlushStats stats;
    std::vector<OutboxItem> pending;
    if (!store_.ListPending(pending)) return stats;

    MailSender sender(smtp);
    for (const auto& item : pending) {
        UltraNetMailOptions opts;
        opts.credentials.username = item.draft.fromAddr;
        opts.credentials.password = credentialFor ? credentialFor(item.accountId) : std::string();
        opts.useTls = true;

        UltraNetResult r = sender.Send(item.draft, item.serverUrl, opts);
        if (r) { store_.Remove(item.id); stats.sent++; }
        else   { store_.MarkFailed(item.id, r.message); stats.failed++; }
    }
    return stats;
}

} // namespace UltraMail
