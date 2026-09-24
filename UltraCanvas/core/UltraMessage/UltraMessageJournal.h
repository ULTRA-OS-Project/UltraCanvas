// UltraCanvas/core/UltraMessage/UltraMessageJournal.h
// The broker's journal (§8): persistent topics on an UltraDatabase (SQLite)
// file, queried by the feed. Only the broker opens it; endpoints reach it
// through the control RPC, so one process holds the database at a time.
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMessageInternal.h"

#include <UltraDatabase/UltraDatabaseValue.h>

#include <mutex>
#include <string>
#include <vector>

namespace UltraMessage {
namespace Internal {

class Journal {
public:
    Journal() = default;
    ~Journal();
    Journal(const Journal&) = delete;
    Journal& operator=(const Journal&) = delete;

    // ":memory:" keeps the journal in RAM (tests, tools).
    UltraMsgResult Open(const std::string& path);
    void Close();
    bool IsOpen() const { return !connection_.empty(); }
    const std::string& Path() const { return path_; }

    // Writes a message; a message with the Replace flag removes the one it
    // names first. Idempotent for an id already stored.
    UltraMsgResult Store(const UltraMsgMessage& message);

    UltraMsgResult Query(const UltraMsgQuery& query, std::vector<UltraMsgMessage>& out);
    UltraMsgResult Count(const UltraMsgQuery& query, int64_t& out);
    UltraMsgResult Get(const std::string& id, UltraMsgMessage& out);
    UltraMsgResult SetRead(const std::vector<std::string>& ids, bool read);
    UltraMsgResult Dismiss(const std::vector<std::string>& ids);
    UltraMsgResult Delete(const std::vector<std::string>& ids);
    UltraMsgResult ListConversations(const UltraMsgQuery& query,
                                     std::vector<UltraMsgConversation>& out);
    UltraMsgResult SetRetention(const std::string& pattern, int days, int64_t maxRows);
    // Applies every retention rule plus the defaults (90 days, 50 000 rows).
    UltraMsgResult ApplyRetention();
    UltraMsgResult Export(const UltraMsgQuery& query, const std::string& path, int64_t& outCount);

    // The persisted adapter switches (§9). Unknown names report `found` false
    // so the caller applies the adapter's default.
    UltraMsgResult GetAdapterEnabled(const std::string& name, bool& enabled, bool& found);
    UltraMsgResult SetAdapterEnabled(const std::string& name, bool enabled);

private:
    struct Clause {
        std::string sql;                  // " AND ..." fragments
        std::vector<std::string> topicPatterns; // post-filtered for exactness
    };
    Clause BuildWhere(const UltraMsgQuery& query, std::vector<UltraDbValue>& params) const;
    UltraMsgResult ReadRows(const std::string& sql, const std::vector<UltraDbValue>& params,
                            const std::vector<std::string>& patterns,
                            std::vector<UltraMsgMessage>& out);

    mutable std::mutex mutex_;
    std::string connection_;
    std::string path_;
};

// The searchable text of a message body, per topic (text, subject + snippet,
// summary + body); what `textContains` matches against.
std::string ExtractSearchText(const std::string& topic, const JSONValue& body);
// The body.service of a messaging.message, else empty.
std::string ExtractService(const JSONValue& body);

} // namespace Internal
} // namespace UltraMessage
