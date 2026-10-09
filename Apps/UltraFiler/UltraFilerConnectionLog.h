// Apps/UltraFiler/UltraFilerConnectionLog.h
// The connection log of UltraFiler's remote drives: every job the drive
// worker runs - opening a folder, an upload, a download, a delete - as a
// session with its lines and its outcome, so a failure can be read
// afterwards with its codes rather than as one line in the status bar. On an
// FTP / SFTP drive the lines are what UltraNet logged: status steps, the
// commands sent, the server's replies, the error. On a cloud drive (Dropbox,
// OneDrive, Google Drive, Nextcloud, WebDAV) they are what UltraCloud logged
// (UltraCloudLog.h): each request, the service's answer with its HTTP status
// and its own reason for a refusal, a renewed sign-in, the pages of a long
// listing, a request to wait.
//
// Two renderings of it, both for the log window's text areas:
//
//   RenderRemoteLogText       the message log, one line per step, the way an
//                             FTP client shows it ("12:03:04  Response:  227
//                             Entering Passive Mode (...)"), oldest first.
//   RenderRemoteErrorsMarkdown the failed sessions as a Markdown report,
//                             newest first: the message, the error class, the
//                             codes (the server's reply, libcurl's error), a
//                             hint at the likely cause, the last steps and
//                             the diagnostics chain.
//
// Header-only and free of UltraCloud and of any UI, so
// Tests/FilerConnectionLogTest.cpp checks it on any build. The log itself is
// thread-safe: the worker writes into it while the UI thread reads.
//
// Kept in memory only, and bounded (kMaxSessions, kMaxLinesPerSession): it is
// for the session at hand, not an audit trail, and it never holds a password
// - UltraNet writes PASS as "PASS ********" before a line gets here, and
// UltraCloud logs no header, no body and no query value that carries a token.
// Version: 1.1.0 - cloud drives: HTTP sessions, their labels and their hints
// Last Modified: 2026-10-09
// Author: UltraCanvas Framework
#pragma once

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <deque>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace UltraCanvas {

// One line of a drive session.
struct RemoteLogLine {
    // Step is an FTP client's "Status:" line; not named so because Xlib
    // #defines Status, and this header is read beside it.
    enum class Kind { Step, Command, Response, Error };
    Kind kind = Kind::Step;
    std::string text;
    int replyCode = 0;       // Response lines: the server's three-digit code
    int transportCode = 0;   // Error lines: libcurl's error number, 0 if none
    int64_t timeMs = 0;      // wall clock, milliseconds since the epoch
};

// One job of the drive worker, from the moment it was taken off the queue.
struct RemoteLogSession {
    uint64_t id = 0;
    std::string drive;        // the drive as the folder tree names it
    std::string server;       // its address; empty for a drive that has none
    std::string operation;    // "Open folder", "Upload", "Download", ...
    std::string target;       // the folder or file, as the server sees it
    // Work nobody was waiting on: a folder fetched ahead, a preview copy.
    // Logged like the rest, but it does not count as an error the user saw.
    bool background = false;
    // A cloud drive's session: its Command lines are HTTP requests, the codes
    // of its Response lines HTTP statuses - which share numbers with FTP's
    // replies (421, 425, 426) and mean something else.
    bool http = false;
    int64_t startedMs = 0;
    int64_t finishedMs = 0;   // 0 while it is still running
    bool failed = false;
    std::string error;        // the message the window showed
    std::string category;     // the error class, in words ("Network", ...)
    std::string diagnostics;  // the transport's "Name: value" lines
    std::vector<RemoteLogLine> lines;
    // Lines left out because the session outgrew kMaxLinesPerSession.
    std::size_t droppedLines = 0;

    bool Running() const { return finishedMs == 0; }

    // The last complete reply the server sent, and its code; 0 / "" if none.
    int LastReplyCode() const {
        for (auto it = lines.rbegin(); it != lines.rend(); ++it)
            if (it->kind == RemoteLogLine::Kind::Response && it->replyCode > 0)
                return it->replyCode;
        return 0;
    }
    std::string LastReply() const {
        for (auto it = lines.rbegin(); it != lines.rend(); ++it)
            if (it->kind == RemoteLogLine::Kind::Response && it->replyCode > 0)
                return it->text;
        return {};
    }
    // Whether the server agreed to passive mode at some point (227 / 229):
    // a timeout after that is a data connection that never carried anything.
    bool EnteredPassiveMode() const {
        for (const RemoteLogLine& l : lines)
            if (l.kind == RemoteLogLine::Kind::Response &&
                (l.replyCode == 227 || l.replyCode == 229))
                return true;
        return false;
    }
    // libcurl's error number from the session's error line; 0 if none.
    int TransportCode() const {
        for (auto it = lines.rbegin(); it != lines.rend(); ++it)
            if (it->kind == RemoteLogLine::Kind::Error && it->transportCode != 0)
                return it->transportCode;
        return 0;
    }
    // The last command sent, without its argument ("MLSD", "STOR"); "" if none.
    std::string LastCommand() const {
        for (auto it = lines.rbegin(); it != lines.rend(); ++it)
            if (it->kind == RemoteLogLine::Kind::Command)
                return it->text.substr(0, it->text.find(' '));
        return {};
    }
};

inline int64_t RemoteLogNowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
}

