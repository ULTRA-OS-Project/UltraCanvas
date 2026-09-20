// UltraCanvas/core/UltraMessage/UltraMessageJournal.cpp
// The journal on UltraDatabase: one SQLite file per user, migrated through
// UltraDb_Migrate, parameter binding throughout. Text search is a LIKE over
// the extracted text column in this phase; an FTS5 index is a later step so
// the journal does not depend on how the system sqlite3 was compiled.
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraMessageJournal.h"

#include <UltraDatabase/UltraDatabase.h>

#include <fstream>
#include <map>

namespace UltraMessage {
namespace Internal {

using UltraCanvas::JSON::Serialize;
using UltraCanvas::JSONSerializeOptions;

namespace {

constexpr int kDefaultRetentionDays = 90;
constexpr int64_t kDefaultRetentionRows = 50000;

const std::vector<UltraDbMigration>& Migrations() {
    static const std::vector<UltraDbMigration> steps = {
        {1, "journal-v1",
         "CREATE TABLE IF NOT EXISTS messages ("
         "  id TEXT PRIMARY KEY,"
         "  topic TEXT NOT NULL,"
         "  kind TEXT NOT NULL,"
         "  app_id TEXT,"
         "  instance_id TEXT,"
         "  sender_name TEXT,"
         "  sender_pid INTEGER,"
         "  sender_verified INTEGER DEFAULT 0,"
         "  target TEXT,"
         "  conversation TEXT,"
         "  service TEXT,"
         "  time_ms INTEGER NOT NULL,"
         "  ttl_s INTEGER DEFAULT 0,"
         "  flags INTEGER DEFAULT 0,"
         "  replaces TEXT,"
         "  body_json TEXT NOT NULL,"
         "  text TEXT,"
         "  read INTEGER DEFAULT 0,"
         "  dismissed INTEGER DEFAULT 0"
         ");"
         "CREATE INDEX IF NOT EXISTS idx_messages_time ON messages(time_ms);"
         "CREATE INDEX IF NOT EXISTS idx_messages_topic ON messages(topic, time_ms);"
         "CREATE INDEX IF NOT EXISTS idx_messages_conversation ON messages(conversation, time_ms);"
         "CREATE TABLE IF NOT EXISTS attachments ("
         "  message_id TEXT NOT NULL,"
         "  name TEXT,"
         "  mime TEXT,"
         "  size INTEGER,"
         "  path TEXT,"
         "  bytes BLOB"
         ");"
         "CREATE INDEX IF NOT EXISTS idx_attachments_message ON attachments(message_id);"
         "CREATE TABLE IF NOT EXISTS conversations ("
         "  id TEXT PRIMARY KEY,"
         "  service TEXT,"
         "  title TEXT,"
         "  is_group INTEGER DEFAULT 0,"
         "  last_time_ms INTEGER DEFAULT 0,"
         "  last_message_id TEXT"
         ");"
         "CREATE TABLE IF NOT EXISTS retention ("
         "  pattern TEXT PRIMARY KEY,"
         "  days INTEGER DEFAULT 0,"
         "  max_rows INTEGER DEFAULT 0"
         ");"},
    };
    return steps;
}

UltraMsgResult DbError(const UltraDbResult& r, const char* what) {
    return UltraMsgResult::Error(UltraMsgResultCode::JournalError,
                                 std::string(what) + ": " + r.message);
}

std::string EscapeLike(const std::string& text) {
    std::string out;
    for (char c : text) {
        if (c == '%' || c == '_' || c == '\\') out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

std::string Placeholders(size_t count) {
    std::string out;
    for (size_t i = 0; i < count; ++i) out += i ? ",?" : "?";
    return out;
}

} // namespace

std::string ExtractSearchText(const std::string& topic, const JSONValue& body) {
    if (!body.IsObject()) return std::string();
    std::string text;
    auto append = [&](const char* key) {
        if (const JSONValue* v = body.Find(key); v && v->IsString()) {
            if (!text.empty()) text.push_back('\n');
            text += v->GetString();
        }
    };
    if (topic == UltraMsgTopics::MailMessage) {
        append("subject");
        append("snippet");
        if (const JSONValue* from = body.Find("from"); from && from->IsObject()) {
            if (const JSONValue* name = from->Find("name"); name && name->IsString()) {
                text.push_back('\n');
                text += name->GetString();
            }
            if (const JSONValue* address = from->Find("address"); address && address->IsString()) {
                text.push_back('\n');
                text += address->GetString();
            }
        }
    } else if (topic == UltraMsgTopics::SystemNotification) {
        append("appName");
        append("summary");
        append("body");
    } else {
        append("text");
        if (const JSONValue* sender = body.Find("sender"); sender && sender->IsObject()) {
            if (const JSONValue* name = sender->Find("name"); name && name->IsString()) {
                text.push_back('\n');
                text += name->GetString();
            }
        }
        if (text.empty()) {
            append("title");
            append("summary");
            append("subject");
            append("body");
        }
    }
    return text;
}

std::string ExtractService(const JSONValue& body) {
    if (!body.IsObject()) return std::string();
    if (const JSONValue* service = body.Find("service"); service && service->IsString())
        return service->GetString();
    return std::string();
}

Journal::~Journal() { Close(); }

UltraMsgResult Journal::Open(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!connection_.empty()) return UltraMsgResult::Ok();
    if (path != ":memory:" && !EnsureParentDirectory(path))
        return UltraMsgResult::Error(UltraMsgResultCode::JournalError,
                                     "cannot create the journal directory for " + path);
    UltraDbConnectionConfig config;
    config.name = "ultramessage.journal." + GenerateToken(8);
    config.driver = "sqlite";
    config.database = path;
    UltraDbResult r = UltraDb_RegisterConnection(config);
    if (!r) return DbError(r, "open journal");
    r = UltraDb_Migrate(config.name, Migrations());
    if (!r) {
        UltraDb_CloseConnection(config.name);
        return DbError(r, "migrate journal");
    }
    UltraDb_Exec(config.name, "PRAGMA journal_mode=WAL");
    connection_ = config.name;
    path_ = path;
    return UltraMsgResult::Ok();
}

void Journal::Close() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (connection_.empty()) return;
    UltraDb_CloseConnection(connection_);
    connection_.clear();
    path_.clear();
}

UltraMsgResult Journal::Store(const UltraMsgMessage& m) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (connection_.empty()) return UltraMsgResult::Error(UltraMsgResultCode::JournalError, "journal closed");
    const UltraMsgEnvelope& e = m.envelope;
    UltraDbHandle tx = UltraDb_Begin(connection_);
    if (tx == UltraDbInvalidHandle)
        return UltraMsgResult::Error(UltraMsgResultCode::JournalError, "cannot begin a transaction");

