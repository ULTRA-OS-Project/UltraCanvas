// include/UltraNet/UltraNetCurlDebug.h
// Opt-in libcurl protocol tracing for the mail plug-ins.
//
// A mail account that will not send or fetch is almost never debuggable from
// the UltraNetResultCode alone: the server's own reply text ("535 5.7.8
// Username and Password not accepted", "555 5.5.2 Syntax error") carries the
// answer, and libcurl throws it away once it has mapped the exchange to a
// CURLcode. Setting ULTRANET_CURL_DEBUG puts that conversation on stderr.
//
// Off unless asked for: the trace is a user's mail session, so it is never
// written by default, the message bodies are never written at all, and the
// SASL exchange is redacted - a trace a user pastes into a bug report must not
// carry their password.
//
// Header-only, like UltraNetMailAddr.h, so the separately linked plug-in DSOs
// do not have to pull a core object in for it.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <curl/curl.h>

namespace ultranet_curldebug {

// True when ULTRANET_CURL_DEBUG is set to anything but a spelling of "off".
// Read on each call rather than cached: a session can be traced by setting the
// variable and reconnecting, without restarting the application.
inline bool Requested() {
    const char* value = std::getenv("ULTRANET_CURL_DEBUG");
    if (!value || !*value) return false;
    return !(std::strcmp(value, "0") == 0 || std::strcmp(value, "false") == 0 ||
             std::strcmp(value, "no") == 0 || std::strcmp(value, "off") == 0);
}

namespace detail {

inline bool StartsWithNoCase(const std::string& line, const char* prefix) {
    const std::size_t n = std::strlen(prefix);
    if (line.size() < n) return false;
    for (std::size_t i = 0; i < n; ++i) {
        const char a = line[i] >= 'A' && line[i] <= 'Z' ? char(line[i] + 32) : line[i];
        const char b = prefix[i] >= 'A' && prefix[i] <= 'Z' ? char(prefix[i] + 32) : prefix[i];
        if (a != b) return false;
    }
    return true;
}

// A line that is nothing but base64 is a SASL exchange: the second and third
// lines of AUTH LOGIN are the bare base64 of the username and the password,
// with no keyword on them to match. In an SMTP/IMAP/POP3 command stream
// nothing else looks like this, and over-redacting a debug trace costs
// nothing next to printing a password.
inline bool LooksLikeSaslToken(const std::string& line) {
    if (line.size() < 8) return false;
    for (char c : line) {
        const bool base64Char =
            (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=';
        if (!base64Char) return false;
    }
    return true;
}

} // namespace detail

// Replaces the credential in one traced protocol line with "<redacted>",
// leaving the keyword so the trace still shows which step failed. Handles the
// keyword forms (SMTP AUTH, IMAP LOGIN/AUTHENTICATE, POP3 USER/PASS, an
// Authorization: header) and the bare base64 continuation lines of AUTH LOGIN.
inline std::string Redact(const std::string& line) {
    static const char* const kKeywords[] = {
        "AUTH ", "AUTHENTICATE ", "LOGIN ", "USER ", "PASS ", "Authorization: ",
    };
    for (const char* keyword : kKeywords) {
        if (!detail::StartsWithNoCase(line, keyword)) continue;
        std::string head = line.substr(0, std::strlen(keyword));
        // "AUTH PLAIN <token>" / "AUTHENTICATE XOAUTH2 <token>": the mechanism
        // is not a secret and is the useful half of the line, so keep it and
        // cut what follows. A bare "AUTH LOGIN" carries no credential at all -
        // the tokens come on the next lines - so it is left as it is.
        if (detail::StartsWithNoCase(line, "AUTH ") ||
            detail::StartsWithNoCase(line, "AUTHENTICATE ")) {
            const std::size_t mechEnd = line.find(' ', head.size());
            if (mechEnd == std::string::npos) return line;
            head = line.substr(0, mechEnd + 1);
        }
        return head + "<redacted>";
    }
    // IMAP tags the command: "a003 LOGIN user pass".
    const std::size_t space = line.find(' ');
    if (space != std::string::npos && space + 1 < line.size()) {
        const std::string rest = line.substr(space + 1);
        if (detail::StartsWithNoCase(rest, "LOGIN ") ||
            detail::StartsWithNoCase(rest, "AUTHENTICATE ")) {
            return line.substr(0, space + 1) + Redact(rest);
        }
    }
    if (detail::LooksLikeSaslToken(line)) return "<redacted>";
    return line;
}

namespace detail {

inline int TraceCallback(CURL* /*handle*/, curl_infotype type, char* data,
                         std::size_t size, void* /*userdata*/) {
    const char* marker = nullptr;
    switch (type) {
        case CURLINFO_TEXT:       marker = "*"; break;
        case CURLINFO_HEADER_OUT: marker = ">"; break;
        case CURLINFO_HEADER_IN:  marker = "<"; break;
        // The payload is the user's mail. Its size is worth seeing; its
        // contents are not ours to print.
        case CURLINFO_DATA_OUT:
        case CURLINFO_DATA_IN:
        case CURLINFO_SSL_DATA_OUT:
        case CURLINFO_SSL_DATA_IN:
        default:
            return 0;
    }

    std::string text(data, size);
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) {
            const std::string safe = Redact(line);
            std::fprintf(stderr, "[ultranet] %s %s\n", marker, safe.c_str());
        }
        start = end + 1;
    }
    std::fflush(stderr);
    return 0;
}

} // namespace detail

// Turns tracing on for one easy handle when ULTRANET_CURL_DEBUG asks for it;
// a no-op otherwise, so call sites need no condition of their own.
inline void EnableIfRequested(CURL* handle) {
    if (!handle || !Requested()) return;
    curl_easy_setopt(handle, CURLOPT_DEBUGFUNCTION, detail::TraceCallback);
    curl_easy_setopt(handle, CURLOPT_VERBOSE, 1L);
}

} // namespace ultranet_curldebug
