// Plugins/UltraIM/direct/DirectStore.cpp
// ChatCube Direct - the device's own record (SQLite via UltraDatabase)
// Version: 0.1.2 - RemoveMessage, IsReadReported, NoteDeleted / IsDeleted; schema 4: the deleted table
// Version: 0.1.1 - Search; schema 3: the links table (message ids only) and messages by type
// Version: 0.1.0
// Last Modified: 2026-11-11
// Author: UltraCanvas Framework

#include "DirectStore.h"

#include "DataFormats/UltraCanvasJSON.h"

#include "UltraDatabase/UltraDatabase.h"
#include "UltraIM/UltraIMCore.h"

#include <algorithm>
#include <sstream>

namespace UltraIMDirect {

namespace {

const std::vector<UltraDbMigration>& Migrations() {
    static const std::vector<UltraDbMigration> steps = {
        {1, "chatcube direct schema",
         "CREATE TABLE settings(key TEXT PRIMARY KEY, value TEXT NOT NULL);"
         "CREATE TABLE contacts("
         "  user_id TEXT PRIMARY KEY, display_name TEXT NOT NULL, public_key TEXT NOT NULL,"
         "  presence INTEGER NOT NULL DEFAULT 0);"
         "CREATE TABLE conversations("
         "  id TEXT PRIMARY KEY, title TEXT NOT NULL, is_group INTEGER NOT NULL,"
         "  direct_contact TEXT NOT NULL DEFAULT '', members TEXT NOT NULL DEFAULT '',"
         "  last_ts INTEGER NOT NULL DEFAULT 0, snippet TEXT NOT NULL DEFAULT '',"
         "  unread INTEGER NOT NULL DEFAULT 0);"
         "CREATE TABLE messages("
         "  seq INTEGER PRIMARY KEY AUTOINCREMENT, conv TEXT NOT NULL, client_id TEXT NOT NULL,"
         "  sender TEXT NOT NULL, sender_name TEXT NOT NULL DEFAULT '', ts INTEGER NOT NULL,"
         "  type INTEGER NOT NULL, text TEXT NOT NULL DEFAULT '', reply_to TEXT NOT NULL DEFAULT '',"
         "  status INTEGER NOT NULL, own INTEGER NOT NULL, read_reported INTEGER NOT NULL DEFAULT 0,"
         "  UNIQUE(conv, client_id));"
         "CREATE INDEX messages_by_conv ON messages(conv, seq);"
         "CREATE TABLE outbox("
         "  client_id TEXT NOT NULL, recipient TEXT NOT NULL, conv TEXT NOT NULL,"
         "  body TEXT NOT NULL, ts INTEGER NOT NULL, attempts INTEGER NOT NULL DEFAULT 0,"
         "  PRIMARY KEY(client_id, recipient));"},
        {2, "attachments and the parts of files on their way in",
         "ALTER TABLE messages ADD COLUMN attachment TEXT NOT NULL DEFAULT '';"
         "ALTER TABLE messages ADD COLUMN file_id TEXT NOT NULL DEFAULT '';"
         "CREATE INDEX messages_by_file ON messages(file_id);"
         "CREATE TABLE parts("
         "  file_id TEXT NOT NULL, seq INTEGER NOT NULL, data TEXT NOT NULL,"
         "  PRIMARY KEY(file_id, seq));"},
        // Which messages have a link in them: the message's seq and nothing
        // else - the link itself is read out of the text when it is shown
        // (UltraIM_ExtractLinks), so no address is stored twice. Filled here
        // for the history already there, generously (any "www."): a search
        // checks each candidate's text anyway.
        {3, "the messages with a link, and messages by type",
         "CREATE TABLE links(msg_seq INTEGER PRIMARY KEY);"
         "INSERT INTO links(msg_seq) SELECT seq FROM messages"
         "  WHERE text LIKE '%http://%' OR text LIKE '%https://%' OR text LIKE '%www.%';"
         "CREATE INDEX messages_by_type ON messages(conv, type, seq);"},
        // What was deleted before it was all here - a file with parts still
        // on their way, a message its "delete" overtook: dropped when it
        // does arrive instead of appearing after all, or waiting for ever.
        {4, "what was deleted while it was arriving",
         "CREATE TABLE deleted(key TEXT PRIMARY KEY, ts INTEGER NOT NULL);"},
    };
    return steps;
}

// The attachment as one JSON text in the message's row.
std::string AttachmentToJson(const UltraIMAttachment& a) {
    if (a.remoteUrl.empty() && a.localPath.empty() && a.fileName.empty()) return std::string();
    UltraCanvas::JSONValue o = UltraCanvas::JSONValue::MakeObject();
    o.Set("local", UltraCanvas::JSONValue(a.localPath));
    o.Set("thumb", UltraCanvas::JSONValue(a.thumbnailPath));
    o.Set("file", UltraCanvas::JSONValue(a.remoteUrl));
    o.Set("name", UltraCanvas::JSONValue(a.fileName));
    o.Set("mime", UltraCanvas::JSONValue(a.mimeType));
    o.Set("size", UltraCanvas::JSONValue(a.fileSizeBytes));
    o.Set("w", UltraCanvas::JSONValue(a.width));
    o.Set("h", UltraCanvas::JSONValue(a.height));
    o.Set("ms", UltraCanvas::JSONValue(static_cast<int64_t>(a.durationSeconds * 1000.0)));
    o.Set("here", UltraCanvas::JSONValue(a.isDownloaded));
    return UltraCanvas::JSON::Serialize(o);
}

UltraIMAttachment AttachmentFromJson(const std::string& text) {
    UltraIMAttachment a;
    if (text.empty()) return a;
    UltraCanvas::JSONParseResult parsed;
    const UltraCanvas::JSONValue o = UltraCanvas::JSON::Parse(text, &parsed);
    if (!parsed.success || !o.IsObject()) return a;
    a.localPath = o["local"].GetString();
    a.thumbnailPath = o["thumb"].GetString();
    a.remoteUrl = o["file"].GetString();
    a.fileName = o["name"].GetString();
    a.mimeType = o["mime"].GetString();
    a.fileSizeBytes = o["size"].GetInteger(0);
    a.width = static_cast<int>(o["w"].GetInteger(0));
    a.height = static_cast<int>(o["h"].GetInteger(0));
    a.durationSeconds = static_cast<double>(o["ms"].GetInteger(0)) / 1000.0;
    a.isDownloaded = o["here"].GetBoolean(false);
    return a;
}

std::string JoinIds(const std::vector<std::string>& ids) {
    std::string out;
    for (const auto& id : ids) {
        if (!out.empty()) out += ',';
        out += id;
    }
    return out;
}

std::vector<std::string> SplitIds(const std::string& text) {
    std::vector<std::string> out;
    std::stringstream stream(text);
    std::string item;
    while (std::getline(stream, item, ',')) {
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

UltraIMConversation ConversationFromRow(const UltraDbRow& r, const std::string& providerId) {
    UltraIMConversation c;
    c.conversationId = r["id"].AsString();
    c.providerId = providerId;
    c.title = r["title"].AsString();
    c.isGroup = r["is_group"].AsInt() != 0;
    c.directContactId = r["direct_contact"].AsString();
    c.memberIds = SplitIds(r["members"].AsString());
    c.lastMessageTimeMs = r["last_ts"].AsInt64();
    c.lastMessageSnippet = r["snippet"].AsString();
    c.unreadCount = r["unread"].AsInt();
    return c;
}

UltraIMMessage MessageFromRow(const UltraDbRow& r, const std::string& providerId) {
    UltraIMMessage m;
    m.providerId = providerId;
    m.conversationId = r["conv"].AsString();
    m.clientMessageId = r["client_id"].AsString();
    m.messageId = m.clientMessageId;
    m.senderId = r["sender"].AsString();
    m.senderName = r["sender_name"].AsString();
    m.timestampMs = r["ts"].AsInt64();
    m.type = static_cast<UltraIMMessageType>(r["type"].AsInt());
    m.text = r["text"].AsString();
    m.replyTo.messageId = r["reply_to"].AsString();
    m.deliveryStatus = static_cast<UltraIMDeliveryStatus>(r["status"].AsInt());
    m.isOwn = r["own"].AsInt() != 0;
    m.attachment = AttachmentFromJson(r["attachment"].AsString());
    return m;
}

std::string Snippet(const UltraIMMessage& m) {
    std::string text = m.text;
    const std::string named = m.text.empty() ? m.attachment.fileName : m.text;
    switch (m.type) {
        case UltraIMMessageType::Image: text = "[photo] " + m.text; break;
        case UltraIMMessageType::Video: text = "[video] " + named; break;
        case UltraIMMessageType::File:  text = "[file] " + named; break;
        case UltraIMMessageType::Audio: text = "[audio] " + named; break;
        case UltraIMMessageType::Sticker: text = "[sticker]"; break;
        default: break;
    }
    std::string s = text.substr(0, 80);
    std::replace(s.begin(), s.end(), '\n', ' ');
    return s;
}

} // namespace

Store::~Store() {
    Close();
}

bool Store::Open(const std::string& path, const std::string& providerId, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    providerId_ = providerId;
    connection_ = "chatcube-direct-" + UltraIM_NewClientMessageId();
    UltraDbConnectionConfig config;
    config.name = connection_;
    config.driver = "sqlite";
    config.database = path;
    UltraDbResult registered = UltraDb_RegisterConnection(config);
    if (!registered) {
        error = registered.message;
        connection_.clear();
        return false;
    }
    if (path != ":memory:") {
        UltraDb_Exec(connection_, "PRAGMA journal_mode=WAL");
        UltraDb_Exec(connection_, "PRAGMA synchronous=NORMAL");
    }
    UltraDbResult migrated = UltraDb_Migrate(connection_, Migrations());
    if (!migrated) {
        error = migrated.message;
        UltraDb_CloseConnection(connection_);
        connection_.clear();
        return false;
    }
    return true;
}

void Store::Close() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (connection_.empty()) return;
    UltraDb_CloseConnection(connection_);
    connection_.clear();
}

// ---- settings ----------------------------------------------------------------

std::string Store::GetSetting(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    UltraDbResultSet rows;
    if (!UltraDb_Query(connection_, "SELECT value FROM settings WHERE key=?", {key}, rows) || rows.Empty())
        return std::string();
    return rows.Row(0)["value"].AsString();
}

void Store::SetSetting(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    UltraDb_Exec(connection_, "INSERT INTO settings(key, value) VALUES(?,?) "
                              "ON CONFLICT(key) DO UPDATE SET value=excluded.value", {key, value});
}

// ---- contacts ----------------------------------------------------------------

void Store::UpsertContact(const StoredContact& c) {
    std::lock_guard<std::mutex> lock(mutex_);
    UltraDb_Exec(connection_,
                 "INSERT INTO contacts(user_id, display_name, public_key, presence) VALUES(?,?,?,?) "
                 "ON CONFLICT(user_id) DO UPDATE SET display_name=excluded.display_name, "
                 "public_key=excluded.public_key, presence=excluded.presence",
                 {c.userId, c.displayName, c.publicKey, static_cast<int>(c.presence)});
}

bool Store::GetContact(const std::string& userId, StoredContact& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    UltraDbResultSet rows;
    if (!UltraDb_Query(connection_, "SELECT user_id, display_name, public_key, presence FROM contacts WHERE user_id=?",
                       {userId}, rows) || rows.Empty())
        return false;
    const auto& r = rows.Row(0);
    out.userId = r["user_id"].AsString();
    out.displayName = r["display_name"].AsString();
    out.publicKey = r["public_key"].AsString();
    out.presence = static_cast<UltraIMPresence>(r["presence"].AsInt());
    return true;
}

std::vector<StoredContact> Store::ListContacts() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<StoredContact> out;
    UltraDbResultSet rows;
    if (!UltraDb_Query(connection_, "SELECT user_id, display_name, public_key, presence FROM contacts "
                                    "ORDER BY display_name", rows))
        return out;
    for (const auto& r : rows) {
        StoredContact c;
        c.userId = r["user_id"].AsString();
        c.displayName = r["display_name"].AsString();
        c.publicKey = r["public_key"].AsString();
        c.presence = static_cast<UltraIMPresence>(r["presence"].AsInt());
        out.push_back(std::move(c));
    }
    return out;
}

// ---- conversations -----------------------------------------------------------

void Store::UpsertConversation(const UltraIMConversation& c) {
    std::lock_guard<std::mutex> lock(mutex_);
    UltraDb_Exec(connection_,
                 "INSERT INTO conversations(id, title, is_group, direct_contact, members, last_ts, snippet, unread) "
                 "VALUES(?,?,?,?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET title=excluded.title, "
                 "is_group=excluded.is_group, direct_contact=excluded.direct_contact, members=excluded.members",
                 {c.conversationId, c.title, c.isGroup ? 1 : 0, c.directContactId, JoinIds(c.memberIds),
                  c.lastMessageTimeMs, c.lastMessageSnippet, c.unreadCount});
}

bool Store::GetConversation(const std::string& id, UltraIMConversation& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    UltraDbResultSet rows;
    if (!UltraDb_Query(connection_, "SELECT * FROM conversations WHERE id=?", {id}, rows) || rows.Empty())
        return false;
    out = ConversationFromRow(rows.Row(0), providerId_);
    return true;
}

std::vector<UltraIMConversation> Store::ListConversations() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<UltraIMConversation> out;
    UltraDbResultSet rows;
    if (!UltraDb_Query(connection_, "SELECT * FROM conversations ORDER BY last_ts DESC", rows)) return out;
    for (const auto& r : rows) out.push_back(ConversationFromRow(r, providerId_));
    return out;
}

