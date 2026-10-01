// Apps/UltraMail/engine/UltraMailOutbox.cpp
// Version: 0.4.0 - a Drafts copy until the message is sent (migration 3: the
//                  Message-ID, reply headers and Drafts folder of each message)
// Version: 0.3.0 - keeps an HTML draft's text version and inline pictures
//                  (migration 2)
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailOutbox.h"

#include <UltraDatabase/UltraDatabase.h>
#include <UltraNet/UltraNetMime.h>

#include <chrono>
#include <cstdio>
#include <random>

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

// "<abc@host>" and "abc@host" are the same Message-ID.
std::string BareMessageId(const std::string& id) {
    size_t b = 0, e = id.size();
    while (b < e && (id[b] == ' ' || id[b] == '<' || id[b] == '\t')) ++b;
    while (e > b && (id[e - 1] == ' ' || id[e - 1] == '>' || id[e - 1] == '\t'
                     || id[e - 1] == '\r' || id[e - 1] == '\n')) --e;
    return id.substr(b, e - b);
}

} // namespace

std::string NewMessageId(const std::string& fromAddr) {
    std::string domain = "ultramail.local";
    if (auto at = fromAddr.rfind('@'); at != std::string::npos && at + 1 < fromAddr.size())
        domain = fromAddr.substr(at + 1);
    static std::mt19937_64 random{std::random_device{}()
                                  ^ static_cast<uint64_t>(std::chrono::steady_clock::now()
                                                              .time_since_epoch().count())};
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%llx.%016llx",
                  static_cast<unsigned long long>(now),
                  static_cast<unsigned long long>(random()));
    return "<" + std::string(buffer) + "@" + domain + ">";
}

std::string BuildDraftCopy(const OutboxItem& item) {
    const Draft& d = item.draft;
    UltraNetMimeBuildInput in;
    in.from = d.fromName.empty() ? d.fromAddr : (d.fromName + " <" + d.fromAddr + ">");
    in.to = d.to;
    in.cc = d.cc;
    // Bcc is never written into a message's headers (the builder keeps it for
    // delivery only); the outbox keeps it for the send.
    in.subject = d.subject;
    in.body = d.body;
    in.bodyMediaType = d.bodyIsHtml ? "text/html" : "text/plain";
    if (d.bodyIsHtml) in.alternativeText = d.textBody;
    for (const auto& p : d.inlineParts) {
        UltraNetMimeBuildAttachment a;
        a.filename = p.filename;
        if (!p.mediaType.empty()) a.mediaType = p.mediaType;
        a.data = p.data;
        a.isInline = true;
        a.contentId = p.contentId;
        in.attachments.push_back(std::move(a));
    }
    for (const auto& att : d.attachments) {
        UltraNetMimeBuildAttachment a;
        a.filename = att.filename;
        if (!att.mediaType.empty()) a.mediaType = att.mediaType;
        a.data = att.data;
        in.attachments.push_back(std::move(a));
    }
    in.messageId = item.messageId;
    if (!d.inReplyTo.empty())  in.extraHeaders["In-Reply-To"] = d.inReplyTo;
    if (!d.references.empty()) in.extraHeaders["References"] = d.references;
    in.extraHeaders["X-UltraMail-Outbox"] = "waiting to be sent";
    return UltraNet_MimeBuild(in);
}

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
        // An HTML message's plain-text version, and the pictures its cid:
        // links show (stored with the attachments, marked inline).
        { 2, "html alternative and inline parts",
          "ALTER TABLE outbox ADD COLUMN text_body TEXT;"
          "ALTER TABLE outbox_attachments ADD COLUMN content_id TEXT;"
          "ALTER TABLE outbox_attachments ADD COLUMN is_inline INTEGER DEFAULT 0;" },
        // The copy kept in the Drafts folder until the message is sent - its
        // Message-ID and folder - and the reply headers of an answer.
        { 3, "drafts copy and reply headers",
          "ALTER TABLE outbox ADD COLUMN message_id TEXT DEFAULT '';"
          "ALTER TABLE outbox ADD COLUMN drafts_folder TEXT DEFAULT '';"
          "ALTER TABLE outbox ADD COLUMN in_reply_to TEXT DEFAULT '';"
          "ALTER TABLE outbox ADD COLUMN refs TEXT DEFAULT '';" },
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
        "cc_addrs, bcc_addrs, subject, body, body_is_html, text_body, message_id, "
        "in_reply_to, refs, attempts, created_at) "
        "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0, datetime('now'))",
        { accountId, serverUrl, d.fromName, d.fromAddr, Join(d.to, '\n'),
          Join(d.cc, '\n'), Join(d.bcc, '\n'), d.subject, d.body, d.bodyIsHtml ? 1 : 0,
          d.textBody, NewMessageId(d.fromAddr), d.inReplyTo, d.references });
    if (!ins) { UltraDb_Rollback(tx); return ins; }
    outId = ins.lastInsertId;

    auto insertPart = [&](const Attachment& a, bool isInline) {
        return UltraDb_ExecInTx(tx,
            "INSERT INTO outbox_attachments(outbox_id, filename, media_type, data, content_id, is_inline) "
            "VALUES(?, ?, ?, ?, ?, ?)",
            { outId, a.filename, a.mediaType, a.data, a.contentId, isInline ? 1 : 0 });
    };
    for (const auto& a : d.attachments) {
        UltraDbResult ar = insertPart(a, false);
        if (!ar) { UltraDb_Rollback(tx); return ar; }
    }
    for (const auto& p : d.inlineParts) {
        UltraDbResult ar = insertPart(p, true);
        if (!ar) { UltraDb_Rollback(tx); return ar; }
    }
    return UltraDb_Commit(tx);
}