    if ((e.flags & UltraMsgFlag_Replace) && !e.replaces.empty()) {
        UltraDb_ExecInTx(tx, "DELETE FROM attachments WHERE message_id = ?", {e.replaces});
        UltraDb_ExecInTx(tx, "DELETE FROM messages WHERE id = ?", {e.replaces});
    }

    JSONSerializeOptions compact;
    compact.pretty = false;
    const std::string bodyJson = Serialize(m.body, compact);
    const std::string service = ExtractService(m.body);
    UltraDbResult r = UltraDb_ExecInTx(
        tx,
        "INSERT OR IGNORE INTO messages(id, topic, kind, app_id, instance_id, sender_name, sender_pid,"
        " sender_verified, target, conversation, service, time_ms, ttl_s, flags, replaces, body_json,"
        " text, read, dismissed) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
        {e.id, e.topic, std::string(UltraMsg_KindName(e.kind)), e.from.appId, e.from.instanceId,
         e.from.displayName, static_cast<int64_t>(e.from.processId), e.from.verified, e.to,
         e.conversation, service, e.timestampMs, static_cast<int64_t>(e.ttlSeconds),
         static_cast<int64_t>(e.flags), e.replaces, bodyJson, ExtractSearchText(e.topic, m.body),
         m.read, m.dismissed});
    if (!r) {
        UltraDb_Rollback(tx);
        return DbError(r, "store message");
    }
    if (r.affectedRows > 0) {
        for (const auto& a : m.attachments) {
            r = UltraDb_ExecInTx(tx,
                                 "INSERT INTO attachments(message_id, name, mime, size, path, bytes)"
                                 " VALUES (?,?,?,?,?,?)",
                                 {e.id, a.name, a.mimeType, static_cast<int64_t>(a.bytes.size()),
                                  a.filePath, a.bytes.empty() ? UltraDbValue::Null() : UltraDbValue::Blob(a.bytes)});
            if (!r) {
                UltraDb_Rollback(tx);
                return DbError(r, "store attachment");
            }
        }
        if (!e.conversation.empty()) {
            std::string title;
            bool isGroup = false;
            if (m.body.IsObject()) {
                if (const JSONValue* c = m.body.Find("conversation"); c && c->IsObject()) {
                    title = c->Get("title").GetString();
                    isGroup = c->Get("isGroup").GetBoolean(false);
                }
            }
            r = UltraDb_ExecInTx(tx,
                                 "INSERT INTO conversations(id, service, title, is_group, last_time_ms, last_message_id)"
                                 " VALUES (?,?,?,?,?,?)"
                                 " ON CONFLICT(id) DO UPDATE SET"
                                 "  service = CASE WHEN excluded.service <> '' THEN excluded.service ELSE service END,"
                                 "  title = CASE WHEN excluded.title <> '' THEN excluded.title ELSE title END,"
                                 "  is_group = excluded.is_group,"
                                 "  last_time_ms = CASE WHEN excluded.last_time_ms >= last_time_ms THEN excluded.last_time_ms ELSE last_time_ms END,"
                                 "  last_message_id = CASE WHEN excluded.last_time_ms >= last_time_ms THEN excluded.last_message_id ELSE last_message_id END",
                                 {e.conversation, service, title, isGroup, e.timestampMs, e.id});
            if (!r) {
                UltraDb_Rollback(tx);
                return DbError(r, "store conversation");
            }
        }
    }
    r = UltraDb_Commit(tx);
    if (!r) return DbError(r, "commit");
    return UltraMsgResult::Ok();
}

Journal::Clause Journal::BuildWhere(const UltraMsgQuery& q, std::vector<UltraDbValue>& params) const {
    Clause clause;
    std::string& sql = clause.sql;
    sql = " WHERE 1=1";
    if (!q.topics.empty()) {
        std::string topicSql;
        for (const auto& pattern : q.topics) {
            if (!IsValidPattern(pattern)) continue;
            if (!topicSql.empty()) topicSql += " OR ";
            if (pattern.find('*') == std::string::npos && pattern.find('#') == std::string::npos) {
                topicSql += "topic = ?";
                params.emplace_back(pattern);
            } else {
                const std::string prefix = PatternLiteralPrefix(pattern);
                topicSql += "topic LIKE ? ESCAPE '\\'";
                params.emplace_back(prefix.empty() ? std::string("%") : EscapeLike(prefix) + "%");
                clause.topicPatterns.push_back(pattern);
            }
        }
        if (!topicSql.empty()) sql += " AND (" + topicSql + ")";
    }
    if (!q.conversation.empty()) {
        sql += " AND conversation = ?";
        params.emplace_back(q.conversation);
    }
    if (!q.appId.empty()) {
        sql += " AND app_id = ?";
        params.emplace_back(q.appId);
    }
    if (!q.service.empty()) {
        sql += " AND service = ?";
        params.emplace_back(q.service);
    }
    if (q.sinceMs > 0) {
        sql += " AND time_ms >= ?";
        params.emplace_back(q.sinceMs);
    }
    if (q.untilMs > 0) {
        sql += " AND time_ms <= ?";
        params.emplace_back(q.untilMs);
    }
    if (q.unreadOnly) sql += " AND read = 0";
    if (!q.includeDismissed) sql += " AND dismissed = 0";
    if (!q.textContains.empty()) {
        sql += " AND text LIKE ? ESCAPE '\\'";
        params.emplace_back("%" + EscapeLike(q.textContains) + "%");
    }
    return clause;
}

UltraMsgResult Journal::ReadRows(const std::string& sql, const std::vector<UltraDbValue>& params,
                                 const std::vector<std::string>& patterns,
                                 std::vector<UltraMsgMessage>& out) {
    UltraDbResultSet rows;
    UltraDbResult r = UltraDb_Query(connection_, sql, params, rows);
    if (!r) return DbError(r, "query journal");
    for (const UltraDbRow& row : rows) {
        UltraMsgMessage m;
        UltraMsgEnvelope& e = m.envelope;
        e.id = row["id"].AsString();
        e.topic = row["topic"].AsString();
        if (!patterns.empty()) {
            bool matched = false;
            for (const auto& p : patterns) if (TopicMatches(p, e.topic)) { matched = true; break; }
            if (!matched) continue;
        }
        UltraMsg_KindFromName(row["kind"].AsString(), e.kind);
        e.from.appId = row["app_id"].AsString();
        e.from.instanceId = row["instance_id"].AsString();
        e.from.displayName = row["sender_name"].AsString();
        e.from.processId = row["sender_pid"].AsInt();
        e.from.verified = row["sender_verified"].AsBool();
        e.to = row["target"].AsString();
        e.conversation = row["conversation"].AsString();
        e.timestampMs = row["time_ms"].AsInt64();
        e.ttlSeconds = row["ttl_s"].AsInt();
        e.flags = static_cast<uint32_t>(row["flags"].AsInt64());
        e.replaces = row["replaces"].AsString();
        UltraCanvas::JSONParseResult parsed;
        m.body = UltraCanvas::JSON::Parse(row["body_json"].AsString(), &parsed);
        if (!parsed.success) m.body = JSONValue::MakeObject();
        m.read = row["read"].AsBool();
        m.dismissed = row["dismissed"].AsBool();

        UltraDbResultSet attachmentRows;
        if (UltraDb_Query(connection_,
                          "SELECT name, mime, size, path, bytes FROM attachments WHERE message_id = ?",
                          {e.id}, attachmentRows)) {
            for (const UltraDbRow& a : attachmentRows) {
                UltraMsgAttachment attachment;
                attachment.name = a["name"].AsString();
                attachment.mimeType = a["mime"].AsString();
                attachment.filePath = a["path"].AsString();
                if (!a["bytes"].IsNull()) attachment.bytes = a["bytes"].AsBlob();
                m.attachments.push_back(std::move(attachment));
            }
        }
        out.push_back(std::move(m));
    }
    return UltraMsgResult::Ok();
}

UltraMsgResult Journal::Query(const UltraMsgQuery& query, std::vector<UltraMsgMessage>& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    out.clear();
    if (connection_.empty()) return UltraMsgResult::Error(UltraMsgResultCode::JournalError, "journal closed");
    std::vector<UltraDbValue> params;
    const Clause where = BuildWhere(query, params);
    std::string sql = "SELECT * FROM messages" + where.sql + " ORDER BY time_ms DESC, id DESC";
    const int limit = query.limit > 0 ? query.limit : 100;
    sql += " LIMIT ? OFFSET ?";
    params.emplace_back(static_cast<int64_t>(limit));
    params.emplace_back(static_cast<int64_t>(query.offset > 0 ? query.offset : 0));
    return ReadRows(sql, params, where.topicPatterns, out);
}

UltraMsgResult Journal::Count(const UltraMsgQuery& query, int64_t& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    out = 0;
    if (connection_.empty()) return UltraMsgResult::Error(UltraMsgResultCode::JournalError, "journal closed");
    std::vector<UltraDbValue> params;
    const Clause where = BuildWhere(query, params);
    if (!where.topicPatterns.empty()) {
        // Wildcard patterns need the exact matcher; count by reading topics.
        UltraDbResultSet rows;
        UltraDbResult r = UltraDb_Query(connection_, "SELECT topic FROM messages" + where.sql, params, rows);
        if (!r) return DbError(r, "count journal");
        for (const UltraDbRow& row : rows) {
            const std::string topic = row["topic"].AsString();
            for (const auto& p : where.topicPatterns) if (TopicMatches(p, topic)) { ++out; break; }
        }
        return UltraMsgResult::Ok();
    }
    UltraDbResultSet rows;
    UltraDbResult r = UltraDb_Query(connection_, "SELECT COUNT(*) AS n FROM messages" + where.sql, params, rows);
    if (!r) return DbError(r, "count journal");
    if (!rows.Empty()) out = rows.Row(0)["n"].AsInt64();
    return UltraMsgResult::Ok();
}

UltraMsgResult Journal::Get(const std::string& id, UltraMsgMessage& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (connection_.empty()) return UltraMsgResult::Error(UltraMsgResultCode::JournalError, "journal closed");
    std::vector<UltraMsgMessage> rows;
    UltraMsgResult r = ReadRows("SELECT * FROM messages WHERE id = ?", {id}, {}, rows);
    if (!r) return r;
    if (rows.empty()) return UltraMsgResult::Error(UltraMsgResultCode::NoSuchTarget, "no journaled message " + id);
    out = std::move(rows.front());
    return UltraMsgResult::Ok();
}

UltraMsgResult Journal::SetRead(const std::vector<std::string>& ids, bool read) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (connection_.empty()) return UltraMsgResult::Error(UltraMsgResultCode::JournalError, "journal closed");
    if (ids.empty()) return UltraMsgResult::Ok();
    std::vector<UltraDbValue> params;
    params.emplace_back(read);
    for (const auto& id : ids) params.emplace_back(id);
    UltraDbResult r = UltraDb_Exec(connection_,
                                   "UPDATE messages SET read = ? WHERE id IN (" + Placeholders(ids.size()) + ")",
                                   params);
    if (!r) return DbError(r, "mark read");
    return UltraMsgResult::Ok();
}

