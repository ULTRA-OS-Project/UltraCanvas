// core/UltraNet/UltraNetFtpLog.h
// The message log of an FTP / SFTP call: what libcurl says it is doing, the
// commands it sends and the server's replies, turned into the lines an FTP
// client shows while it connects -
//
//   Status:   Resolving address of ftp.example.com
//   Status:   Connecting to 203.0.113.7:21...
//   Status:   Connection established, waiting for welcome message...
//   Response: 220 Welcome
//   Command:  USER erika
//   Response: 331 Password required
//   Command:  PASS ********
//   Response: 230 Logged in
//   Status:   Logged in
//   Command:  PASV
//   Response: 227 Entering Passive Mode (203,0,113,7,246,253)
//   Status:   Retrieving directory listing...
//   Command:  MLSD
//   Error:    Connection timed out after 30 seconds of inactivity
//
// Before this, a failed call said "Timeout was reached" and nothing else: no
// address, no reply, no word on which step it got to. The log is what turns
// that into something a user can act on - or forward to whoever runs the
// server.
//
// What is never logged: the password. "PASS secret" goes out as
// "PASS ********", and so does ACCT. Payload (the listing, the file) is not a
// log line either.
//
// Header-only and free of libcurl: UltraNetFtp.cpp maps libcurl's debug
// stream onto Channel, and Tests/UltraNet/test_ftp_log.cpp feeds a recorded
// transcript straight in.
// A call that takes up a connection an earlier call left open says so
// ("Using the open connection to ftp.example.com - already logged in")
// instead of resolving and logging in again.
// Version: 1.1.0 - a connection kept open and used again
// Last Modified: 2026-10-09
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraNet/UltraNetFtp.h"

#include <cctype>
#include <string>
#include <utility>
#include <vector>

namespace ultranet_internal::ftplog {

// Which part of libcurl's debug stream a chunk came from.
enum class Channel {
    Text,       // libcurl's own commentary (CURLINFO_TEXT)
    Sent,       // a command to the server (CURLINFO_HEADER_OUT)
    Received    // a reply from the server (CURLINFO_HEADER_IN)
};

inline std::string Upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

inline bool StartsWith(const std::string& s, const char* prefix) {
    return s.rfind(prefix, 0) == 0;
}

// The lines of one chunk, without their line endings and without blank ones.
// libcurl hands the debug callback whole lines; a chunk that holds several
// (a multi-line reply, a block of commentary) is split here.
inline std::vector<std::string> SplitChunk(const std::string& chunk) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= chunk.size()) {
        std::size_t nl = chunk.find('\n', start);
        std::string line = chunk.substr(start, nl == std::string::npos ? std::string::npos
                                                                      : nl - start);
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n' ||
                                 line.back() == ' ' || line.back() == '\t'))
            line.pop_back();
        std::size_t lead = 0;
        while (lead < line.size() && (line[lead] == ' ' || line[lead] == '\t')) ++lead;
        if (lead < line.size()) out.push_back(line.substr(lead));
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return out;
}

// A command as it may be shown: the password of PASS (and ACCT, which some
// servers take a second secret through) replaced by asterisks.
inline std::string RedactCommand(const std::string& line) {
    const std::string up = Upper(line);
    if (up == "PASS" || up == "ACCT" || StartsWith(up, "PASS ") || StartsWith(up, "ACCT "))
        return line.substr(0, 4) + " ********";
    return line;
}

// The three-digit code a reply line starts with ("227 Entering ..." -> 227,
// "230-Welcome" -> 230), or 0 for a line that carries none - the middle of a
// multi-line reply.
inline int ReplyCode(const std::string& line) {
    if (line.size() < 3) return 0;
    for (int i = 0; i < 3; ++i)
        if (!std::isdigit(static_cast<unsigned char>(line[i]))) return 0;
    if (line.size() > 3 && line[3] != ' ' && line[3] != '-') return 0;
    return (line[0] - '0') * 100 + (line[1] - '0') * 10 + (line[2] - '0');
}

// "ftp://user:pw@files.example.org:2121/pub/" -> "files.example.org". What
// the first status line names; empty when the URL has no host.
inline std::string HostOfUrl(const std::string& url) {
    std::size_t start = url.find("://");
    start = start == std::string::npos ? 0 : start + 3;
    std::size_t end = url.find('/', start);
    if (end == std::string::npos) end = url.size();
    std::string authority = url.substr(start, end - start);
    const std::size_t at = authority.rfind('@');
    if (at != std::string::npos) authority.erase(0, at + 1);
    if (!authority.empty() && authority.front() == '[') {
        const std::size_t close = authority.find(']');
        return close == std::string::npos ? authority : authority.substr(1, close - 1);
    }
    const std::size_t colon = authority.find(':');
    if (colon != std::string::npos) authority.erase(colon);
    return authority;
}