bool Store::RemoveConversation(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    UltraDbResult r = UltraDb_Exec(connection_, "DELETE FROM conversations WHERE id=?", {id});
    UltraDb_Exec(connection_, "DELETE FROM links WHERE msg_seq IN (SELECT seq FROM messages WHERE conv=?)", {id});
    UltraDb_Exec(connection_, "DELETE FROM messages WHERE conv=?", {id});
    UltraDb_Exec(connection_, "DELETE FROM outbox WHERE conv=?", {id});
    return r && r.affectedRows > 0;
}

void Store::ClearUnread(const std::string& conversationId) {
    std::lock_guard<std::mutex> lock(mutex_);
    UltraDb_Exec(connection_, "UPDATE conversations SET unread=0 WHERE id=?", {conversationId});
}

// ---- messages ----------------------------------------------------------------

bool Store::AddMessage(const UltraIMMessage& m) {
    std::lock_guard<std::mutex> lock(mutex_);
    UltraDbResult r = UltraDb_Exec(
        connection_,
        "INSERT OR IGNORE INTO messages(conv, client_id, sender, sender_name, ts, type, text, reply_to, status, own, "
        "attachment, file_id) VALUES(?,?,?,?,?,?,?,?,?,?,?,?)",
        {m.conversationId, m.clientMessageId, m.senderId, m.senderName, m.timestampMs, static_cast<int>(m.type),
         m.text, m.replyTo.messageId, static_cast<int>(m.deliveryStatus), m.isOwn ? 1 : 0,
         AttachmentToJson(m.attachment), m.attachment.remoteUrl});
    if (!r || r.affectedRows == 0) return false;
    if (!UltraIM_ExtractLinks(m.text).empty()) {
        UltraDb_Exec(connection_,
                     "INSERT OR IGNORE INTO links(msg_seq) SELECT seq FROM messages WHERE conv=? AND client_id=?",
                     {m.conversationId, m.clientMessageId});
    }
    UltraDb_Exec(connection_,
                 "UPDATE conversations SET last_ts=MAX(last_ts, ?), snippet=?, unread=unread+? WHERE id=?",
                 {m.timestampMs, Snippet(m), m.isOwn ? 0 : 1, m.conversationId});
    return true;
}