UltraMsgResult Journal::Dismiss(const std::vector<std::string>& ids) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (connection_.empty()) return UltraMsgResult::Error(UltraMsgResultCode::JournalError, "journal closed");
    if (ids.empty()) return UltraMsgResult::Ok();
    std::vector<UltraDbValue> params;
    for (const auto& id : ids) params.emplace_back(id);
    UltraDbResult r = UltraDb_Exec(connection_,
                                   "UPDATE messages SET dismissed = 1, read = 1 WHERE id IN (" +
                                       Placeholders(ids.size()) + ")",
                                   params);
    if (!r) return DbError(r, "dismiss");
    return UltraMsgResult::Ok();
}

UltraMsgResult Journal::Delete(const std::vector<std::string>& ids) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (connection_.empty()) return UltraMsgResult::Error(UltraMsgResultCode::JournalError, "journal closed");
    if (ids.empty()) return UltraMsgResult::Ok();
    std::vector<UltraDbValue> params;
    for (const auto& id : ids) params.emplace_back(id);
    const std::string in = "(" + Placeholders(ids.size()) + ")";
    UltraDbResult r = UltraDb_Exec(connection_, "DELETE FROM attachments WHERE message_id IN " + in, params);
    if (!r) return DbError(r, "delete attachments");
    r = UltraDb_Exec(connection_, "DELETE FROM messages WHERE id IN " + in, params);
    if (!r) return DbError(r, "delete messages");
    return UltraMsgResult::Ok();
}