class RemoteConnectionLog {
public:
    static constexpr std::size_t kMaxSessions = 200;
    static constexpr std::size_t kMaxLinesPerSession = 400;

    // Starts a session from `header` (its id, times and outcome are filled in
    // here) and answers its id.
    uint64_t Begin(RemoteLogSession header) {
        std::lock_guard<std::mutex> lk(mutex_);
        header.id = nextId_++;
        header.startedMs = RemoteLogNowMs();
        header.finishedMs = 0;
        header.failed = false;
        header.lines.clear();
        header.droppedLines = 0;
        sessions_.push_back(std::move(header));
        // The oldest go first. A failure dropped this way stops being
        // counted, so the count always matches what the log can show.
        while (sessions_.size() > kMaxSessions) sessions_.pop_front();
        ++revision_;
        return sessions_.back().id;
    }

    // A line of session `id`; ignored when the session has been dropped.
    void Append(uint64_t id, RemoteLogLine line) {
        std::lock_guard<std::mutex> lk(mutex_);
        RemoteLogSession* s = FindLocked(id);
        if (!s) return;
        if (line.timeMs == 0) line.timeMs = RemoteLogNowMs();
        // A long session keeps its beginning (how it connected) and its end
        // (the error is always the last line): the middle is what goes.
        if (s->lines.size() >= kMaxLinesPerSession &&
            line.kind != RemoteLogLine::Kind::Error) {
            ++s->droppedLines;
        } else {
            s->lines.push_back(std::move(line));
        }
        ++revision_;
    }

    void Finish(uint64_t id, bool failed, const std::string& error,
                const std::string& category, const std::string& diagnostics) {
        std::lock_guard<std::mutex> lk(mutex_);
        RemoteLogSession* s = FindLocked(id);
        if (!s) return;
        s->finishedMs = RemoteLogNowMs();
        if (s->finishedMs <= 0) s->finishedMs = 1;
        s->failed = failed;
        s->error = error;
        s->category = category;
        s->diagnostics = diagnostics;
        ++revision_;
    }

    std::vector<RemoteLogSession> Snapshot() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return {sessions_.begin(), sessions_.end()};
    }

    // The failures somebody was waiting on: background work does not count.
    std::size_t ErrorCount() const {
        std::lock_guard<std::mutex> lk(mutex_);
        std::size_t n = 0;
        for (const RemoteLogSession& s : sessions_)
            if (s.failed && !s.background) ++n;
        return n;
    }

    std::size_t SessionCount() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return sessions_.size();
    }

    // Changes on every write, so a reader can tell whether to render again.
    uint64_t Revision() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return revision_;
    }

    void Clear() {
        std::lock_guard<std::mutex> lk(mutex_);
        sessions_.clear();
        ++revision_;
    }

private:
    RemoteLogSession* FindLocked(uint64_t id) {
        // Nearly always the newest one.
        for (auto it = sessions_.rbegin(); it != sessions_.rend(); ++it)
            if (it->id == id) return &*it;
        return nullptr;
    }

    mutable std::mutex mutex_;
    std::deque<RemoteLogSession> sessions_;
    uint64_t nextId_ = 1;
    uint64_t revision_ = 0;
};

// ===== RENDERING =====