bool Store::UpdateStatus(const std::string& conversationId, const std::string& clientId,
                         UltraIMDeliveryStatus status) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Never downgrade: a late "sent" must not undo "read", and only a message
    // still sending can fail; a failed one may still go through later.
    const int failed = static_cast<int>(UltraIMDeliveryStatus::Failed);
    const int sending = static_cast<int>(UltraIMDeliveryStatus::Sending);
    const int value = static_cast<int>(status);
    UltraDbResult r = UltraDb_Exec(
        connection_,
        "UPDATE messages SET status=? WHERE conv=? AND client_id=? AND "
        "((? <> ? AND (status < ? OR status = ?)) OR (? = ? AND status = ?))",
        {value, conversationId, clientId, value, failed, value, failed, value, failed, sending});
    return r && r.affectedRows > 0;
}

bool Store::UpdateAttachment(const std::string& conversationId, const std::string& clientId,
                             const UltraIMAttachment& attachment) {
    std::lock_guard<std::mutex> lock(mutex_);
    UltraDbResult r = UltraDb_Exec(connection_, "UPDATE messages SET attachment=?, file_id=? WHERE conv=? AND client_id=?",
                                   {AttachmentToJson(attachment), attachment.remoteUrl, conversationId, clientId});
    return r && r.affectedRows > 0;
}