// libcurl's commentary that describes its own bookkeeping rather than the
// connection: true for a line the log leaves out.
inline bool IsNoise(const std::string& text) {
    static const char* const kNoise[] = {
        "Maxdownload",                 // "Maxdownload = -1"
        "Remembering we are in dir",
        "ftp_perform ends",
        "Request has same path",
        "Expire in",                   // debug builds of libcurl
        "STATE:",
        "Curl_",
        "multi_done",
        "Getting file with size",      // repeats what the reply already said
        // The connection pool looking for a connection to take up again
        // ("Found bundle for host: 0x5581... [serially]", libcurl 8.x before
        // 8.10): said before "Re-using existing connection", which is the
        // line the log reports.
        "Found bundle for host",
        "Can not multiplex",
        // The list of hosts that bypass a proxy, often hundreds of characters;
        // a proxy that IS used is still named ("Uses proxy env variable
        // ftp_proxy == ...").
        "Uses proxy env variable no_proxy",
    };
    for (const char* n : kNoise)
        if (StartsWith(text, n)) return true;
    return false;
}

// "Connection timed out after 30 seconds of inactivity", the way an FTP
// client words it. libcurl says "Operation too slow. Less than 1 bytes/sec
// transferred the last 30 seconds" when the data stops moving, and "server
// response timeout" when a reply does not come; both are the same thing to
// the person waiting.
inline std::string InactivityMessage(int seconds) {
    return "Connection timed out after " + std::to_string(seconds) +
           (seconds == 1 ? " second" : " seconds") + " of inactivity";
}

inline bool IsInactivityDetail(const std::string& detail) {
    const std::string up = Upper(detail);
    return StartsWith(up, "OPERATION TOO SLOW") ||
           up.find("RESPONSE TIMEOUT") != std::string::npos;
}

// The message a failed call reports. `detail` is libcurl's specific reason
// (its error buffer); the server's own refusal is added when the last reply
// was one (4xx / 5xx) and the detail does not already quote it, because
// "RETR response: 550" leaves out the half that says why: "550 Permission
// denied".
inline std::string FailureMessage(const std::string& detail, int inactivitySeconds,
                                  int lastReplyCode, const std::string& lastReply) {
    std::string message = (inactivitySeconds > 0 && IsInactivityDetail(detail))
            ? InactivityMessage(inactivitySeconds)
            : detail;
    if (lastReplyCode >= 400 && !lastReply.empty() &&
        message.find(lastReply) == std::string::npos) {
        message += message.empty() ? lastReply : " - the server said \"" + lastReply + "\"";
    }
    return message;
}

// One call's log: libcurl's stream in, UltraNetFtpLogLines out, with the
// steps an FTP client says in its own words added where the stream only
// implies them ("Logged in" after a 230, "Retrieving directory listing..."
// before MLSD / LIST / NLST). With nobody to tell it emits nothing, but it
// still follows the replies, so LastReply() is there for the failure message.
class Transcript {
public:
    Transcript(UltraNetFtpLogCallback sink, bool sftp)
        : sink_(std::move(sink)), sftp_(sftp) {}

    bool Active() const { return static_cast<bool>(sink_); }

    // A transfer for `url` is about to start: one per libcurl handle, so a
    // listing that has to ask twice says so twice. "Resolving address of"
    // is held back until libcurl says what it does first: a connection kept
    // open by an earlier call is used again without resolving or logging in,
    // and the log must not claim it was.
    void Begin(const std::string& url) {
        controlConnected_ = false;
        loggedIn_ = false;
        authRefused_ = false;
        tlsUp_ = false;
        pendingHost_ = HostOfUrl(url);
    }

    // Read whether or not anyone listens: the last reply is what a failure
    // message quotes, and that is owed to every caller.
    void Feed(Channel channel, const std::string& chunk) {
        for (const std::string& line : SplitChunk(chunk)) {
            switch (channel) {
                case Channel::Text:     OnText(line); break;
                case Channel::Sent:     OnSent(line); break;
                case Channel::Received: OnReceived(line); break;
            }
        }
    }

    void Step(const std::string& text) { Emit(UltraNetFtpLogKind::Step, text); }

    // The last line of a failed call.
    void Fail(const std::string& message, UltraNetResultCode code, int transportCode) {
        if (!sink_) return;
        SayResolving();
        UltraNetFtpLogLine l;
        l.kind = UltraNetFtpLogKind::Error;
        l.text = message;
        l.resultCode = code;
        l.transportCode = transportCode;
        sink_(l);
    }

    int LastReplyCode() const { return lastReplyCode_; }
    const std::string& LastReply() const { return lastReply_; }
    // The verb of the last command sent ("MLSD", "TYPE"), in upper case: what
    // the last reply answered.
    const std::string& LastCommand() const { return lastVerb_; }

private:
    // The step Begin held back, said before anything else is: whatever
    // libcurl reports first, other than taking up a kept connection, belongs
    // to a connection being made.
    void SayResolving() {
        if (pendingHost_.empty()) return;
        const std::string host = std::move(pendingHost_);
        pendingHost_.clear();
        Emit(UltraNetFtpLogKind::Step, "Resolving address of " + host);
    }

