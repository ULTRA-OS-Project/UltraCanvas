// Tests/FilerConnectionLogTest.cpp
// UltraFiler's connection log (Apps/UltraFiler/UltraFilerConnectionLog.h):
// the sessions the drive worker records, their bounds, and the two texts the
// log window shows - the message log and the Markdown error report with its
// codes and hints.
//
// A failed FTP connection used to leave one line in the status bar and no
// way to see the server's reply or the step it got to; what the report must
// carry for that is checked here - and, for a cloud drive, that its requests
// and HTTP statuses are not read as FTP's commands and replies.
// Version: 1.1.0
// Last Modified: 2026-10-09
// Author: UltraCanvas Framework
#include "UltraFilerConnectionLog.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace {

int failures = 0;

void Check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "  ok  " : " FAIL ", what);
    if (!ok) ++failures;
}

bool Contains(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

RemoteLogLine Line(RemoteLogLine::Kind kind, const std::string& text, int reply = 0,
                   int transport = 0) {
    RemoteLogLine l;
    l.kind = kind;
    l.text = text;
    l.replyCode = reply;
    l.transportCode = transport;
    return l;
}

// The session in the screenshot that asked for all this: PASV accepted, the
// listing command sent, and then nothing.
uint64_t AddTimedOutListing(RemoteConnectionLog& log) {
    RemoteLogSession header;
    header.drive = "riscoscloverleaf";
    header.server = "ftp://ftp.riscoscloverleaf.com";
    header.operation = "Open folder";
    header.target = "/";
    const uint64_t id = log.Begin(header);
    using K = RemoteLogLine::Kind;
    log.Append(id, Line(K::Step, "Resolving address of ftp.riscoscloverleaf.com"));
    log.Append(id, Line(K::Step, "Connecting to 169.58.121.55:21..."));
    log.Append(id, Line(K::Response, "220 Welcome", 220));
    log.Append(id, Line(K::Command, "USER erika"));
    log.Append(id, Line(K::Response, "331 Password required", 331));
    log.Append(id, Line(K::Command, "PASS ********"));
    log.Append(id, Line(K::Response, "230 Logged in", 230));
    log.Append(id, Line(K::Command, "PASV"));
    log.Append(id, Line(K::Response, "227 Entering Passive Mode (169,58,121,55,246,253)", 227));
    // libcurl asks for the transfer type between PASV and the listing.
    log.Append(id, Line(K::Command, "TYPE A"));
    log.Append(id, Line(K::Response, "200 Type set", 200));
    log.Append(id, Line(K::Step, "Retrieving directory listing..."));
    log.Append(id, Line(K::Command, "MLSD"));
    log.Append(id, Line(K::Error, "Connection timed out after 30 seconds of inactivity", 0, 28));
    log.Finish(id, true,
               "cannot list this folder: list /: Connection timed out after 30 seconds of "
               "inactivity",
               "Network - the server could not be reached, or stopped answering "
               "(UltraCloud code 4)",
               "Error: Operation too slow (libcurl error 28: Timeout was reached)\n"
               "Connected to: 169.58.121.55:21\n");
    return id;
}

} // namespace