bool Store::RemoveMessage(const std::string& conversationId, const std::string& clientId, UltraIMMessage& removed) {
    std::lock_guard<std::mutex> lock(mutex_);
    UltraDbResultSet rows;
    if (!UltraDb_Query(connection_, "SELECT * FROM messages WHERE conv=? AND client_id=?", {conversationId, clientId},
                       rows) ||
        rows.Empty())
        return false;
    const UltraDbRow& row = *rows.begin();
    removed = MessageFromRow(row, providerId_);
    const int64_t seq = row["seq"].AsInt64();
    const bool unread = row["own"].AsInt() == 0 && row["read_reported"].AsInt() == 0;
    const std::string fileId = row["file_id"].AsString();

    UltraDb_Exec(connection_, "DELETE FROM links WHERE msg_seq=?", {seq});
    UltraDb_Exec(connection_, "DELETE FROM messages WHERE seq=?", {seq});
    // What of it has not left yet stays here.
    UltraDb_Exec(connection_, "DELETE FROM outbox WHERE client_id=?", {clientId});
    if (!fileId.empty()) {
        const std::string partIds = "c-" + fileId + "-";
        UltraDb_Exec(connection_, "DELETE FROM outbox WHERE substr(client_id, 1, ?)=?",
                     {static_cast<int64_t>(partIds.size()), partIds});
        UltraDb_Exec(connection_, "DELETE FROM parts WHERE file_id=?", {fileId});
        UltraDb_Exec(connection_, "INSERT OR IGNORE INTO deleted(key, ts) VALUES(?, ?)",
                     {DeletedFileKey(fileId), removed.timestampMs});
    }
    // The conversation's last line is the message before it now.
    UltraDbResultSet last;
    std::string snippet;
    if (UltraDb_Query(connection_, "SELECT * FROM messages WHERE conv=? ORDER BY seq DESC LIMIT 1", {conversationId},
                      last) &&
        !last.Empty())
        snippet = Snippet(MessageFromRow(*last.begin(), providerId_));
    UltraDb_Exec(connection_, "UPDATE conversations SET snippet=?, unread=MAX(0, unread-?) WHERE id=?",
                 {snippet, unread ? 1 : 0, conversationId});
    return true;
}

