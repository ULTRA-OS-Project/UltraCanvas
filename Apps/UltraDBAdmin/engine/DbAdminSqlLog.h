// Apps/UltraDBAdmin/engine/DbAdminSqlLog.h
// The SQL log: every statement the app ran - a Browse page, a row count, a
// commit, an import, the console's scripts, the tree's introspection - with
// when, where, how long, how many rows and how it ended, so "show me the SQL"
// holds for every action, not just some
// (Docs/Research/UltraDBAdminDesignProposal.md, 5.1).
//
// Filled by an UltraDatabase statement observer (UltraDb_AddStatementObserver)
// for the connections the app registered, on whatever thread ran the
// statement; read by the window on the UI thread. The newest `limit` entries
// are kept. Bound values are never logged, only how many there were.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <UltraDatabase/UltraDatabase.h>

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace UltraDBAdmin {

struct LogEntry {
    uint64_t seq = 0;                    // rises by one per entry, from 1
    std::time_t at = 0;                  // when it ended, wall clock
    int millisecond = 0;                 // of `at`
    std::string connection;              // the registered name it ran on
    UltraDbStatementEvent::Kind kind = UltraDbStatementEvent::Kind::Query;
    std::string sql;
    size_t boundValues = 0;
    double seconds = 0.0;                // -1: not timed on its own (one statement of a script)
    size_t rows = 0;
    int64_t affected = 0;
    bool inTransaction = false;
    bool ok = true;
    std::string message;                 // the error, when it failed
};

class SqlLog {
public:
    explicit SqlLog(size_t limit = 2000) : limit_(limit) {}

    void Add(const UltraDbStatementEvent& event);
    void Clear();

    // The entries after `seq` (0: all), oldest first.
    std::vector<LogEntry> Since(uint64_t seq) const;
    uint64_t LastSeq() const;
    size_t Size() const;
    // Entries dropped to keep the limit, ever: the window says so.
    uint64_t Dropped() const;

    // Starts filling `log` from UltraDatabase, for the connection names
    // `accept` takes (on any thread). Returns the observer's id for
    // UltraDb_RemoveStatementObserver.
    static int Attach(const std::shared_ptr<SqlLog>& log, std::function<bool(const std::string&)> accept);

private:
    size_t limit_;
    mutable std::mutex mutex_;
    std::vector<LogEntry> entries_;      // oldest first
    uint64_t nextSeq_ = 1;
    uint64_t dropped_ = 0;
};

// "14:03:27.120" - the entry's time of day, local.
std::string LogTime(const LogEntry& entry);
// "12 ms", "1.24 s", "" for an untimed one.
std::string LogDuration(const LogEntry& entry);
// "50 rows", "3 changed", "failed", "".
std::string LogOutcome(const LogEntry& entry);
// "query", "exec", "transaction", "script", "cursor", "schema".
const char* LogKindWord(UltraDbStatementEvent::Kind kind);

// The entries as text to copy: one statement per paragraph, each headed by a
// comment line with its time, where, how long and how it ended - so the text
// pastes into the console and runs.
std::string LogText(const std::vector<LogEntry>& entries,
                    const std::function<std::string(const std::string&)>& connectionName);

} // namespace UltraDBAdmin