int main() {
    std::printf("Sessions\n");
    {
        RemoteConnectionLog log;
        Check(log.SessionCount() == 0 && log.ErrorCount() == 0, "a new log is empty");
        const uint64_t before = log.Revision();
        const uint64_t id = AddTimedOutListing(log);
        Check(log.Revision() != before, "every write moves the revision on");
        Check(log.SessionCount() == 1 && log.ErrorCount() == 1, "a failed session is counted");

        const std::vector<RemoteLogSession> snap = log.Snapshot();
        Check(snap.size() == 1 && snap[0].id == id, "the snapshot holds the session");
        const RemoteLogSession& s = snap[0];
        Check(!s.Running() && s.failed, "finished and failed");
        Check(s.LastReplyCode() == 200, "the last reply is the one before the silence");
        Check(s.LastReply() == "200 Type set", "and its text");
        Check(s.EnteredPassiveMode(), "passive mode was granted on the way");
        Check(s.TransportCode() == 28, "libcurl's code comes from the error line");
        Check(s.LastCommand() == "MLSD", "the last command, without its argument");

        // Background work is logged, but a failure in it is nobody's error.
        RemoteLogSession ahead;
        ahead.operation = "Read folder ahead";
        ahead.background = true;
        const uint64_t bg = log.Begin(ahead);
        log.Finish(bg, true, "cannot list this folder", "", "");
        Check(log.SessionCount() == 2 && log.ErrorCount() == 1,
              "a failed background session is not counted as an error");

        log.Append(9999, Line(RemoteLogLine::Kind::Step, "ignored"));
        Check(log.Snapshot().size() == 2, "a line for an unknown session is dropped");

        log.Clear();
        Check(log.SessionCount() == 0 && log.ErrorCount() == 0, "Clear empties it");
    }

    std::printf("Bounds\n");
    {
        RemoteConnectionLog log;
        for (std::size_t i = 0; i < RemoteConnectionLog::kMaxSessions + 5; ++i) {
            RemoteLogSession h;
            h.operation = "Open folder";
            h.target = "/" + std::to_string(i);
            log.Finish(log.Begin(h), false, "", "", "");
        }
        const std::vector<RemoteLogSession> snap = log.Snapshot();
        Check(snap.size() == RemoteConnectionLog::kMaxSessions, "the session count is capped");
        Check(snap.front().target == "/5", "the oldest sessions are the ones dropped");

        RemoteLogSession h;
        const uint64_t id = log.Begin(h);
        for (std::size_t i = 0; i < RemoteConnectionLog::kMaxLinesPerSession + 10; ++i)
            log.Append(id, Line(RemoteLogLine::Kind::Response, "150 data", 150));
        log.Append(id, Line(RemoteLogLine::Kind::Error, "the end", 0, 28));
        const RemoteLogSession last = log.Snapshot().back();
        Check(last.droppedLines == 10, "lines beyond the cap are counted, not kept");
        Check(!last.lines.empty() && last.lines.back().text == "the end",
              "the error line is kept even past the cap");
    }

    std::printf("Message log\n");
    {
        RemoteConnectionLog log;
        Check(Contains(RenderRemoteLogText(log.Snapshot()), "No connections yet"),
              "an empty log says what it will hold");
        AddTimedOutListing(log);
        RemoteLogSession ok;
        ok.drive = "NAS";
        ok.operation = "Upload";
        ok.target = "/backup/a.zip";
        const uint64_t okId = log.Begin(ok);
        log.Finish(okId, false, "", "", "");
        RemoteLogSession running;
        running.drive = "NAS";
        running.operation = "Open folder";
        running.target = "/videos";
        const uint64_t runId = log.Begin(running);
        log.Append(runId, Line(RemoteLogLine::Kind::Step, "Connecting to 10.0.0.2:21..."));

        const std::string text = RenderRemoteLogText(log.Snapshot());
        Check(Contains(text, "Open folder / on riscoscloverleaf (ftp://ftp.riscoscloverleaf.com)"),
              "a session is headed by what it was, where");
        Check(Contains(text, "Response: 227 Entering Passive Mode"), "replies are listed");
        Check(Contains(text, "Command:  TYPE A"), "every command is listed");
        Check(Contains(text, "Command:  PASS ********"), "commands are listed, password masked");
        Check(Contains(text, "Error:    Connection timed out after 30 seconds of inactivity"
                             "  [libcurl error 28]"),
              "the error line carries libcurl's code");
        Check(Contains(text, "Failed after"), "a failed session says so");
        Check(Contains(text, "Done in"), "a finished one says how long it took");
        Check(Contains(text, "Status:   Connecting to 10.0.0.2:21..."),
              "a running session shows its steps so far");
        // A running session is the end of the text: what comes next is only
        // ever appended.
        const std::string tail = "Connecting to 10.0.0.2:21...\n";
        Check(text.size() >= tail.size() &&
              text.compare(text.size() - tail.size(), tail.size(), tail) == 0,
              "the running session closes the text, open");
    }

    std::printf("Error report\n");
    {
        RemoteConnectionLog log;
        Check(Contains(RenderRemoteErrorsMarkdown(log.Snapshot()), "No connections yet"),
              "an empty report says so");
        RemoteLogSession fine;
        fine.operation = "Open folder";
        log.Finish(log.Begin(fine), false, "", "", "");
        Check(Contains(RenderRemoteErrorsMarkdown(log.Snapshot()), "No errors"),
              "a log without failures says none failed");

        AddTimedOutListing(log);
        const std::string md = RenderRemoteErrorsMarkdown(log.Snapshot());
        Check(Contains(md, "1 of 2 connections"), "the count of failures");
        Check(Contains(md, "**cannot list this folder: list /: Connection timed out after 30 "
                           "seconds of inactivity**"),
              "the message, in bold");
        Check(Contains(md, "| Error codes | libcurl error 28, last server reply 200 |"),
              "both codes in the table");
        Check(Contains(md, "| Last server reply | 200 Type set |"),
              "the last reply in the table");
        Check(Contains(md, "(UltraCloud code 4)"), "the error class with its code");
        Check(Contains(md, "**Likely cause:**") && Contains(md, "firewall"),
              "a timeout after PASV points at the data connection");
        Check(Contains(md, "```text\n") && Contains(md, "Command:  MLSD"),
              "the last steps as a code block");
        Check(Contains(md, "**Diagnostics**") && Contains(md, "Connected to: 169.58.121.55:21"),
              "the diagnostics chain");
    }

    std::printf("Hints\n");
    {
        RemoteLogSession s;
        s.lines.push_back(Line(RemoteLogLine::Kind::Response, "530 Login incorrect.", 530));
        s.lines.push_back(Line(RemoteLogLine::Kind::Error, "Access denied", 0, 67));
        Check(Contains(RemoteFailureHint(s), "user name or password"), "a refused sign-in");

        RemoteLogSession dns;
        dns.lines.push_back(Line(RemoteLogLine::Kind::Error, "Could not resolve host", 0, 6));
        Check(Contains(RemoteFailureHint(dns), "name could not be found"), "an unknown host");

        RemoteLogSession refused;
        refused.lines.push_back(Line(RemoteLogLine::Kind::Error, "Couldn't connect", 0, 7));
        Check(Contains(RemoteFailureHint(refused), "Nothing answered"), "a closed port");

        RemoteLogSession denied;
        denied.lines.push_back(Line(RemoteLogLine::Kind::Response, "550 Permission denied", 550));
        denied.lines.push_back(Line(RemoteLogLine::Kind::Error, "RETR response: 550", 0, 19));
        Check(Contains(RemoteFailureHint(denied), "refused that file or folder"),
              "a 550 from the server");

        RemoteLogSession plain;
        Check(RemoteFailureHint(plain).empty(), "nothing to say, nothing said");
    }

    std::printf("Cloud drives\n");
    {
        // A Dropbox listing the service refused: requests, not commands, and
        // HTTP statuses, not FTP replies.
        RemoteConnectionLog log;
        RemoteLogSession header;
        header.drive = "Dropbox";
        header.server = "dropbox";
        header.operation = "Open folder";
        header.target = "/Photos";
        header.http = true;
        const uint64_t id = log.Begin(header);
        using K = RemoteLogLine::Kind;
        log.Append(id, Line(K::Step, "The access token has expired - renewing it"));
        log.Append(id, Line(K::Step, "Access token renewed"));
        log.Append(id, Line(K::Command, "POST https://api.dropboxapi.com/2/files/list_folder"));
        log.Append(id, Line(K::Response, "409 Conflict - path/not_found (0.31 s)", 409));
        log.Finish(id, true, "list /Photos: HTTP 409 - path/not_found",
                   "Server - the service refused the request (UltraCloud code 5)",
                   "HTTP status: 409 Conflict\nService's reason: path/not_found\n"
                   "Request ID: 5f1c0de\n");

        const std::string text = RenderRemoteLogText(log.Snapshot());
        Check(Contains(text, "Request:  POST https://api.dropboxapi.com/2/files/list_folder"),
              "a cloud drive's requests are labelled as requests");
        Check(!Contains(text, "Command:  POST"), "not as FTP commands");
        Check(Contains(text, "Response: 409 Conflict - path/not_found"),
              "the answer with the service's reason");
        Check(Contains(text, "Status:   Access token renewed"), "a renewed sign-in is a step");

        const std::string md = RenderRemoteErrorsMarkdown(log.Snapshot());
        Check(Contains(md, "| Error codes | HTTP status 409 |"),
              "the code is an HTTP status, not a server reply");
        Check(Contains(md, "| Last answer | 409 Conflict - path/not\\_found (0.31 s) |"),
              "the last answer in the table");
        Check(!Contains(md, "Last server reply"), "no FTP wording in a cloud report");
        Check(Contains(md, "Request:  POST"), "the last steps keep the request label");
        Check(Contains(md, "Request ID: 5f1c0de"), "the diagnostics carry the request ID");
        Check(Contains(md, "**Likely cause:**") && Contains(md, "conflict"),
              "a 409 is explained as a conflict");

        // The same numbers mean other things in FTP: an HTTP 425 is not a
        // data connection, a 421 not a server hanging up.
        RemoteLogSession early;
        early.http = true;
        early.lines.push_back(Line(K::Response, "425 Too Early", 425));
        Check(RemoteFailureHint(early).empty(), "an HTTP 425 gets no FTP hint");
        RemoteLogSession ftp425;
        ftp425.lines.push_back(Line(K::Response, "425 Can't open data connection", 425));
        Check(Contains(RemoteFailureHint(ftp425), "data connection"),
              "while FTP's 425 keeps its own");

        RemoteLogSession expired;
        expired.http = true;
        expired.lines.push_back(Line(K::Response, "401 Unauthorized - invalid_token", 401));
        Check(Contains(RemoteFailureHint(expired), "Sign in to the drive again"),
              "a 401 asks for a new sign-in");

        RemoteLogSession forbidden;
        forbidden.http = true;
        forbidden.lines.push_back(Line(K::Response, "403 Forbidden - accessDenied", 403));
        Check(Contains(RemoteFailureHint(forbidden), "may not use"), "a 403 is a refusal");

        RemoteLogSession throttled;
        throttled.http = true;
        throttled.lines.push_back(Line(K::Response, "429 Too Many Requests", 429));
        Check(Contains(RemoteFailureHint(throttled), "limiting"), "a 429 is throttling");

        // Google Drive throttles with a 403; the message says it is limiting.
        RemoteLogSession google;
        google.http = true;
        google.error = "list /: the service is limiting requests - it asks to wait 30 s "
                       "(HTTP 403 - userRateLimitExceeded: User Rate Limit Exceeded)";
        google.lines.push_back(Line(K::Response, "403 Forbidden - userRateLimitExceeded", 403));
        Check(Contains(RemoteFailureHint(google), "limiting"),
              "Google's throttling 403 is not read as a refusal");

        RemoteLogSession full;
        full.http = true;
        full.lines.push_back(Line(K::Response, "507 Insufficient Storage", 507));
        Check(Contains(RemoteFailureHint(full), "storage is used up"), "a full drive");

        RemoteLogSession offline;
        offline.http = true;
        offline.failed = true;
        offline.lines.push_back(
                Line(K::Command, "PROPFIND https://cloud.example.org/remote.php/dav/files/erika/"));
        offline.lines.push_back(Line(K::Error, "No answer: Could not resolve host (after 12 ms)"));
        Check(Contains(RemoteFailureHint(offline), "could not be reached"),
              "a request with no answer at all");

        RemoteLogSession answered;
        answered.http = true;
        answered.lines.push_back(Line(K::Response, "200 OK - 420 ms", 200));
        Check(RemoteFailureHint(answered).empty(), "nothing to explain about a 200");
    }

    std::printf("Formatting\n");
    {
        Check(FormatRemoteLogDuration(850) == "850 ms", "milliseconds");
        Check(FormatRemoteLogDuration(31234) == "31.2 s", "seconds with a tenth, a dot");
        Check(FormatRemoteLogDuration(245000) == "4 min 05 s", "minutes");
        Check(EscapeRemoteLogMarkdown("a|b *c* $5 [x]") == "a\\|b \\*c\\* \\$5 \\[x\\]",
              "table and emphasis characters are escaped");
        Check(FenceSafe("x ``` y") == "x ''' y", "a fence inside a code block is defused");
        Check(FormatRemoteLogClock(0).size() == 8, "a clock is HH:MM:SS");
    }

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