bool Store::IsReadReported(const std::string& conversationId, const std::string& clientId) {
    std::lock_guard<std::mutex> lock(mutex_);
    UltraDbResultSet rows;
    if (!UltraDb_Query(connection_, "SELECT read_reported FROM messages WHERE conv=? AND client_id=?",
                       {conversationId, clientId}, rows) ||
        rows.Empty())
        return false;
    return (*rows.begin())["read_reported"].AsInt() != 0;
}

void Store::NoteDeleted(const std::string& key, int64_t timestampMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    UltraDb_Exec(connection_, "INSERT OR IGNORE INTO deleted(key, ts) VALUES(?, ?)", {key, timestampMs});
}

bool Store::IsDeleted(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    UltraDbResultSet rows;
    return UltraDb_Query(connection_, "SELECT 1 FROM deleted WHERE key=?", {key}, rows) && !rows.Empty();
}

bool Store::FindMessageByFile(const std::string& fileId, UltraIMMessage& out) {
    if (fileId.empty()) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    UltraDbResultSet rows;
    if (!UltraDb_Query(connection_, "SELECT * FROM messages WHERE file_id=? ORDER BY seq DESC LIMIT 1", {fileId}, rows) ||
        rows.Empty())
        return false;
    out = MessageFromRow(rows.Row(0), providerId_);
    return true;
}

