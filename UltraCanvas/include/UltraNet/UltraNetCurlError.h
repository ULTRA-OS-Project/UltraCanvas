// include/UltraNet/UltraNetCurlError.h
// curl_easy_perform with the specific failure reason kept, plus the full chain
// of what was running when it failed. curl_easy_strerror only names the error
// class ("SSL peer certificate or SSH remote key was not OK"); the per-transfer
// error buffer says what actually went wrong ("SSL certificate problem: unable
// to get local issuer certificate", "schannel: the certificate or certificate
// chain is based on an untrusted root", ...). And even that does not say which
// component, protocol, TLS mode, libraries, versions and trust roots were in
// play - which is what finding the cause needs, and the only trace a Windows
// GUI build leaves (it has no stderr for ULTRANET_CURL_VERBOSE). That chain
// goes into UltraNetResult::diagnostics, one "Name: value" line each.
// Header-only so each loadable plug-in DSO can use it.
//
// Version: 0.2.0 - UltraNetResult::diagnostics: the full connection chain
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <UltraNet/UltraNetCore.h>   // UltraNetResult, UltraNet_Describe*

#include <curl/curl.h>

#include <string>

namespace ultranet_curlerror {

// What a plug-in knows about the connection it is about to run and curl does
// not report back: which component, how TLS starts, how it signs in. Set by
// the plug-in when it configures a handle (CurrentContext() = ...), read by
// Perform on failure. Per thread: a plug-in operation configures its handle
// and runs it on one thread, one operation after another.
struct Context {
    std::string component;   // "UltraNet IMAP plug-in 0.2.0"
    std::string tls;         // "implicit (TLS from connect)", "STARTTLS required", ...
    std::string signIn;      // "Automatic, password", "OAuth2 (XOAUTH2 / OAUTHBEARER), token"
};

inline Context& CurrentContext() {
    thread_local Context ctx;
    return ctx;
}

// "imaps://user:secret@host:993/INBOX" -> "imaps://host:993/INBOX": a URL may
// carry the credentials in its userinfo, and diagnostics are shown and copied.
inline std::string RedactUrl(const std::string& url) {
    const std::size_t scheme = url.find("://");
    if (scheme == std::string::npos) return url;
    const std::size_t hostStart = scheme + 3;
    const std::size_t pathStart = url.find('/', hostStart);
    const std::size_t at = url.rfind('@', pathStart == std::string::npos ? url.size() : pathStart);
    if (at == std::string::npos || at < hostStart) return url;
    return url.substr(0, hostStart) + url.substr(at + 1);
}

// The chain for a failed transfer on `h`, one "Name: value" line each.
inline std::string Diagnostics(CURL* h, CURLcode rc, const std::string& reason) {
    const Context& ctx = CurrentContext();
    std::string d;
    auto line = [&d](const char* name, const std::string& value) {
        if (value.empty()) return;
        d += name;
        d += ": ";
        d += value;
        d += '\n';
    };

    line("Error", reason + " (libcurl error " + std::to_string(static_cast<int>(rc)) +
                  ": " + curl_easy_strerror(rc) + ")");
    line("Component", ctx.component);

    char* url = nullptr;
    if (curl_easy_getinfo(h, CURLINFO_EFFECTIVE_URL, &url) == CURLE_OK && url)
        line("Server", RedactUrl(url));
    char* ip = nullptr;
    long port = 0;
    curl_easy_getinfo(h, CURLINFO_PRIMARY_IP, &ip);
    curl_easy_getinfo(h, CURLINFO_PRIMARY_PORT, &port);
    line("Connected to", (ip && *ip) ? std::string(ip) + ":" + std::to_string(port)
                                     : std::string("no connection was made"));
    line("TLS", ctx.tls);
    line("Sign-in", ctx.signIn);

    const curl_version_info_data* info = curl_version_info(CURLVERSION_NOW);
    if (info) {
        std::string lib = std::string("libcurl ") + (info->version ? info->version : "?");
        if (info->host) lib += std::string(" (") + info->host + ")";
        line("Library", lib);
        // Every TLS backend compiled in; the active one without parentheses.
        line("TLS library", info->ssl_version ? info->ssl_version : "none");
        if (info->libz_version) line("zlib", info->libz_version);
    }
    line("Trusted roots", UltraNet_DescribeTrustRoots());
    line("System", UltraNet_DescribePlatform());
    return d;
}

// Runs the transfer on `h`; on failure `outMessage` holds curl's specific
// reason (falling back to curl_easy_strerror when curl left none) and, when
// given, `outDiagnostics` the full chain above. The error buffer is detached
// again before returning, so a reused handle never points at this (stack)
// buffer.
inline CURLcode Perform(CURL* h, std::string& outMessage, std::string* outDiagnostics = nullptr) {
    char buf[CURL_ERROR_SIZE];
    buf[0] = '\0';
    curl_easy_setopt(h, CURLOPT_ERRORBUFFER, buf);
    const CURLcode rc = curl_easy_perform(h);
    curl_easy_setopt(h, CURLOPT_ERRORBUFFER, static_cast<char*>(nullptr));
    if (rc == CURLE_OK) {
        outMessage.clear();
        if (outDiagnostics) outDiagnostics->clear();
        return rc;
    }
    std::string detail(buf);
    while (!detail.empty() && (detail.back() == '\n' || detail.back() == '\r' || detail.back() == ' '))
        detail.pop_back();
    outMessage = detail.empty() ? std::string(curl_easy_strerror(rc)) : detail;
    if (outDiagnostics) *outDiagnostics = Diagnostics(h, rc, outMessage);
    return rc;
}

// UltraNetResult::Error carrying the diagnostics too.
inline UltraNetResult Error(UltraNetResultCode code, const std::string& message,
                            const std::string& diagnostics) {
    UltraNetResult r = UltraNetResult::Error(code, message);
    r.diagnostics = diagnostics;
    return r;
}

} // namespace ultranet_curlerror
