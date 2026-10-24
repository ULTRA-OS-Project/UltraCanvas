// Apps/UltraDBAdmin/engine/DbAdminSqlLog.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "DbAdminSqlLog.h"

#include <algorithm>
#include <chrono>
#include <cstdio>

namespace UltraDBAdmin {

void SqlLog::Add(const UltraDbStatementEvent& event) {
    LogEntry entry;
    const auto now = std::chrono::system_clock::now();
    entry.at = std::chrono::system_clock::to_time_t(now);
    entry.millisecond = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000);
    entry.connection = event.connection;
    entry.kind = event.kind;
    entry.sql = event.sql;
    entry.boundValues = event.boundValues;
    entry.seconds = event.seconds;
    entry.rows = event.rows;
    entry.affected = event.result.affectedRows;
    entry.inTransaction = event.inTransaction;
    entry.ok = event.result.success;
    if (!entry.ok) entry.message = event.result.message;

    std::lock_guard<std::mutex> lock(mutex_);
    entry.seq = nextSeq_++;
    entries_.push_back(std::move(entry));
    if (entries_.size() > limit_) {
        // A tenth at a time, so a full log does not move every entry per statement.
        const size_t drop = std::max<size_t>(1, limit_ / 10);
        entries_.erase(entries_.begin(), entries_.begin() + static_cast<std::ptrdiff_t>(drop));
        dropped_ += drop;
    }
}

void SqlLog::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.clear();
}

std::vector<LogEntry> SqlLog::Since(uint64_t seq) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<LogEntry> out;
    for (const auto& entry : entries_)
        if (entry.seq > seq) out.push_back(entry);
    return out;
}

uint64_t SqlLog::LastSeq() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return nextSeq_ - 1;
}

size_t SqlLog::Size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_.size();
}

uint64_t SqlLog::Dropped() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return dropped_;
}

int SqlLog::Attach(const std::shared_ptr<SqlLog>& log, std::function<bool(const std::string&)> accept) {
    // The observer owns the log: a report still under way when the window
    // removes it finishes on a log that is still there.
    return UltraDb_AddStatementObserver([log, accept = std::move(accept)](const UltraDbStatementEvent& event) {
        if (!accept || accept(event.connection)) log->Add(event);
    });
}

std::string LogTime(const LogEntry& entry) {
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &entry.at);
#else
    localtime_r(&entry.at, &local);
#endif
    char text[24];
    std::snprintf(text, sizeof text, "%02d:%02d:%02d.%03d", local.tm_hour, local.tm_min, local.tm_sec,
                  entry.millisecond);
    return text;
}

std::string LogDuration(const LogEntry& entry) {
    if (entry.seconds < 0.0) return std::string();
    char text[32];
    // locale-ok: a duration for the reader, in the desktop's own decimal mark.
    if (entry.seconds < 1.0) std::snprintf(text, sizeof text, "%.0f ms", entry.seconds * 1000.0);
    else std::snprintf(text, sizeof text, "%.2f s", entry.seconds);
    return text;
}

std::string LogOutcome(const LogEntry& entry) {
    if (!entry.ok) return "failed";
    if (entry.rows > 0) return std::to_string(entry.rows) + (entry.rows == 1 ? " row" : " rows");
    if (entry.affected > 0) return std::to_string(entry.affected) + " changed";
    return std::string();
}

const char* LogKindWord(UltraDbStatementEvent::Kind kind) {
    switch (kind) {
        case UltraDbStatementEvent::Kind::Query:         return "query";
        case UltraDbStatementEvent::Kind::Exec:          return "exec";
        case UltraDbStatementEvent::Kind::Transaction:   return "transaction";
        case UltraDbStatementEvent::Kind::Script:        return "script";
        case UltraDbStatementEvent::Kind::Cursor:        return "cursor";
        case UltraDbStatementEvent::Kind::Introspection: return "schema";
    }
    return "query";
}

std::string LogText(const std::vector<LogEntry>& entries,
                    const std::function<std::string(const std::string&)>& connectionName) {
    std::string text;
    for (const auto& entry : entries) {
        std::string head = "-- " + LogTime(entry) + "  " +
                           (connectionName ? connectionName(entry.connection) : entry.connection);
        const std::string duration = LogDuration(entry);
        if (!duration.empty()) head += "  " + duration;
        const std::string outcome = LogOutcome(entry);
        if (!outcome.empty()) head += "  " + outcome;
        if (entry.boundValues > 0)
            head += "  " + std::to_string(entry.boundValues) + (entry.boundValues == 1 ? " value bound" : " values bound");
        if (!entry.ok) {
            // The error on one comment line: a newline in it would end the comment.
            std::string message = entry.message;
            for (auto& c : message)
                if (c == '\n' || c == '\r') c = ' ';
            head += ": " + message;
        }
        text += head + "\n";
        // Introspection is not SQL to run: it stays a comment.
        text += entry.kind == UltraDbStatementEvent::Kind::Introspection ? entry.sql + "\n\n" : entry.sql + ";\n\n";
    }
    return text;
}

} // namespace UltraDBAdmin