void Store::AddPart(const std::string& fileId, int seq, const std::string& data) {
    std::lock_guard<std::mutex> lock(mutex_);
    UltraDb_Exec(connection_, "INSERT OR IGNORE INTO parts(file_id, seq, data) VALUES(?,?,?)", {fileId, seq, data});
}

int Store::CountParts(const std::string& fileId) {
    std::lock_guard<std::mutex> lock(mutex_);
    UltraDbResultSet rows;
    if (!UltraDb_Query(connection_, "SELECT COUNT(*) AS n FROM parts WHERE file_id=?", {fileId}, rows) || rows.Empty())
        return 0;
    return rows.Row(0)["n"].AsInt();
}

std::vector<std::string> Store::Parts(const std::string& fileId) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> out;
    UltraDbResultSet rows;
    if (!UltraDb_Query(connection_, "SELECT data FROM parts WHERE file_id=? ORDER BY seq", {fileId}, rows)) return out;
    for (const auto& r : rows) out.push_back(r["data"].AsString());
    return out;
}

void Store::RemoveParts(const std::string& fileId) {
    std::lock_guard<std::mutex> lock(mutex_);
    UltraDb_Exec(connection_, "DELETE FROM parts WHERE file_id=?", {fileId});
}

bool Store::FindMessage(const std::string& conversationId, const std::string& clientId, UltraIMMessage& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    UltraDbResultSet rows;
    if (!UltraDb_Query(connection_, "SELECT * FROM messages WHERE conv=? AND client_id=?",
                       {conversationId, clientId}, rows) || rows.Empty())
        return false;
    out = MessageFromRow(rows.Row(0), providerId_);
    return true;
}

std::vector<UltraIMMessage> Store::Messages(const std::string& conversationId,
                                            const std::string& beforeClientId, int limit) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<UltraIMMessage> out;
    if (limit <= 0) limit = 50;
    UltraDbResultSet rows;
    bool ok;
    if (beforeClientId.empty()) {
        ok = static_cast<bool>(UltraDb_Query(connection_,
                                             "SELECT * FROM messages WHERE conv=? ORDER BY seq DESC LIMIT ?",
                                             {conversationId, limit}, rows));
    } else {
        ok = static_cast<bool>(UltraDb_Query(
            connection_,
            "SELECT * FROM messages WHERE conv=? AND seq < "
            "(SELECT seq FROM messages WHERE conv=? AND client_id=?) ORDER BY seq DESC LIMIT ?",
            {conversationId, conversationId, beforeClientId, limit}, rows));
    }
    if (!ok) return out;
    for (const auto& r : rows) out.push_back(MessageFromRow(r, providerId_));
    std::reverse(out.begin(), out.end());
    return out;
}

