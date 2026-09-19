// include/UltraNet/UltraNetCurlDebug.h
// Opt-in libcurl wire tracing for the UltraNet protocol plug-ins (SMTP / IMAP /
// POP3 / ...). Header-only so each loadable plug-in DSO can pull it in without a
// shared library dependency.
//
// Enabled at runtime by exporting a truthy ULTRANET_CURL_VERBOSE (anything other
// than unset / empty / "0"). When on, the handle logs curl's own commentary plus
// the protocol command/response lines to stderr, prefixed "[ultranet]". The
// outbound SASL / AUTH line (which carries the XOAUTH2 bearer token or the
// account password) is redacted so secrets never reach the log — the inbound
// server responses, which are what actually reveal where a session fails
// (TLS handshake, "334"/"535" on AUTH, MAIL FROM / RCPT / DATA), are kept.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <curl/curl.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace ultranet_curldebug {

// True when a line mentions authentication — the AUTH / AUTHENTICATE command or
// an XOAUTH2 / bearer marker. On its own this is only a keyword match; whether a
// line is actually redacted is decided by ShouldRedact below (direction-aware).
inline bool HasAuthKeyword(const std::string& line) {
    std::string up;
    up.reserve(line.size());
    for (char c : line) up.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    return up.find("AUTH")    != std::string::npos ||
           up.find("XOAUTH2") != std::string::npos ||
           up.find("BEARER")  != std::string::npos;
}

// The full redaction rule. Only client->server (HEADER_OUT) traffic can carry a
// secret, so inbound server responses are always kept — including the EHLO
// capability line ("250 AUTH ... XOAUTH2") and the "334" AUTH error challenge,
// which are exactly what a trace is read for. Within outbound traffic, redact the
// AUTH command and any line long enough to be a base64 credential blob (a bare
// SASL continuation has no keyword to match on, but dwarfs any real command).
inline bool ShouldRedact(curl_infotype type, const std::string& line) {
    if (type != CURLINFO_HEADER_OUT) return false;
    return HasAuthKeyword(line) || line.size() > 120;
}

inline int DebugCallback(CURL*, curl_infotype type, char* data, size_t size, void*) {
    const char* tag = nullptr;
    switch (type) {
        case CURLINFO_TEXT:       tag = "* "; break;  // curl's own commentary (connect, TLS)
        case CURLINFO_HEADER_IN:  tag = "< "; break;  // server -> client (responses)
        case CURLINFO_HEADER_OUT: tag = "> "; break;  // client -> server (commands)
        default: return 0;  // DATA_IN / DATA_OUT / SSL_DATA: payload / binary, skip
    }
    const std::string chunk(data, size);
    size_t start = 0;
    while (start < chunk.size()) {
        const size_t nl = chunk.find('\n', start);
        std::string line = chunk.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (!line.empty()) {
            if (ShouldRedact(type, line))
                std::fprintf(stderr, "[ultranet] %s<redacted auth line>\n", tag);
            else
                std::fprintf(stderr, "[ultranet] %s%s\n", tag, line.c_str());
        }
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return 0;
}

// Turn on CURLOPT_VERBOSE + the redacting debug callback when the environment
// asks for it. No-op otherwise, so it is safe to call unconditionally on every
// handle.
inline void EnableIfRequested(CURL* h) {
    const char* v = std::getenv("ULTRANET_CURL_VERBOSE");
    if (!v || !*v || (v[0] == '0' && v[1] == '\0')) return;
    curl_easy_setopt(h, CURLOPT_DEBUGFUNCTION, &DebugCallback);
    curl_easy_setopt(h, CURLOPT_VERBOSE, 1L);
}

} // namespace ultranet_curldebug