UltraMsgResult Journal::ListConversations(const UltraMsgQuery& query,
                                          std::vector<UltraMsgConversation>& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    out.clear();
    if (connection_.empty()) return UltraMsgResult::Error(UltraMsgResultCode::JournalError, "journal closed");
    std::string sql =
        "SELECT c.id AS id, c.service AS service, c.title AS title, c.is_group AS is_group,"
        " c.last_time_ms AS last_time_ms, c.last_message_id AS last_message_id,"
        " (SELECT COUNT(*) FROM messages m WHERE m.conversation = c.id AND m.dismissed = 0) AS total,"
        " (SELECT COUNT(*) FROM messages m WHERE m.conversation = c.id AND m.read = 0 AND m.dismissed = 0) AS unread"
        " FROM conversations c WHERE 1=1";
    std::vector<UltraDbValue> params;
    if (!query.service.empty()) {
        sql += " AND c.service = ?";
        params.emplace_back(query.service);
    }
    if (!query.conversation.empty()) {
        sql += " AND c.id = ?";
        params.emplace_back(query.conversation);
    }
    if (query.sinceMs > 0) {
        sql += " AND c.last_time_ms >= ?";
        params.emplace_back(query.sinceMs);
    }
    sql += " ORDER BY c.last_time_ms DESC LIMIT ? OFFSET ?";
    params.emplace_back(static_cast<int64_t>(query.limit > 0 ? query.limit : 100));
    params.emplace_back(static_cast<int64_t>(query.offset > 0 ? query.offset : 0));
    UltraDbResultSet rows;
    UltraDbResult r = UltraDb_Query(connection_, sql, params, rows);
    if (!r) return DbError(r, "list conversations");
    for (const UltraDbRow& row : rows) {
        UltraMsgConversation c;
        c.id = row["id"].AsString();
        c.service = row["service"].AsString();
        c.title = row["title"].AsString();
        c.isGroup = row["is_group"].AsBool();
        c.lastTimeMs = row["last_time_ms"].AsInt64();
        c.lastMessageId = row["last_message_id"].AsString();
        c.messageCount = row["total"].AsInt();
        c.unreadCount = row["unread"].AsInt();
        if (query.unreadOnly && c.unreadCount == 0) continue;
        out.push_back(std::move(c));
    }
    return UltraMsgResult::Ok();
}