    void Emit(UltraNetFtpLogKind kind, const std::string& text, int replyCode = 0) {
        if (!sink_ || text.empty()) return;
        SayResolving();
        UltraNetFtpLogLine l;
        l.kind = kind;
        l.text = text;
        l.replyCode = replyCode;
        sink_(l);
    }

    void OnText(const std::string& line) {
        if (IsNoise(line)) return;
        // A connection an earlier call left open, taken up again: already
        // connected and logged in, so the next connection libcurl reports is
        // the data connection. Worded "Re-using existing connection ..." up
        // to libcurl 8.x and "Reusing existing ftp: connection ..." from
        // 8.21, the vendored third_party/curl.
        if (StartsWith(line, "Re-using existing") || StartsWith(line, "Reusing existing")) {
            const std::string host = std::move(pendingHost_);
            pendingHost_.clear();
            controlConnected_ = true;
            loggedIn_ = true;
            Step(host.empty() ? "Using the open connection - already logged in"
                              : "Using the open connection to " + host +
                                " - already logged in");
            return;
        }
        // "Connection #0 to host h left intact": what makes the line above
        // possible next time.
        if (StartsWith(line, "Connection #") && line.find("left intact") != std::string::npos) {
            Step("Connection kept open for the next request");
            return;
        }
        // "Trying 203.0.113.7:21..." is the connect starting.
        if (StartsWith(line, "Trying ")) {
            Step("Connecting to " + line.substr(7));
            return;
        }
        // The first connection made is the control connection; any after it
        // carries the listing or the file. libcurl has worded this three
        // ways: "Connected to ..." for each (7.x), "Connected 2nd connection
        // to ..." for the data connection (8.x), and from 8.21 "Established
        // connection to h (ip port n) from ..." / "Established 2nd connection
        // to ..." - the only wording the vendored third_party/curl uses.
        if (StartsWith(line, "Connected to ") || StartsWith(line, "Connected 2nd connection") ||
            StartsWith(line, "Established connection to ") ||
            StartsWith(line, "Established 2nd connection to ")) {
            if (!controlConnected_) {
                controlConnected_ = true;
                Step(sftp_ ? "Connection established, starting the SSH session..."
                             : "Connection established, waiting for welcome message...");
            } else {
                Step("Data connection established");
            }
            return;
        }
        if (line.find("SSL connection using") != std::string::npos ||
            line.find("TLS connection using") != std::string::npos)
            tlsUp_ = true;
        Step(line);
        // SFTP has no 230 to read: libcurl says it in words.
        if (sftp_ && !loggedIn_ && StartsWith(line, "Authentication complete")) {
            loggedIn_ = true;
            Step("Logged in");
        }
    }

    void OnSent(const std::string& line) {
        const std::string verb = Upper(line.substr(0, line.find(' ')));
        if (verb == "USER" && authRefused_ && !tlsUp_)
            Step("Insecure server, it does not support FTP over TLS");
        if (verb == "MLSD" || verb == "LIST" || verb == "NLST")
            Step("Retrieving directory listing...");
        else if (verb == "RETR")
            Step("Starting download");
        else if (verb == "STOR" || verb == "APPE")
            Step("Starting upload");
        lastVerb_ = verb;
        Emit(UltraNetFtpLogKind::Command, RedactCommand(line));
    }

    void OnReceived(const std::string& line) {
        const int code = ReplyCode(line);
        Emit(UltraNetFtpLogKind::Response, line, code);
        // Only the last line of a reply ("230 ...", not "230-...") ends it;
        // the steps follow the reply, not its first line.
        if (code == 0 || (line.size() > 3 && line[3] == '-')) return;
        lastReplyCode_ = code;
        lastReply_ = line;
        if (lastVerb_ == "AUTH" && code >= 400) authRefused_ = true;
        if (code == 230 && !loggedIn_) {
            loggedIn_ = true;
            Step("Logged in");
        } else if (code == 226 || code == 250) {
            if (lastVerb_ == "MLSD" || lastVerb_ == "LIST" || lastVerb_ == "NLST")
                Step("Directory listing successful");
            else if (lastVerb_ == "RETR" || lastVerb_ == "STOR" || lastVerb_ == "APPE")
                Step("File transfer successful");
        }
    }

    UltraNetFtpLogCallback sink_;
    bool sftp_ = false;
    bool controlConnected_ = false;
    bool loggedIn_ = false;
    bool authRefused_ = false;
    bool tlsUp_ = false;
    // The host Begin named, until "Resolving address of" has been said.
    std::string pendingHost_;
    std::string lastVerb_;
    int lastReplyCode_ = 0;
    std::string lastReply_;
};

} // namespace ultranet_internal::ftplog