// "12:03:04", in local time.
inline std::string FormatRemoteLogClock(int64_t ms) {
    const std::time_t t = static_cast<std::time_t>(ms / 1000);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[16];
    std::snprintf(buf, sizeof buf, "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}

// "850 ms", "31.2 s", "4 min 05 s". Whole-number arithmetic, so no locale
// decides the decimal mark.
inline std::string FormatRemoteLogDuration(int64_t ms) {
    if (ms < 0) ms = 0;
    if (ms < 1000) return std::to_string(ms) + " ms";
    if (ms < 60000)
        return std::to_string(ms / 1000) + "." + std::to_string((ms % 1000) / 100) + " s";
    const int64_t seconds = ms / 1000;
    const int64_t rest = seconds % 60;
    return std::to_string(seconds / 60) + " min " + (rest < 10 ? "0" : "") +
           std::to_string(rest) + " s";
}

// "Status:   ", padded so the texts line up the way they do in an FTP client.
inline const char* RemoteLogKindLabel(RemoteLogLine::Kind kind, bool http = false) {
    switch (kind) {
        case RemoteLogLine::Kind::Step:     return "Status:   ";
        case RemoteLogLine::Kind::Command:  return http ? "Request:  " : "Command:  ";
        case RemoteLogLine::Kind::Response: return "Response: ";
        case RemoteLogLine::Kind::Error:    return "Error:    ";
    }
    return "";
}

// One line as the message log shows it.
inline std::string FormatRemoteLogLine(const RemoteLogLine& line, bool http = false) {
    std::string out = FormatRemoteLogClock(line.timeMs) + "  " +
                      RemoteLogKindLabel(line.kind, http) + line.text;
    if (line.kind == RemoteLogLine::Kind::Error && line.transportCode != 0)
        out += "  [libcurl error " + std::to_string(line.transportCode) + "]";
    return out;
}

// "Open folder /pub on Backup NAS (ftp://files.example.org)".
inline std::string DescribeRemoteSession(const RemoteLogSession& s) {
    std::string out = s.operation;
    if (!s.target.empty()) out += " " + s.target;
    if (!s.drive.empty()) out += " on " + s.drive;
    if (!s.server.empty() && s.server != s.drive) out += " (" + s.server + ")";
    if (s.background) out += " - in the background";
    return out;
}

// The message log: each session as a header line, its lines, and how it
// ended. Oldest first, like any log.
inline std::string RenderRemoteLogText(const std::vector<RemoteLogSession>& sessions) {
    if (sessions.empty())
        return "No connections yet. Open a folder on an FTP, SFTP or cloud drive and "
               "every step of the connection is listed here.\n";
    std::string out;
    for (const RemoteLogSession& s : sessions) {
        out += "--- " + FormatRemoteLogClock(s.startedMs) + "  " + DescribeRemoteSession(s) +
               " ---\n";
        for (std::size_t i = 0; i < s.lines.size(); ++i) {
            // Where the middle of an overlong session was left out.
            if (s.droppedLines > 0 && i + 1 == s.lines.size() &&
                s.lines[i].kind == RemoteLogLine::Kind::Error)
                out += "          ... " + std::to_string(s.droppedLines) + " lines left out\n";
            out += FormatRemoteLogLine(s.lines[i], s.http) + "\n";
        }
        if (s.droppedLines > 0 && (s.lines.empty() ||
                                   s.lines.back().kind != RemoteLogLine::Kind::Error))
            out += "          ... " + std::to_string(s.droppedLines) + " lines left out\n";
        // A session still running is the last one, and its lines are still
        // coming: nothing closes it yet, so the text only ever grows at the
        // end and a view can follow it.
        if (s.Running()) continue;
        if (s.failed) {
            out += FormatRemoteLogClock(s.finishedMs) + "  Failed after " +
                   FormatRemoteLogDuration(s.finishedMs - s.startedMs) + ": " + s.error + "\n";
        } else {
            out += FormatRemoteLogClock(s.finishedMs) + "  Done in " +
                   FormatRemoteLogDuration(s.finishedMs - s.startedMs) + "\n";
        }
        out += "\n";
    }
    return out;
}

// Text that goes into Markdown prose or a table cell as it is: the characters
// that would start emphasis, code, a link, a formula or a cell break are
// escaped, and a line break becomes a space.
inline std::string EscapeRemoteLogMarkdown(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (char c : text) {
        switch (c) {
            case '\\': case '*': case '_': case '`': case '[': case ']':
            case '$': case '|': case '<': case '>': case '#':
                out.push_back('\\');
                out.push_back(c);
                break;
            case '\r': break;
            case '\n': out.push_back(' '); break;
            default: out.push_back(c);
        }
    }
    return out;
}

// Text for a fenced code block: a run of three backticks would end the block.
inline std::string FenceSafe(const std::string& text) {
    std::string out = text;
    for (std::size_t at = out.find("```"); at != std::string::npos; at = out.find("```", at))
        out.replace(at, 3, "'''");
    return out;
}

// The likely cause of a failed cloud drive session, from the HTTP status of
// the service's last answer; "" when nothing more specific than the message
// itself can be said.
inline std::string RemoteHttpFailureHint(const RemoteLogSession& s) {
    // Google Drive says it is throttling with a 403; the message says so.
    if (s.error.find("limiting requests") != std::string::npos)
        return "The service is limiting how many requests this account may make. Wait the "
               "time it asks for and try again; opening fewer folders at once helps.";
    switch (s.LastReplyCode()) {
        case 0:
            if (s.failed && !s.lines.empty() &&
                s.lines.back().kind == RemoteLogLine::Kind::Error)
                return "The service could not be reached. Check that this computer is "
                       "online, and that no proxy or firewall is in the way.";
            return {};
        case 400:
            return "The service did not accept the request - often a file or folder name "
                   "it does not allow.";
        case 401:
            return "The service no longer accepts the drive's sign-in: the access was "
                   "revoked, the password or app password changed, or the sign-in expired. "
                   "Sign in to the drive again.";
        case 403:
            return "The service refused: this account may not use that file or folder, or "
                   "UltraFiler was not given access to it when the drive was signed in.";
        case 404:
            return "The file or folder is not there any more - moved, renamed or deleted "
                   "elsewhere. Refresh the folder.";
        case 409:
            return "The service refused because of a conflict: an item of that name already "
                   "exists, or a folder on the way is missing.";
        case 412: case 423:
            return "The file is locked, or someone changed it meanwhile. Try again in a "
                   "moment.";
        case 413:
            return "The file is larger than the service accepts.";
        case 429:
            return "The service is limiting how many requests this account may make. Wait the "
                   "time it asks for and try again; opening fewer folders at once helps.";
        case 500: case 502: case 503: case 504:
            return "The service itself had a problem, or is overloaded. Try again in a while; "
                   "if it lasts, the service's status page may say why.";
        case 507:
            return "The drive is full: the account's storage is used up.";
        default: break;
    }
    return {};
}

// A sentence on the likely cause of a failed session, from the server's last
// reply and libcurl's error; "" when nothing more specific than the message
// itself can be said.
inline std::string RemoteFailureHint(const RemoteLogSession& s) {
    if (s.http) return RemoteHttpFailureHint(s);
    const int reply = s.LastReplyCode();
    const int curl = s.TransportCode();
    const std::string command = s.LastCommand();
    const bool dataCommand = command == "MLSD" || command == "LIST" || command == "NLST" ||
                             command == "RETR" || command == "STOR" || command == "APPE";
    switch (curl) {
        case 6:
            return "The server's name could not be found. Check the address in the "
                   "drive's settings, and that this computer is online.";
        case 7:
            return "Nothing answered at that address and port. Check the port, and "
                   "that the server is running and can be reached from this network.";
        case 9:
            return "The server would not let this account into that folder.";
        case 28:
            // libcurl sends TYPE between PASV and the listing, so the last
            // reply is usually "200 Type set" - what matters is that passive
            // mode was granted and a listing or transfer was asked for.
            if (s.EnteredPassiveMode() && dataCommand)
                return "The server accepted passive mode, but no data arrived on the "
                       "data connection. That is usually a firewall or NAT between this "
                       "computer and the server blocking the data connection (the server "
                       "needs its passive port range open), or an overloaded server.";
            if (reply == 0)
                return "The server did not answer. Check the address and port, and "
                       "that no firewall is dropping the connection.";
            return "The server stopped answering part way through.";
        case 35: case 60: case 77:
            return "The secure (TLS) connection could not be set up: the server's "
                   "certificate is not trusted here, or it does not offer TLS on this port.";
        case 64:
            return "The server does not offer FTP over TLS. Use an ftp:// address if an "
                   "unencrypted connection is acceptable.";
        case 67:
            return "The server refused the user name or password. Check them in the "
                   "drive's settings.";
        case 79:
            return "The SSH session failed: check the user name and password, and the "
                   "port (SFTP is usually 22).";
        default: break;
    }
    switch (reply) {
        case 421: return "The server closed the connection - it may allow only so many "
                         "connections at once, or be shutting down.";
        case 425: case 426:
            return "The data connection could not be opened or was cut - often a "
                   "firewall or NAT in the way of passive mode.";
        case 450: case 550:
            return "The server refused that file or folder: it is missing, or this "
                   "account may not use it.";
        case 452: case 552:
            return "The server is out of space, or the account's quota is used up.";
        case 530:
            return "The server refused the sign-in. Check the user name and password in "
                   "the drive's settings.";
        case 553:
            return "The server does not allow that file name.";
        default: break;
    }
    return {};
}

// The failed sessions as a Markdown report, newest first.
inline std::string RenderRemoteErrorsMarkdown(const std::vector<RemoteLogSession>& sessions) {
    std::size_t failures = 0, finished = 0;
    for (const RemoteLogSession& s : sessions) {
        if (!s.Running()) ++finished;
        if (s.failed) ++failures;
    }
    std::string out = "# Remote drive errors\n\n";
    if (sessions.empty()) {
        out += "No connections yet.\n";
        return out;
    }
    if (failures == 0) {
        out += "No errors - all " + std::to_string(finished) +
               (finished == 1 ? " connection" : " connections") + " since " +
               FormatRemoteLogClock(sessions.front().startedMs) + " worked.\n";
        return out;
    }
    out += std::to_string(failures) + " of " + std::to_string(sessions.size()) +
           (sessions.size() == 1 ? " connection" : " connections") + " since " +
           FormatRemoteLogClock(sessions.front().startedMs) +
           " failed, newest first. Every step of every connection is on the "
           "*Message log* tab.\n\n";

    for (auto it = sessions.rbegin(); it != sessions.rend(); ++it) {
        const RemoteLogSession& s = *it;
        if (!s.failed) continue;
        out += "## " + EscapeRemoteLogMarkdown(FormatRemoteLogClock(s.finishedMs) + " - " +
                                               s.operation +
                                               (s.target.empty() ? "" : " " + s.target) +
                                               (s.drive.empty() ? "" : " - " + s.drive)) +
               "\n\n";
        out += "**" + EscapeRemoteLogMarkdown(s.error.empty() ? "Failed" : s.error) + "**\n\n";

        out += "| Field | Value |\n|---|---|\n";
        std::string drive = s.drive;
        if (!s.server.empty() && s.server != s.drive)
            drive += drive.empty() ? s.server : " - " + s.server;
        if (!drive.empty())
            out += "| Drive | " + EscapeRemoteLogMarkdown(drive) + " |\n";
        out += "| Operation | " +
               EscapeRemoteLogMarkdown(s.operation + (s.target.empty() ? "" : " " + s.target) +
                                       (s.background ? " (in the background)" : "")) +
               " |\n";
        if (!s.category.empty())
            out += "| Error class | " + EscapeRemoteLogMarkdown(s.category) + " |\n";
        std::string codes;
        if (const int curl = s.TransportCode(); curl != 0)
            codes += "libcurl error " + std::to_string(curl);
        if (const int reply = s.LastReplyCode(); reply != 0)
            codes += (codes.empty() ? "" : ", ") +
                     std::string(s.http ? "HTTP status " : "last server reply ") +
                     std::to_string(reply);
        if (!codes.empty()) out += "| Error codes | " + EscapeRemoteLogMarkdown(codes) + " |\n";
        if (const std::string reply = s.LastReply(); !reply.empty())
            out += std::string(s.http ? "| Last answer | " : "| Last server reply | ") +
                   EscapeRemoteLogMarkdown(reply) + " |\n";
        out += "| When | " + FormatRemoteLogClock(s.startedMs) + ", failed after " +
               FormatRemoteLogDuration(s.finishedMs - s.startedMs) + " |\n\n";

        if (const std::string hint = RemoteFailureHint(s); !hint.empty())
            out += "**Likely cause:** " + EscapeRemoteLogMarkdown(hint) + "\n\n";

        if (!s.lines.empty()) {
            out += "**Last steps**\n\n```text\n";
            const std::size_t kShown = 12;
            const std::size_t from = s.lines.size() > kShown ? s.lines.size() - kShown : 0;
            for (std::size_t i = from; i < s.lines.size(); ++i)
                out += FenceSafe(FormatRemoteLogLine(s.lines[i], s.http)) + "\n";
            out += "```\n\n";
        }
        if (!s.diagnostics.empty()) {
            out += "**Diagnostics**\n\n```text\n" + FenceSafe(s.diagnostics);
            if (s.diagnostics.back() != '\n') out += "\n";
            out += "```\n\n";
        }
    }
    return out;
}

} // namespace UltraCanvas
