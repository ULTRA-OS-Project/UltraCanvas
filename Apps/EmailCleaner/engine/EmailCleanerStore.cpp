// Apps/EmailCleaner/engine/EmailCleanerStore.cpp
// Schema, writes and the aggregate queries behind the map view, the timetable
// and the timeline.
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#include "EmailCleanerStore.h"

#include <UltraDatabase/UltraDatabase.h>

#include <algorithm>
#include <cctype>
#include <map>
#include <utility>

namespace EmailCleaner {

namespace {

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// SQLite's LIKE is case-insensitive for ASCII, but the columns hold arbitrary
// UTF-8; matching a lowercased needle against a lowercased column keeps the
// behaviour predictable. Escape the wildcards so a search for "50%" works.
std::string LikePattern(const std::string& needle) {
    std::string escaped;
    for (char c : needle) {
        if (c == '%' || c == '_' || c == '\\') escaped.push_back('\\');
        escaped.push_back(c);
    }
    return "%" + Lower(escaped) + "%";
}

// The unwanted categories as a SQL IN list, built from the taxonomy rather
// than written out — a new unwanted category then cannot silently miss one of
// the queries that counts them.
std::string UnwantedCategorySqlList() {
    std::string out = "(";
    bool first = true;
    for (MessageCategory c : AllCategories()) {
        if (!IsUnwanted(c)) continue;
        if (!first) out += ",";
        out += "'" + ToString(c) + "'";
        first = false;
    }
    return out + ")";
}

const char* kMessageColumns =
    "account_id, folder, uid, message_id, sender_name, sender_addr, sender_domain, "
    "subject, date, size_bytes, flags, automated, attachment_count, "
    "attachment_bytes, category, score, unsub_mailto, unsub_mailto_subject, "
    "unsub_url, unsub_one_click, blocked, overridden, base_category, base_score";

AnalyzedMessage RowToMessage(const UltraDbRow& row) {
    AnalyzedMessage m;
    m.accountId       = row["account_id"].AsString();
    m.folder          = row["folder"].AsString();
    m.uid             = row["uid"].AsInt64();
    m.messageId       = row["message_id"].AsString();
    m.senderName      = row["sender_name"].AsString();
    m.senderAddr      = row["sender_addr"].AsString();
    m.senderDomain    = row["sender_domain"].AsString();
    m.subject         = row["subject"].AsString();
    m.date            = row["date"].AsInt64();
    m.sizeBytes       = row["size_bytes"].AsInt64();
    m.flags           = row["flags"].AsU32();
    m.automated       = row["automated"].AsInt64() != 0;
    m.attachmentCount = row["attachment_count"].AsInt();
    m.attachmentBytes = row["attachment_bytes"].AsInt64();
    m.category        = CategoryFromString(row["category"].AsString());
    m.score           = row["score"].AsDouble();
    m.unsubMailto        = row["unsub_mailto"].AsString();
    m.unsubMailtoSubject = row["unsub_mailto_subject"].AsString();
    m.unsubUrl           = row["unsub_url"].AsString();
    m.unsubOneClick      = row["unsub_one_click"].AsInt64() != 0;
    m.blocked            = row["blocked"].AsInt64() != 0;
    m.overridden         = row["overridden"].AsInt64() != 0;
    m.baseCategory       = CategoryFromString(row["base_category"].AsString());
    m.baseScore          = row["base_score"].AsDouble();
    return m;
}

// ORDER BY expression for a sender/domain rollup metric.
const char* MetricOrder(SenderMetric metric) {
    switch (metric) {
        case SenderMetric::TotalBytes:      return "total_bytes DESC";
        case SenderMetric::AttachmentBytes: return "attachment_bytes DESC";
        case SenderMetric::UnwantedCount:   return "unwanted_count DESC, message_count DESC";
        case SenderMetric::MessageCount:    break;
    }
    return "message_count DESC";
}

} // namespace

// ---- Open / schema ---------------------------------------------------------

UltraDbResult AnalysisStore::Open(const std::string& connectionName,
                                  const std::string& databasePath) {
    // Registering a name that already exists *replaces* the pooled entry and
    // drops the physical connection — which for ":memory:" would silently
    // throw the database away. So re-registering only happens when the name is
    // new or now points somewhere else.
    bool needsRegistration = true;
    if (UltraDb_HasConnection(connectionName)) {
        UltraDbConnectionInfo info;
        if (UltraDb_GetConnectionInfo(connectionName, info) && info.database == databasePath)
            needsRegistration = false;
    }
    if (needsRegistration) {
        UltraDbConnectionConfig cfg;
        cfg.name     = connectionName;
        cfg.driver   = "sqlite";
        cfg.database = databasePath;
        UltraDbResult reg = UltraDb_RegisterConnection(cfg);
        if (!reg) return reg;
    }

    connection_ = connectionName;

    const std::vector<UltraDbMigration> steps = {
        { 1, "analysis schema",
          "CREATE TABLE accounts("
          "  account_id TEXT PRIMARY KEY,"
          "  display_name TEXT,"
          "  email TEXT,"
          "  short_name TEXT);"

          "CREATE TABLE messages("
          "  account_id TEXT NOT NULL,"
          "  folder TEXT NOT NULL,"
          "  uid INTEGER NOT NULL,"
          "  message_id TEXT,"
          "  sender_name TEXT,"
          "  sender_addr TEXT,"
          "  sender_domain TEXT,"
          "  subject TEXT,"
          "  date INTEGER DEFAULT 0,"
          "  size_bytes INTEGER DEFAULT 0,"
          "  flags INTEGER DEFAULT 0,"
          "  automated INTEGER DEFAULT 0,"
          "  attachment_count INTEGER DEFAULT 0,"
          "  attachment_bytes INTEGER DEFAULT 0,"
          "  category TEXT DEFAULT 'unclassified',"
          "  score REAL DEFAULT 0,"
          "  PRIMARY KEY(account_id, folder, uid));"
          "CREATE INDEX idx_messages_sender ON messages(sender_addr);"
          "CREATE INDEX idx_messages_domain ON messages(sender_domain);"
          "CREATE INDEX idx_messages_date ON messages(date DESC);"
          "CREATE INDEX idx_messages_category ON messages(category);"

          "CREATE TABLE attachments("
          "  attachment_id INTEGER PRIMARY KEY AUTOINCREMENT,"
          "  account_id TEXT NOT NULL,"
          "  folder TEXT NOT NULL,"
          "  uid INTEGER NOT NULL,"
          "  filename TEXT,"
          "  media_type TEXT,"
          "  size_bytes INTEGER DEFAULT 0,"
          "  is_inline INTEGER DEFAULT 0,"
          "  risky INTEGER DEFAULT 0);"
          "CREATE INDEX idx_attachments_msg ON attachments(account_id, folder, uid);"
          "CREATE INDEX idx_attachments_type ON attachments(media_type);"

          "CREATE TABLE keyword_hits("
          "  hit_id INTEGER PRIMARY KEY AUTOINCREMENT,"
          "  account_id TEXT NOT NULL,"
          "  folder TEXT NOT NULL,"
          "  uid INTEGER NOT NULL,"
          "  category TEXT,"
          "  field TEXT,"
          "  term TEXT,"
          "  weight REAL DEFAULT 0);"
          "CREATE INDEX idx_hits_msg ON keyword_hits(account_id, folder, uid);"
          "CREATE INDEX idx_hits_term ON keyword_hits(term);"

          "CREATE TABLE ingest_state("
          "  account_id TEXT NOT NULL,"
          "  folder TEXT NOT NULL,"
          "  last_uid INTEGER DEFAULT 0,"
          "  last_run INTEGER DEFAULT 0,"
          "  messages INTEGER DEFAULT 0,"
          "  PRIMARY KEY(account_id, folder));" },

        { 2, "unsubscribe offers and the blocklist",
          // Phase 2. The unsubscribe columns are per message because a sender
          // can rotate the URL; the rollup query takes the newest non-empty
          // one. `blocked` is a cached stamp of the blocklist so the map can
          // shade a blocked sender without joining on every redraw.
          "ALTER TABLE messages ADD COLUMN unsub_mailto TEXT DEFAULT '';"
          "ALTER TABLE messages ADD COLUMN unsub_mailto_subject TEXT DEFAULT '';"
          "ALTER TABLE messages ADD COLUMN unsub_url TEXT DEFAULT '';"
          "ALTER TABLE messages ADD COLUMN unsub_one_click INTEGER DEFAULT 0;"
          "ALTER TABLE messages ADD COLUMN blocked INTEGER DEFAULT 0;"

          "CREATE TABLE blocklist("
          "  pattern TEXT PRIMARY KEY,"
          "  is_domain INTEGER DEFAULT 0,"
          "  reason TEXT,"
          "  added INTEGER DEFAULT 0);" },

        { 3, "verdict overrides",
          // Phase 3. What the user said about a sender, which beats whatever
          // the classifier decides for their mail from now on. Kept separate
          // from the blocklist because the two answer different questions:
          // the blocklist is "I do not want to hear from them", an override is
          // "your verdict about them is wrong". A sender can be neither, one,
          // or both.
          //
          // `category` is the stored MessageCategory name; an override to a
          // wanted category (Personal, Newsletter, Notification) is the "this
          // is fine" direction, one to an unwanted category the "this is spam"
          // direction.
          //
          // base_category / base_score keep the classifier's own verdict, so
          // category / score can carry the effective one and *removing* an
          // override genuinely undoes it rather than leaving the corrected
          // value behind with nothing to restore from. Backfilled from the
          // existing columns, which up to now were the classifier's verdict.
          // `overridden` is a cached stamp so the map can show what changed
          // without joining per redraw.
          "ALTER TABLE messages ADD COLUMN overridden INTEGER DEFAULT 0;"
          "ALTER TABLE messages ADD COLUMN base_category TEXT DEFAULT '';"
          "ALTER TABLE messages ADD COLUMN base_score REAL DEFAULT 0;"
          "UPDATE messages SET base_category = category, base_score = score;"

          "CREATE TABLE verdict_overrides("
          "  pattern TEXT PRIMARY KEY,"
          "  is_domain INTEGER DEFAULT 0,"
          "  category TEXT NOT NULL,"
          "  reason TEXT,"
          "  added INTEGER DEFAULT 0);" },
    };
    // The list above must end at the version the header advertises.
    if (!steps.empty() && steps.back().version != kSchemaVersion) {
        return UltraDbResult::Error(
            UltraDbResultCode::InvalidArgument,
            "AnalysisStore::kSchemaVersion is " + std::to_string(kSchemaVersion) +
            " but the last migration is " + std::to_string(steps.back().version));
    }
    return UltraDb_Migrate(connection_, steps);
}

// ---- Accounts --------------------------------------------------------------

UltraDbResult AnalysisStore::UpsertAccount(const StoredAccount& account) {
    if (account.accountId.empty())
        return UltraDbResult::Error(UltraDbResultCode::InvalidArgument,
                                    "account id must not be empty");
    return UltraDb_Exec(connection_,
        "INSERT INTO accounts(account_id, display_name, email, short_name) "
        "VALUES(?, ?, ?, ?) "
        "ON CONFLICT(account_id) DO UPDATE SET "
        "  display_name = excluded.display_name,"
        "  email = excluded.email,"
        "  short_name = excluded.short_name",
        { account.accountId, account.displayName, account.email, account.shortName });
}

UltraDbResult AnalysisStore::ListAccounts(std::vector<StoredAccount>& out) const {
    out.clear();
    UltraDbResultSet rs;
    UltraDbResult r = UltraDb_Query(connection_,
        "SELECT account_id, display_name, email, short_name FROM accounts "
        "ORDER BY email", rs);
    if (!r) return r;
    for (const UltraDbRow& row : rs) {
        StoredAccount a;
        a.accountId   = row["account_id"].AsString();
        a.displayName = row["display_name"].AsString();
        a.email       = row["email"].AsString();
        a.shortName   = row["short_name"].AsString();
        out.push_back(std::move(a));
    }
    return r;
}

UltraDbResult AnalysisStore::RemoveAccount(const std::string& accountId) {
    UltraDbResult r = ClearMessages(accountId);
    if (!r) return r;
    r = UltraDb_Exec(connection_, "DELETE FROM ingest_state WHERE account_id = ?", { accountId });
    if (!r) return r;
    return UltraDb_Exec(connection_, "DELETE FROM accounts WHERE account_id = ?", { accountId });
}

// ---- Messages --------------------------------------------------------------

namespace {

// The write half of UpsertMessage. Everything runs inside the caller's
// transaction handle, so a batch commits (or rolls back) as one unit.
UltraDbResult WriteMessage(UltraDbHandle tx, const AnalyzedMessage& m) {
    UltraDbResult r = UltraDb_ExecInTx(tx,
        "INSERT INTO messages(" + std::string(kMessageColumns) + ") "
        "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(account_id, folder, uid) DO UPDATE SET "
        "  message_id = excluded.message_id,"
        "  sender_name = excluded.sender_name,"
        "  sender_addr = excluded.sender_addr,"
        "  sender_domain = excluded.sender_domain,"
        "  subject = excluded.subject,"
        "  date = excluded.date,"
        "  size_bytes = excluded.size_bytes,"
        "  flags = excluded.flags,"
        "  automated = excluded.automated,"
        "  attachment_count = excluded.attachment_count,"
        "  attachment_bytes = excluded.attachment_bytes,"
        "  category = excluded.category,"
        "  score = excluded.score,"
        "  unsub_mailto = excluded.unsub_mailto,"
        "  unsub_mailto_subject = excluded.unsub_mailto_subject,"
        "  unsub_url = excluded.unsub_url,"
        "  unsub_one_click = excluded.unsub_one_click,"
        "  blocked = excluded.blocked,"
        // A fresh classifier verdict resets the effective one; any override is
        // re-stamped afterwards by ApplyOverridesToMessages(), the same way
        // the blocklist stamp is.
        "  overridden = excluded.overridden,"
        "  base_category = excluded.base_category,"
        "  base_score = excluded.base_score",
        { m.accountId, m.folder, m.uid, m.messageId, m.senderName, m.senderAddr,
          m.senderDomain, m.subject, m.date, m.sizeBytes, m.flags,
          m.automated ? 1 : 0, m.attachmentCount, m.attachmentBytes,
          ToString(m.category), m.score, m.unsubMailto, m.unsubMailtoSubject,
          m.unsubUrl, m.unsubOneClick ? 1 : 0, m.blocked ? 1 : 0,
          // Not overridden means the effective verdict is the classifier's, so
          // it is also the base; overridden means the caller kept the base.
          m.overridden ? 1 : 0,
          ToString(m.overridden ? m.baseCategory : m.category),
          m.overridden ? m.baseScore : m.score });
    if (!r) return r;

    // Attachments and hits are derived data: replace them wholesale so a
    // re-analysis with new rules cannot leave stale evidence behind.
    r = UltraDb_ExecInTx(tx,
        "DELETE FROM attachments WHERE account_id = ? AND folder = ? AND uid = ?",
        { m.accountId, m.folder, m.uid });
    if (!r) return r;
    r = UltraDb_ExecInTx(tx,
        "DELETE FROM keyword_hits WHERE account_id = ? AND folder = ? AND uid = ?",
        { m.accountId, m.folder, m.uid });
    if (!r) return r;

    for (const AttachmentRecord& a : m.attachments) {
        r = UltraDb_ExecInTx(tx,
            "INSERT INTO attachments(account_id, folder, uid, filename, media_type, "
            "size_bytes, is_inline, risky) VALUES(?, ?, ?, ?, ?, ?, ?, ?)",
            { m.accountId, m.folder, m.uid, a.filename, a.mediaType, a.sizeBytes,
              a.isInline ? 1 : 0, a.risky ? 1 : 0 });
        if (!r) return r;
    }
    for (const KeywordHit& h : m.hits) {
        r = UltraDb_ExecInTx(tx,
            "INSERT INTO keyword_hits(account_id, folder, uid, category, field, term, weight) "
            "VALUES(?, ?, ?, ?, ?, ?, ?)",
            { m.accountId, m.folder, m.uid, ToString(h.category), ToString(h.field),
              h.term, h.weight });
        if (!r) return r;
    }
    return UltraDbResult::Ok();
}

} // namespace

UltraDbResult AnalysisStore::UpsertMessage(const AnalyzedMessage& message) {
    return UpsertMessages({ message });
}

UltraDbResult AnalysisStore::UpsertMessages(const std::vector<AnalyzedMessage>& messages) {
    if (messages.empty()) return UltraDbResult::Ok();

    UltraDbResult error;
    UltraDbHandle tx = UltraDb_Begin(connection_, &error);
    if (tx == UltraDbInvalidHandle) return error;

    for (const AnalyzedMessage& m : messages) {
        if (m.accountId.empty() || m.folder.empty()) {
            UltraDb_Rollback(tx);
            return UltraDbResult::Error(UltraDbResultCode::InvalidArgument,
                                        "message needs an account and a folder");
        }
        UltraDbResult r = WriteMessage(tx, m);
        if (!r) {
            UltraDb_Rollback(tx);
            return r;
        }
    }
    return UltraDb_Commit(tx);
}

bool AnalysisStore::HasMessage(const std::string& accountId, const std::string& folder,
                               int64_t uid) const {
    UltraDbResultSet rs;
    UltraDbResult r = UltraDb_Query(connection_,
        "SELECT 1 FROM messages WHERE account_id = ? AND folder = ? AND uid = ?",
        { accountId, folder, uid }, rs);
    return r && !rs.Empty();
}

UltraDbResult AnalysisStore::ClearMessages(const std::string& accountId) {
    if (accountId.empty()) {
        UltraDbResult r = UltraDb_Exec(connection_, "DELETE FROM keyword_hits");
        if (!r) return r;
        r = UltraDb_Exec(connection_, "DELETE FROM attachments");
        if (!r) return r;
        return UltraDb_Exec(connection_, "DELETE FROM messages");
    }
    UltraDbResult r = UltraDb_Exec(connection_,
        "DELETE FROM keyword_hits WHERE account_id = ?", { accountId });
    if (!r) return r;
    r = UltraDb_Exec(connection_, "DELETE FROM attachments WHERE account_id = ?", { accountId });
    if (!r) return r;
    return UltraDb_Exec(connection_, "DELETE FROM messages WHERE account_id = ?", { accountId });
}

// ---- Filters ---------------------------------------------------------------

std::string AnalysisStore::BuildWhere(const MessageFilter& filter,
                                      const std::string& alias,
                                      UltraDbParams& params) {
    const std::string prefix = alias.empty() ? "" : alias + ".";
    std::string where;
    auto add = [&where](const std::string& clause) {
        where += where.empty() ? " WHERE " : " AND ";
        where += clause;
    };

    if (!filter.accountId.empty()) {
        add(prefix + "account_id = ?");
        params.push_back(filter.accountId);
    }
    if (!filter.folder.empty()) {
        add(prefix + "folder = ?");
        params.push_back(filter.folder);
    }
    if (!filter.senderAddr.empty()) {
        add(prefix + "sender_addr = ?");
        params.push_back(Lower(filter.senderAddr));
    }
    if (!filter.senderDomain.empty()) {
        add(prefix + "sender_domain = ?");
        params.push_back(Lower(filter.senderDomain));
    }
    if (filter.categorySet) {
        add(prefix + "category = ?");
        params.push_back(ToString(filter.category));
    }
    if (filter.unwantedOnly) {
        std::string clause = prefix + "category IN (";
        bool first = true;
        for (MessageCategory c : AllCategories()) {
            if (!IsUnwanted(c)) continue;
            clause += first ? "?" : ", ?";
            params.push_back(ToString(c));
            first = false;
        }
        clause += ")";
        add(clause);
    }
    if (filter.withAttachmentsOnly)
        add(prefix + "attachment_count > 0");
    if (filter.since > 0) {
        add(prefix + "date >= ?");
        params.push_back(filter.since);
    }
    if (filter.until > 0) {
        add(prefix + "date < ?");
        params.push_back(filter.until);
    }
    if (!filter.search.empty()) {
        add("(LOWER(" + prefix + "subject) LIKE ? ESCAPE '\\' OR LOWER(" + prefix +
            "sender_addr) LIKE ? ESCAPE '\\' OR LOWER(" + prefix +
            "sender_name) LIKE ? ESCAPE '\\')");
        const std::string pattern = LikePattern(filter.search);
        params.push_back(pattern);
        params.push_back(pattern);
        params.push_back(pattern);
    }
    return where;
}

UltraDbResult AnalysisStore::ListMessages(const MessageFilter& filter,
                                          std::vector<AnalyzedMessage>& out) const {
    out.clear();
    UltraDbParams params;
    const std::string where = BuildWhere(filter, "m", params);

    std::string sql = "SELECT " + std::string(kMessageColumns) +
                      " FROM messages m" + where + " ORDER BY m.date DESC, m.uid DESC";
    if (filter.limit > 0) {
        sql += " LIMIT ?";
        params.push_back(filter.limit);
    }

    UltraDbResultSet rs;
    UltraDbResult r = UltraDb_Query(connection_, sql, params, rs);
    if (!r) return r;
    out.reserve(rs.Size());
    for (const UltraDbRow& row : rs) out.push_back(RowToMessage(row));
    return r;
}

UltraDbResult AnalysisStore::GetAttachments(const std::string& accountId,
                                            const std::string& folder, int64_t uid,
                                            std::vector<AttachmentRecord>& out) const {
    out.clear();
    UltraDbResultSet rs;
    UltraDbResult r = UltraDb_Query(connection_,
        "SELECT filename, media_type, size_bytes, is_inline, risky FROM attachments "
        "WHERE account_id = ? AND folder = ? AND uid = ? ORDER BY attachment_id",
        { accountId, folder, uid }, rs);
    if (!r) return r;
    for (const UltraDbRow& row : rs) {
        AttachmentRecord a;
        a.filename  = row["filename"].AsString();
        a.mediaType = row["media_type"].AsString();
        a.sizeBytes = row["size_bytes"].AsInt64();
        a.isInline  = row["is_inline"].AsInt64() != 0;
        a.risky     = row["risky"].AsInt64() != 0;
        out.push_back(std::move(a));
    }
    return r;
}

UltraDbResult AnalysisStore::GetHits(const std::string& accountId, const std::string& folder,
                                     int64_t uid, std::vector<KeywordHit>& out) const {
    out.clear();
    UltraDbResultSet rs;
    UltraDbResult r = UltraDb_Query(connection_,
        "SELECT category, field, term, weight FROM keyword_hits "
        "WHERE account_id = ? AND folder = ? AND uid = ? ORDER BY weight DESC, term",
        { accountId, folder, uid }, rs);
    if (!r) return r;
    for (const UltraDbRow& row : rs) {
        KeywordHit h;
        h.category = CategoryFromString(row["category"].AsString());
        h.field    = MatchFieldFromString(row["field"].AsString());
        h.term     = row["term"].AsString();
        h.weight   = row["weight"].AsDouble();
        out.push_back(std::move(h));
    }
    return r;
}

// ---- Sender / domain rollups ----------------------------------------------

namespace {

// Shared body of ListSenderBlocks / ListDomainBlocks: `keyColumn` is what the
// rollup groups by, and the per-category second pass fills in topCategory.
UltraDbResult RollupBlocks(const std::string& connection,
                           const std::string& keyColumn,
                           const std::string& where,
                           const UltraDbParams& params,
                           SenderMetric metric,
                           int limit,
                           std::vector<SenderBlock>& out) {
    out.clear();

    std::string sql =
        "SELECT " + keyColumn + " AS block_key,"
        "  COUNT(*) AS message_count,"
        "  SUM(size_bytes) AS total_bytes,"
        "  SUM(attachment_count) AS attachment_count,"
        "  SUM(attachment_bytes) AS attachment_bytes,"
        "  MIN(date) AS first_seen,"
        "  MAX(date) AS last_seen,"
        "  AVG(score) AS avg_score,"
        "  SUM(CASE WHEN category IN " + UnwantedCategorySqlList() +
        "      THEN 1 ELSE 0 END) AS unwanted_count "
        "FROM messages m" + where +
        " GROUP BY " + keyColumn +
        " ORDER BY " + MetricOrder(metric);
    UltraDbParams sqlParams = params;
    if (limit > 0) {
        sql += " LIMIT ?";
        sqlParams.push_back(limit);
    }

    UltraDbResultSet rs;
    UltraDbResult r = UltraDb_Query(connection, sql, sqlParams, rs);
    if (!r) return r;

    std::vector<std::string> keys;
    out.reserve(rs.Size());
    for (const UltraDbRow& row : rs) {
        SenderBlock b;
        const std::string key = row["block_key"].AsString();
        b.messageCount    = row["message_count"].AsInt();
        b.totalBytes      = row["total_bytes"].AsInt64();
        b.attachmentCount = row["attachment_count"].AsInt();
        b.attachmentBytes = row["attachment_bytes"].AsInt64();
        b.firstSeen       = row["first_seen"].AsInt64();
        b.lastSeen        = row["last_seen"].AsInt64();
        b.averageScore    = row["avg_score"].AsDouble();
        b.unwantedCount   = row["unwanted_count"].AsInt();
        if (keyColumn == "sender_domain") {
            b.domain     = key;
            b.senderAddr = key;
        } else {
            b.senderAddr = key;
            b.domain     = DomainOf(key);
        }
        keys.push_back(key);
        out.push_back(std::move(b));
    }
    if (out.empty()) return r;

    // Second pass: the dominant category and a display name per block. Doing
    // it as one grouped query keeps this at two round trips regardless of how
    // many blocks came back.
    UltraDbResultSet catRs;
    r = UltraDb_Query(connection,
        "SELECT " + keyColumn + " AS block_key, category, COUNT(*) AS n "
        "FROM messages m" + where + " GROUP BY " + keyColumn + ", category",
        params, catRs);
    if (!r) return r;

    std::map<std::string, std::pair<MessageCategory, int>> top;
    for (const UltraDbRow& row : catRs) {
        const std::string key = row["block_key"].AsString();
        const MessageCategory category = CategoryFromString(row["category"].AsString());
        const int n = row["n"].AsInt();
        auto it = top.find(key);
        // Ties go to the unwanted category: a sender that sends as much spam
        // as anything else should read as a spam block on the map.
        if (it == top.end() || n > it->second.second ||
            (n == it->second.second && IsUnwanted(category) && !IsUnwanted(it->second.first))) {
            top[key] = { category, n };
        }
    }

    UltraDbResultSet nameRs;
    std::map<std::string, std::string> names;
    if (keyColumn == "sender_addr") {
        r = UltraDb_Query(connection,
            "SELECT sender_addr, sender_name, COUNT(*) AS n FROM messages m" + where +
            " AND sender_name <> ''"
            " GROUP BY sender_addr, sender_name ORDER BY n DESC",
            params, nameRs);
        if (r) {
            for (const UltraDbRow& row : nameRs) {
                const std::string key = row["sender_addr"].AsString();
                if (names.find(key) == names.end())
                    names[key] = row["sender_name"].AsString();
            }
        }
    }

    for (size_t i = 0; i < out.size(); ++i) {
        auto it = top.find(keys[i]);
        if (it != top.end()) out[i].topCategory = it->second.first;
        auto nameIt = names.find(keys[i]);
        out[i].displayName = (nameIt != names.end() && !nameIt->second.empty())
                           ? nameIt->second : keys[i];
    }
    return UltraDbResult::Ok();
}

} // namespace

UltraDbResult AnalysisStore::ListSenderBlocks(const MessageFilter& filter,
                                              SenderMetric metric, int limit,
                                              std::vector<SenderBlock>& out) const {
    UltraDbParams params;
    std::string where = BuildWhere(filter, "m", params);
    // The display-name pass appends "AND sender_name <> ''", so it needs a
    // WHERE to append to.
    if (where.empty()) where = " WHERE 1 = 1";
    return RollupBlocks(connection_, "sender_addr", where, params, metric, limit, out);
}

UltraDbResult AnalysisStore::ListDomainBlocks(const MessageFilter& filter,
                                              SenderMetric metric, int limit,
                                              std::vector<SenderBlock>& out) const {
    UltraDbParams params;
    std::string where = BuildWhere(filter, "m", params);
    if (where.empty()) where = " WHERE 1 = 1";
    return RollupBlocks(connection_, "sender_domain", where, params, metric, limit, out);
}

// ---- Timetable and timeline -----------------------------------------------

UltraDbResult AnalysisStore::GetTimetable(const MessageFilter& filter,
                                          Timetable& out) const {
    out = Timetable{};
    UltraDbParams params;
    const std::string where = BuildWhere(filter, "m", params);
    const std::string dated = where.empty() ? " WHERE m.date > 0" : where + " AND m.date > 0";

    // The weekday/hour split is done here rather than in SQL so the UTC
    // calendar rules live in exactly one place (EmailCleanerTypes) and the
    // grid does not shift with the reader's timezone.
    UltraDbResultSet rs;
    UltraDbResult r = UltraDb_Query(connection_,
        "SELECT date FROM messages m" + dated, params, rs);
    if (!r) return r;

    for (const UltraDbRow& row : rs) {
        const UtcParts p = BreakUtcTime(row["date"].AsInt64());
        out.Add(p.weekday, p.hour);
    }
    return r;
}

UltraDbResult AnalysisStore::GetTimeline(const MessageFilter& filter, TimeBucket bucket,
                                         std::vector<TimelinePoint>& out) const {
    out.clear();
    UltraDbParams params;
    const std::string where = BuildWhere(filter, "m", params);

    UltraDbResultSet rs;
    UltraDbResult r = UltraDb_Query(connection_,
        "SELECT date, size_bytes, category FROM messages m" + where +
        " ORDER BY date ASC", params, rs);
    if (!r) return r;

    // Fold into buckets (UTC, via the shared calendar helpers), then fill the
    // gaps so the chart's x-axis is continuous.
    std::map<int64_t, TimelinePoint> buckets;
    for (const UltraDbRow& row : rs) {
        const int64_t date = row["date"].AsInt64();
        if (date <= 0) continue;
        const int64_t start = BucketStart(date, bucket);
        TimelinePoint& point = buckets[start];
        point.bucketStart = start;
        point.messageCount += 1;
        point.totalBytes += row["size_bytes"].AsInt64();
        if (IsUnwanted(CategoryFromString(row["category"].AsString())))
            point.unwantedCount += 1;
    }
    if (buckets.empty()) return r;

    const int64_t first = buckets.begin()->first;
    const int64_t last  = buckets.rbegin()->first;
    for (int64_t start = first; start <= last; ) {
        auto it = buckets.find(start);
        if (it != buckets.end()) {
            it->second.label = BucketLabel(start, bucket);
            out.push_back(it->second);
        } else {
            TimelinePoint empty;
            empty.bucketStart = start;
            empty.label = BucketLabel(start, bucket);
            out.push_back(empty);
        }
        // Step to the next bucket start. Adding a nominal period and
        // re-snapping keeps month and year steps correct.
        int64_t step = 86400;
        switch (bucket) {
            case TimeBucket::Day:   step = 86400; break;
            case TimeBucket::Week:  step = 7 * 86400; break;
            case TimeBucket::Month: step = 32 * 86400; break;
            case TimeBucket::Year:  step = 366 * 86400; break;
        }
        const int64_t next = BucketStart(start + step, bucket);
        if (next <= start) break;   // defensive: never loop forever
        start = next;
        // A very sparse range would otherwise generate an unbounded number of
        // empty buckets; 2000 columns is far past what any chart can show.
        if (out.size() >= 2000) break;
    }
    return UltraDbResult::Ok();
}

// ---- Rollups ---------------------------------------------------------------

UltraDbResult AnalysisStore::GetCategoryTotals(const MessageFilter& filter,
                                               std::vector<CategoryTotal>& out) const {
    out.clear();
    UltraDbParams params;
    const std::string where = BuildWhere(filter, "m", params);

    UltraDbResultSet rs;
    UltraDbResult r = UltraDb_Query(connection_,
        "SELECT category, COUNT(*) AS n, SUM(size_bytes) AS bytes FROM messages m" +
        where + " GROUP BY category ORDER BY n DESC", params, rs);
    if (!r) return r;
    for (const UltraDbRow& row : rs) {
        CategoryTotal t;
        t.category     = CategoryFromString(row["category"].AsString());
        t.messageCount = row["n"].AsInt();
        t.totalBytes   = row["bytes"].AsInt64();
        out.push_back(t);
    }
    return r;
}

UltraDbResult AnalysisStore::GetAttachmentTypeTotals(
        const MessageFilter& filter, std::vector<AttachmentTypeTotal>& out) const {
    out.clear();
    UltraDbParams params;
    const std::string where = BuildWhere(filter, "m", params);

    UltraDbResultSet rs;
    UltraDbResult r = UltraDb_Query(connection_,
        "SELECT a.media_type AS media_type, COUNT(*) AS n, "
        "       SUM(a.size_bytes) AS bytes, "
        "       SUM(CASE WHEN a.risky <> 0 THEN 1 ELSE 0 END) AS risky "
        "FROM attachments a JOIN messages m "
        "  ON a.account_id = m.account_id AND a.folder = m.folder AND a.uid = m.uid" +
        where + " GROUP BY a.media_type ORDER BY bytes DESC", params, rs);
    if (!r) return r;
    for (const UltraDbRow& row : rs) {
        AttachmentTypeTotal t;
        t.mediaType  = row["media_type"].AsString();
        t.count      = row["n"].AsInt();
        t.totalBytes = row["bytes"].AsInt64();
        t.riskyCount = row["risky"].AsInt();
        out.push_back(std::move(t));
    }
    return r;
}

UltraDbResult AnalysisStore::GetOverview(const MessageFilter& filter,
                                         StoreOverview& out) const {
    out = StoreOverview{};
    UltraDbParams params;
    const std::string where = BuildWhere(filter, "m", params);

    UltraDbResultSet rs;
    UltraDbResult r = UltraDb_Query(connection_,
        "SELECT COUNT(*) AS messages,"
        "       COUNT(DISTINCT sender_addr) AS senders,"
        "       COUNT(DISTINCT account_id) AS accounts,"
        "       SUM(size_bytes) AS total_bytes,"
        "       SUM(attachment_count) AS attachments,"
        "       SUM(attachment_bytes) AS attachment_bytes,"
        "       MIN(CASE WHEN date > 0 THEN date END) AS first_date,"
        "       MAX(date) AS last_date,"
        "       SUM(CASE WHEN category IN " + UnwantedCategorySqlList() +
        "       THEN 1 ELSE 0 END) AS unwanted "
        "FROM messages m" + where, params, rs);
    if (!r) return r;
    if (rs.Empty()) return r;

    const UltraDbRow& row = rs.Row(0);
    out.messages        = row["messages"].AsInt();
    out.senders         = row["senders"].AsInt();
    out.accounts        = row["accounts"].AsInt();
    out.totalBytes      = row["total_bytes"].AsInt64();
    out.attachments     = row["attachments"].AsInt();
    out.attachmentBytes = row["attachment_bytes"].AsInt64();
    out.firstDate       = row["first_date"].AsInt64();
    out.lastDate        = row["last_date"].AsInt64();
    out.unwanted        = row["unwanted"].AsInt();
    return r;
}

UltraDbResult AnalysisStore::GetTopKeywords(const MessageFilter& filter, int limit,
                                            std::vector<KeywordHit>& out) const {
    out.clear();
    UltraDbParams params;
    const std::string where = BuildWhere(filter, "m", params);

    std::string sql =
        "SELECT k.term AS term, k.category AS category, k.field AS field, "
        "       SUM(k.weight) AS total_weight, COUNT(*) AS n "
        "FROM keyword_hits k JOIN messages m "
        "  ON k.account_id = m.account_id AND k.folder = m.folder AND k.uid = m.uid" +
        where + " GROUP BY k.term, k.category, k.field ORDER BY n DESC, total_weight DESC";
    if (limit > 0) {
        sql += " LIMIT ?";
        params.push_back(limit);
    }

    UltraDbResultSet rs;
    UltraDbResult r = UltraDb_Query(connection_, sql, params, rs);
    if (!r) return r;
    for (const UltraDbRow& row : rs) {
        KeywordHit h;
        h.term     = row["term"].AsString();
        h.category = CategoryFromString(row["category"].AsString());
        h.field    = MatchFieldFromString(row["field"].AsString());
        // The rollup reports how often a term fired, which is what the
        // evidence list ranks by; the weight column carries the count.
        h.weight   = static_cast<double>(row["n"].AsInt());
        out.push_back(std::move(h));
    }
    return r;
}

// ---- Blocklist -------------------------------------------------------------

UltraDbResult AnalysisStore::AddBlock(const BlockEntry& entry) {
    if (!entry.Valid())
        return UltraDbResult::Error(UltraDbResultCode::InvalidArgument,
                                    "a block needs a pattern");
    return UltraDb_Exec(connection_,
        "INSERT INTO blocklist(pattern, is_domain, reason, added) VALUES(?, ?, ?, ?) "
        "ON CONFLICT(pattern) DO UPDATE SET "
        "  is_domain = excluded.is_domain,"
        "  reason = excluded.reason",
        { Lower(entry.pattern), entry.isDomain ? 1 : 0, entry.reason, entry.added });
}

UltraDbResult AnalysisStore::RemoveBlock(const std::string& pattern) {
    return UltraDb_Exec(connection_, "DELETE FROM blocklist WHERE pattern = ?",
                        { Lower(pattern) });
}

UltraDbResult AnalysisStore::ListBlocks(std::vector<BlockEntry>& out) const {
    out.clear();
    UltraDbResultSet rs;
    UltraDbResult r = UltraDb_Query(connection_,
        "SELECT pattern, is_domain, reason, added FROM blocklist "
        "ORDER BY added DESC, pattern", rs);
    if (!r) return r;
    for (const UltraDbRow& row : rs) {
        BlockEntry e;
        e.pattern  = row["pattern"].AsString();
        e.isDomain = row["is_domain"].AsInt64() != 0;
        e.reason   = row["reason"].AsString();
        e.added    = row["added"].AsInt64();
        out.push_back(std::move(e));
    }
    return r;
}

bool AnalysisStore::IsBlocked(const std::string& senderAddr,
                              const std::string& domain) const {
    // An address block and a domain block are separate patterns; one query
    // answers both so the ingest can call this per message cheaply.
    const std::string addr = Lower(senderAddr);
    const std::string dom  = domain.empty() ? DomainOf(addr) : Lower(domain);

    UltraDbResultSet rs;
    UltraDbResult r = UltraDb_Query(connection_,
        "SELECT 1 FROM blocklist WHERE "
        "  (is_domain = 0 AND pattern = ?) OR (is_domain <> 0 AND pattern = ?) LIMIT 1",
        { addr, dom }, rs);
    return r && !rs.Empty();
}

UltraDbResult AnalysisStore::ApplyBlocklistToMessages() {
    // Re-stamp every row from its sender's current block state. Done as two
    // statements rather than per sender: the blocklist is small and this keeps
    // it to one pass whatever its size.
    UltraDbResult r = UltraDb_Exec(connection_, "UPDATE messages SET blocked = 0");
    if (!r) return r;
    return UltraDb_Exec(connection_,
        "UPDATE messages SET blocked = 1 WHERE "
        "  sender_addr IN (SELECT pattern FROM blocklist WHERE is_domain = 0) "
        "  OR sender_domain IN (SELECT pattern FROM blocklist WHERE is_domain <> 0)");
}

// ---- Verdict overrides -----------------------------------------------------

UltraDbResult AnalysisStore::SetOverride(const VerdictOverride& entry) {
    if (!entry.Valid())
        return UltraDbResult::Error(UltraDbResultCode::InvalidArgument,
                                    "an override needs a pattern");
    return UltraDb_Exec(connection_,
        "INSERT INTO verdict_overrides(pattern, is_domain, category, reason, added) "
        "VALUES(?, ?, ?, ?, ?) "
        "ON CONFLICT(pattern) DO UPDATE SET "
        "  is_domain = excluded.is_domain,"
        "  category = excluded.category,"
        "  reason = excluded.reason,"
        "  added = excluded.added",
        { Lower(entry.pattern), entry.isDomain ? 1 : 0, ToString(entry.category),
          entry.reason, entry.added });
}

UltraDbResult AnalysisStore::RemoveOverride(const std::string& pattern) {
    return UltraDb_Exec(connection_, "DELETE FROM verdict_overrides WHERE pattern = ?",
                        { Lower(pattern) });
}

UltraDbResult AnalysisStore::ListOverrides(std::vector<VerdictOverride>& out) const {
    out.clear();
    UltraDbResultSet rs;
    UltraDbResult r = UltraDb_Query(connection_,
        "SELECT pattern, is_domain, category, reason, added FROM verdict_overrides "
        "ORDER BY added DESC, pattern", rs);
    if (!r) return r;
    for (const UltraDbRow& row : rs) {
        VerdictOverride e;
        e.pattern  = row["pattern"].AsString();
        e.isDomain = row["is_domain"].AsInt64() != 0;
        e.category = CategoryFromString(row["category"].AsString());
        e.reason   = row["reason"].AsString();
        e.added    = row["added"].AsInt64();
        out.push_back(std::move(e));
    }
    return r;
}

bool AnalysisStore::FindOverride(const std::string& senderAddr,
                                 const std::string& domain,
                                 VerdictOverride& out) const {
    const std::string addr = Lower(senderAddr);
    const std::string dom  = domain.empty() ? DomainOf(addr) : Lower(domain);

    // is_domain ASC puts the address row first, so the more specific statement
    // wins without a second query.
    UltraDbResultSet rs;
    UltraDbResult r = UltraDb_Query(connection_,
        "SELECT pattern, is_domain, category, reason, added FROM verdict_overrides "
        "WHERE (is_domain = 0 AND pattern = ?) OR (is_domain <> 0 AND pattern = ?) "
        "ORDER BY is_domain ASC LIMIT 1", { addr, dom }, rs);
    if (!r || rs.Empty()) return false;

    const UltraDbRow& row = rs.Row(0);
    out.pattern  = row["pattern"].AsString();
    out.isDomain = row["is_domain"].AsInt64() != 0;
    out.category = CategoryFromString(row["category"].AsString());
    out.reason   = row["reason"].AsString();
    out.added    = row["added"].AsInt64();
    return true;
}

UltraDbResult AnalysisStore::ApplyOverridesToMessages() {
    // Two statements, in one transaction so the corpus is never half-stamped.
    //
    // First put every message back to what the classifier said. That is what
    // makes removing an override undo it: nothing has to remember which rows a
    // deleted override used to touch.
    UltraDbResult error;
    UltraDbHandle tx = UltraDb_Begin(connection_, &error);
    if (tx == UltraDbInvalidHandle) return error;

    UltraDbResult r = UltraDb_ExecInTx(tx,
        "UPDATE messages SET category = base_category, score = base_score, overridden = 0 "
        "WHERE overridden <> 0 AND base_category <> ''");
    if (!r) { UltraDb_Rollback(tx); return r; }

    // Then apply what the table says now. An address override beats a domain
    // one, so the domain pass runs first and the address pass overwrites it.
    //
    // Score follows the direction of the correction rather than being invented:
    // "this is fine" means nothing unwanted about it, so 0; "this is spam" is
    // the user asserting it outright, which is a full 100.
    const std::string unwanted = UnwantedCategorySqlList();
    auto applyPass = [&](const char* isDomain, const char* column) {
        const std::string match = std::string("o.is_domain ") + isDomain +
                                  " AND o.pattern = messages." + column;
        return std::string(
            "UPDATE messages SET "
            "  category = (SELECT o.category FROM verdict_overrides o WHERE ") + match + "),"
            "  score = (SELECT CASE WHEN o.category IN " + unwanted +
            "           THEN 100.0 ELSE 0.0 END FROM verdict_overrides o WHERE " + match + "),"
            "  overridden = 1 "
            "WHERE EXISTS (SELECT 1 FROM verdict_overrides o WHERE " + match + ")";
    };

    r = UltraDb_ExecInTx(tx, applyPass("<> 0", "sender_domain"));
    if (!r) { UltraDb_Rollback(tx); return r; }

    r = UltraDb_ExecInTx(tx, applyPass("= 0", "sender_addr"));
    if (!r) { UltraDb_Rollback(tx); return r; }

    return UltraDb_Commit(tx);
}

// ---- Unsubscribe -----------------------------------------------------------

UltraDbResult AnalysisStore::GetUnsubscribeOffer(const MessageFilter& filter,
                                                 std::string& outMailto,
                                                 std::string& outMailtoSubject,
                                                 std::string& outUrl,
                                                 bool& outOneClick,
                                                 bool& found) const {
    outMailto.clear();
    outMailtoSubject.clear();
    outUrl.clear();
    outOneClick = false;
    found = false;

    UltraDbParams params;
    const std::string where = BuildWhere(filter, "m", params);
    const std::string offered =
        (where.empty() ? " WHERE " : where + " AND ") +
        "(m.unsub_mailto <> '' OR m.unsub_url <> '')";

    // Newest first: a sender that rotated its unsubscribe URL should be acted
    // on with the one it used most recently.
    UltraDbResultSet rs;
    UltraDbResult r = UltraDb_Query(connection_,
        "SELECT unsub_mailto, unsub_mailto_subject, unsub_url, unsub_one_click "
        "FROM messages m" + offered + " ORDER BY m.date DESC, m.uid DESC LIMIT 1",
        params, rs);
    if (!r) return r;
    if (rs.Empty()) return r;

    const UltraDbRow& row = rs.Row(0);
    outMailto        = row["unsub_mailto"].AsString();
    outMailtoSubject = row["unsub_mailto_subject"].AsString();
    outUrl           = row["unsub_url"].AsString();
    outOneClick      = row["unsub_one_click"].AsInt64() != 0;
    found = true;
    return r;
}

// ---- Ingest bookkeeping ----------------------------------------------------

UltraDbResult AnalysisStore::UpsertIngestState(const IngestState& state) {
    return UltraDb_Exec(connection_,
        "INSERT INTO ingest_state(account_id, folder, last_uid, last_run, messages) "
        "VALUES(?, ?, ?, ?, ?) "
        "ON CONFLICT(account_id, folder) DO UPDATE SET "
        "  last_uid = excluded.last_uid,"
        "  last_run = excluded.last_run,"
        "  messages = excluded.messages",
        { state.accountId, state.folder, state.lastUid, state.lastRun, state.messages });
}

UltraDbResult AnalysisStore::ListIngestState(const std::string& accountId,
                                             std::vector<IngestState>& out) const {
    out.clear();
    UltraDbResultSet rs;
    UltraDbResult r;
    if (accountId.empty()) {
        r = UltraDb_Query(connection_,
            "SELECT account_id, folder, last_uid, last_run, messages FROM ingest_state "
            "ORDER BY account_id, folder", rs);
    } else {
        r = UltraDb_Query(connection_,
            "SELECT account_id, folder, last_uid, last_run, messages FROM ingest_state "
            "WHERE account_id = ? ORDER BY folder", { accountId }, rs);
    }
    if (!r) return r;
    for (const UltraDbRow& row : rs) {
        IngestState s;
        s.accountId = row["account_id"].AsString();
        s.folder    = row["folder"].AsString();
        s.lastUid   = row["last_uid"].AsInt64();
        s.lastRun   = row["last_run"].AsInt64();
        s.messages  = row["messages"].AsInt();
        out.push_back(std::move(s));
    }
    return r;
}

UltraDbResult AnalysisStore::GetLastUid(const std::string& accountId,
                                        const std::string& folder, int64_t& out) const {
    out = 0;
    UltraDbResultSet rs;
    UltraDbResult r = UltraDb_Query(connection_,
        "SELECT MAX(uid) AS max_uid FROM messages WHERE account_id = ? AND folder = ?",
        { accountId, folder }, rs);
    if (!r) return r;
    if (!rs.Empty()) out = rs.Row(0)["max_uid"].AsInt64();
    return r;
}

} // namespace EmailCleaner