UltraIMSearchPage Store::Search(const UltraIMMessageSearch& search) {
    std::lock_guard<std::mutex> lock(mutex_);
    UltraIMSearchPage page;
    const int limit = search.limit > 0 ? search.limit : 50;
    // The database narrows by conversation and kind (an index each); the
    // words are matched here, where upper and lower case are known for more
    // than ASCII, which is all SQLite's LIKE folds.
    std::string from = "messages m";
    std::string where;
    using T = UltraIMMessageType;
    const auto type = [](T t) { return std::to_string(static_cast<int>(t)); };
    switch (search.kind) {
        case UltraIMSearchKind::AnyKind: where = "m.type <> " + type(T::SystemEvent); break;
        case UltraIMSearchKind::Images:  where = "m.type = " + type(T::Image); break;
        case UltraIMSearchKind::Videos:  where = "m.type = " + type(T::Video); break;
        case UltraIMSearchKind::Media:   where = "m.type IN (" + type(T::Image) + "," + type(T::Video) + ")"; break;
        case UltraIMSearchKind::Files:   where = "m.type = " + type(T::File); break;
        case UltraIMSearchKind::Links:
            from = "links l JOIN messages m ON m.seq = l.msg_seq";
            where = "m.type <> " + type(T::SystemEvent);
            break;
    }
    if (!search.conversationId.empty()) where += " AND m.conv = ?";
    const std::string sql = "SELECT m.* FROM " + from + " WHERE " + where + " AND m.seq < ? ORDER BY m.seq DESC LIMIT ?";

    int64_t before = INT64_MAX;
    if (!search.cursor.empty()) {
        before = 0;
        for (char c : search.cursor) {
            if (c < '0' || c > '9' || before > (INT64_MAX - 9) / 10) return page;   // not one of ours: nothing
            before = before * 10 + (c - '0');
        }
    }
    const int batch = 200;
    for (;;) {
        UltraDbResultSet rows;
        std::vector<UltraDbValue> values;
        if (!search.conversationId.empty()) values.emplace_back(search.conversationId);
        values.emplace_back(before);
        values.emplace_back(batch);
        if (!UltraDb_Query(connection_, sql, values, rows)) return page;
        int read = 0;
        for (const auto& r : rows) {
            ++read;
            before = r["seq"].AsInt64();
            UltraIMMessage m = MessageFromRow(r, providerId_);
            if (!UltraIM_MessageMatchesSearch(m, search)) continue;
            page.messages.push_back(std::move(m));
            if (static_cast<int>(page.messages.size()) >= limit) {
                page.nextCursor = std::to_string(before);
                return page;
            }
        }
        if (read < batch) return page;      // the beginning of the history
    }
}

std::vector<std::string> Store::UnreadIncoming(const std::string& conversationId) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> out;
    UltraDbResultSet rows;
    if (!UltraDb_Query(connection_, "SELECT client_id FROM messages WHERE conv=? AND own=0 AND read_reported=0",
                       {conversationId}, rows))
        return out;
    for (const auto& r : rows) out.push_back(r["client_id"].AsString());
    UltraDb_Exec(connection_, "UPDATE messages SET read_reported=1 WHERE conv=? AND own=0", {conversationId});
    return out;
}

// ---- outbox ------------------------------------------------------------------

void Store::AddOutbox(const OutboxEntry& e) {
    std::lock_guard<std::mutex> lock(mutex_);
    UltraDb_Exec(connection_,
                 "INSERT OR REPLACE INTO outbox(client_id, recipient, conv, body, ts, attempts) VALUES(?,?,?,?,?,?)",
                 {e.clientId, e.recipient, e.conversation, e.body, e.timestampMs, e.attempts});
}

void Store::RemoveOutbox(const std::string& clientId, const std::string& recipient) {
    std::lock_guard<std::mutex> lock(mutex_);
    UltraDb_Exec(connection_, "DELETE FROM outbox WHERE client_id=? AND recipient=?", {clientId, recipient});
}

std::vector<OutboxEntry> Store::Outbox(const std::string& recipient) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<OutboxEntry> out;
    UltraDbResultSet rows;
    bool ok = recipient.empty()
                  ? static_cast<bool>(UltraDb_Query(connection_, "SELECT * FROM outbox ORDER BY ts", rows))
                  : static_cast<bool>(UltraDb_Query(connection_, "SELECT * FROM outbox WHERE recipient=? ORDER BY ts",
                                                    {recipient}, rows));
    if (!ok) return out;
    for (const auto& r : rows) {
        OutboxEntry e;
        e.clientId = r["client_id"].AsString();
        e.recipient = r["recipient"].AsString();
        e.conversation = r["conv"].AsString();
        e.body = r["body"].AsString();
        e.timestampMs = r["ts"].AsInt64();
        e.attempts = r["attempts"].AsInt();
        out.push_back(std::move(e));
    }
    return out;
}

} // namespace UltraIMDirect