UltraDbResult OutboxStore::LoadAttachments(OutboxItem& item) const {
    UltraDbResultSet rs;
    UltraDbResult q = UltraDb_Query(connection_,
        "SELECT filename, media_type, data, content_id, is_inline FROM outbox_attachments "
        "WHERE outbox_id=? ORDER BY id",
        { item.id }, rs);
    if (!q) return q;
    for (const auto& row : rs) {
        Attachment a;
        a.filename = row["filename"].AsString();
        a.mediaType = row["media_type"].AsString();
        a.data = row["data"].AsBlob();
        a.contentId = row["content_id"].AsString();
        a.isInline = row["is_inline"].AsInt64() != 0;
        (a.isInline ? item.draft.inlineParts : item.draft.attachments).push_back(std::move(a));
    }
    return UltraDbResult::Ok();
}

UltraDbResult OutboxStore::ListPending(std::vector<OutboxItem>& out) const {
    out.clear();
    UltraDbResultSet rs;
    UltraDbResult q = UltraDb_Query(connection_,
        "SELECT id, account_id, server_url, from_name, from_addr, to_addrs, cc_addrs, "
        "bcc_addrs, subject, body, body_is_html, text_body, attempts, last_error, "
        "message_id, drafts_folder, in_reply_to, refs "
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
        it.draft.textBody = row["text_body"].AsString();
        it.draft.inReplyTo  = row["in_reply_to"].AsString();
        it.draft.references = row["refs"].AsString();
        it.messageId    = row["message_id"].AsString();
        it.draftsFolder = row["drafts_folder"].AsString();
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

UltraDbResult OutboxStore::SetMessageId(int64_t id, const std::string& messageId) {
    return UltraDb_Exec(connection_, "UPDATE outbox SET message_id = ? WHERE id = ?",
                        { messageId, id });
}

UltraDbResult OutboxStore::MarkDraftSaved(int64_t id, const std::string& folder) {
    return UltraDb_Exec(connection_, "UPDATE outbox SET drafts_folder = ? WHERE id = ?",
                        { folder, id });
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
    return Flush(smtp, [&credentialFor](const std::string& accountId, UltraNetMailOptions& o) {
        o.credentials.type     = UltraNetAuthType::Basic;
        o.credentials.password = credentialFor ? credentialFor(accountId) : std::string();
        return UltraNetResult::Ok();
    });
}

bool Outbox::SaveDraftCopy(OutboxItem& item, const DraftsKeeper& drafts, FlushStats& stats) {
    if (item.HasDraftCopy()) return true;
    if (!drafts.imap || !drafts.prepare) return false;
    // A message queued before migration 3 has no Message-ID yet: give it one,
    // or its copy could never be found again to delete.
    if (item.messageId.empty()) {
        item.messageId = NewMessageId(item.draft.fromAddr);
        store_.SetMessageId(item.id, item.messageId);
    }
    std::string serverUrl, folder;
    UltraNetMailOptions opts;
    UltraNetResult r = drafts.prepare(item.accountId, serverUrl, folder, opts);
    if (r && folder.empty())
        r = UltraNetResult::Error(UltraNetResultCode::InvalidState, "no Drafts folder is known");
    if (r) r = drafts.imap->AppendMessage(serverUrl, folder, BuildDraftCopy(item),
                                          UltraNetMailFlags::Draft | UltraNetMailFlags::Seen, opts);
    if (r) r = store_.MarkDraftSaved(item.id, folder)
                   ? UltraNetResult::Ok()
                   : UltraNetResult::Error(UltraNetResultCode::InvalidState,
                                           "the outbox could not record the Drafts copy");
    if (!r) {
        stats.draftFailures++;
        stats.lastDraftFailure = r;
        return false;
    }
    item.draftsFolder = folder;
    stats.draftsSaved++;
    return true;
}

void Outbox::RemoveDraftCopy(const OutboxItem& item, const DraftsKeeper& drafts, FlushStats& stats) {
    if (!item.HasDraftCopy() || !drafts.imap || !drafts.prepare) return;
    std::string serverUrl, folder;
    UltraNetMailOptions opts;
    UltraNetResult r = drafts.prepare(item.accountId, serverUrl, folder, opts);
    std::vector<UltraNetMailEnvelope> envelopes;
    if (r) r = drafts.imap->FetchEnvelopes(serverUrl, item.draftsFolder, 0, envelopes, opts);
    if (!r) {
        stats.draftFailures++;
        stats.lastDraftFailure = r;
        return;
    }
    const std::string wanted = BareMessageId(item.messageId);
    for (const auto& e : envelopes) {
        if (BareMessageId(e.messageId) != wanted) continue;
        UltraNetResult del = drafts.imap->StoreFlags(serverUrl, item.draftsFolder, e.uid,
                                                     UltraNetMailFlags::Deleted, true, opts);
        if (!del) {
            stats.draftFailures++;
            stats.lastDraftFailure = del;
        }
    }
}

Outbox::FlushStats Outbox::SaveDraftCopies(const DraftsKeeper& drafts) {
    FlushStats stats;
    std::vector<OutboxItem> pending;
    if (!store_.ListPending(pending)) return stats;
    for (auto& item : pending) SaveDraftCopy(item, drafts, stats);
    return stats;
}

Outbox::FlushStats Outbox::Flush(IMailProtocolPlugin& smtp, const OptionsResolver& prepare,
                                 const DraftsKeeper* drafts) {
    FlushStats stats;
    std::vector<OutboxItem> pending;
    if (!store_.ListPending(pending)) return stats;
    MailSender sender(smtp);
    for (auto& item : pending) {
        // The Drafts copy first: if the send fails, the message is in Drafts.
        if (drafts) SaveDraftCopy(item, *drafts, stats);
        UltraNetMailOptions opts;
        opts.useTls = true;   // the resolver may relax this for a plaintext server
        UltraNetResult r = prepare ? prepare(item.accountId, opts) : UltraNetResult::Ok();
        if (opts.credentials.username.empty()) opts.credentials.username = item.draft.fromAddr;
        // The resolver may name the account's server as it is now; the URL
        // stored with the message is what was known when it was queued.
        const std::string serverUrl = opts.serverUrl.empty() ? item.serverUrl : opts.serverUrl;
        if (r) r = sender.Send(item.draft, serverUrl, opts);
        if (r) {
            if (drafts) RemoveDraftCopy(item, *drafts, stats);
            store_.Remove(item.id);
            stats.sent++;
        } else {
            store_.MarkFailed(item.id, r.message);
            stats.failed++;
            stats.lastFailure = r;   // carried out so the UI can say why
        }
    }
    return stats;
}

} // namespace UltraMail