UltraMsgResult Journal::SetRetention(const std::string& pattern, int days, int64_t maxRows) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (connection_.empty()) return UltraMsgResult::Error(UltraMsgResultCode::JournalError, "journal closed");
    if (!IsValidPattern(pattern))
        return UltraMsgResult::Error(UltraMsgResultCode::InvalidArgument, "invalid pattern: " + pattern);
    UltraDbResult r = UltraDb_Exec(connection_,
                                   "INSERT INTO retention(pattern, days, max_rows) VALUES (?,?,?)"
                                   " ON CONFLICT(pattern) DO UPDATE SET days = excluded.days, max_rows = excluded.max_rows",
                                   {pattern, static_cast<int64_t>(days), maxRows});
    if (!r) return DbError(r, "set retention");
    return UltraMsgResult::Ok();
}

UltraMsgResult Journal::ApplyRetention() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (connection_.empty()) return UltraMsgResult::Error(UltraMsgResultCode::JournalError, "journal closed");
    struct Rule { std::string pattern; int days; int64_t maxRows; };
    std::vector<Rule> rules;
    UltraDbResultSet rows;
    if (UltraDb_Query(connection_, "SELECT pattern, days, max_rows FROM retention", rows)) {
        for (const UltraDbRow& row : rows)
            rules.push_back({row["pattern"].AsString(), row["days"].AsInt(), row["max_rows"].AsInt64()});
    }
    bool hasCatchAll = false;
    for (const auto& rule : rules) if (rule.pattern == "#") hasCatchAll = true;
    if (!hasCatchAll) rules.push_back({"#", kDefaultRetentionDays, kDefaultRetentionRows});

    const int64_t now = NowMs();
    for (const auto& rule : rules) {
        const std::string prefix = PatternLiteralPrefix(rule.pattern);
        const std::string like = prefix.empty() ? std::string("%") : EscapeLike(prefix) + "%";
        if (rule.days > 0) {
            const int64_t cutoff = now - static_cast<int64_t>(rule.days) * 86400000LL;
            UltraDb_Exec(connection_,
                         "DELETE FROM attachments WHERE message_id IN"
                         " (SELECT id FROM messages WHERE topic LIKE ? ESCAPE '\\' AND time_ms < ?)",
                         {like, cutoff});
            UltraDb_Exec(connection_, "DELETE FROM messages WHERE topic LIKE ? ESCAPE '\\' AND time_ms < ?",
                         {like, cutoff});
        }
        if (rule.maxRows > 0) {
            UltraDb_Exec(connection_,
                         "DELETE FROM attachments WHERE message_id IN"
                         " (SELECT id FROM messages WHERE topic LIKE ? ESCAPE '\\'"
                         "  ORDER BY time_ms DESC, id DESC LIMIT -1 OFFSET ?)",
                         {like, rule.maxRows});
            UltraDb_Exec(connection_,
                         "DELETE FROM messages WHERE id IN"
                         " (SELECT id FROM messages WHERE topic LIKE ? ESCAPE '\\'"
                         "  ORDER BY time_ms DESC, id DESC LIMIT -1 OFFSET ?)",
                         {like, rule.maxRows});
        }
    }
    UltraDb_Exec(connection_,
                 "DELETE FROM conversations WHERE id NOT IN (SELECT DISTINCT conversation FROM messages"
                 " WHERE conversation IS NOT NULL AND conversation <> '')");
    return UltraMsgResult::Ok();
}

UltraMsgResult Journal::Export(const UltraMsgQuery& query, const std::string& path, int64_t& outCount) {
    outCount = 0;
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return UltraMsgResult::Error(UltraMsgResultCode::JournalError, "cannot write " + path);
    UltraMsgQuery page = query;
    page.limit = 500;
    page.offset = query.offset > 0 ? query.offset : 0;
    JSONSerializeOptions compact;
    compact.pretty = false;
    const int64_t wanted = query.limit > 0 ? query.limit : -1;
    for (;;) {
        std::vector<UltraMsgMessage> messages;
        UltraMsgResult r = Query(page, messages);
        if (!r) return r;
        if (messages.empty()) break;
        for (const auto& m : messages) {
            if (wanted >= 0 && outCount >= wanted) return UltraMsgResult::Ok();
            file << Serialize(MessageToJson(m), compact) << '\n';
            ++outCount;
        }
        if (static_cast<int>(messages.size()) < page.limit) break;
        page.offset += page.limit;
    }
    return UltraMsgResult::Ok();
}

} // namespace Internal
} // namespace UltraMessage
