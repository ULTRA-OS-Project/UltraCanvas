// include/UltraNet/UltraNetCurlProxy.h
// The proxy on every libcurl handle UltraNet opens - the HTTP client, the
// WebSocket client, FTP / SFTP and the IMAP / SMTP / POP3 / WebDAV / LDAP /
// Telnet / gRPC plug-ins - so each connects the way the proxy setting says
// (UltraNet_ResolveProxy decides; this only hands the answer to libcurl).
// Header-only so each loadable plug-in DSO can use it; a plug-in reaches
// UltraNet_ResolveProxy through its host table (UltraNetPlugins.h, ABI 4).
//
// A direct connection sets an empty CURLOPT_PROXY: libcurl otherwise reads
// http_proxy / https_proxy from the environment on its own, which would go
// round both "No proxy" and an exception. For the same reason the exception
// list libcurl would read from no_proxy is emptied - the resolver has
// already applied the exceptions.
//
// An HTTP proxy carries anything that is not HTTP (IMAP, SMTP, FTP, SSH, a
// WebSocket) as a CONNECT tunnel: `tunnel` says so. HTTPS through an HTTP
// proxy is always tunnelled by libcurl itself.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraNet/UltraNetCore.h"

#include <curl/curl.h>

#include <string>

namespace ultranet_curlproxy {

// `proxy` as UltraNet_ResolveProxy returned it, on `h`.
inline void Apply(CURL* h, const UltraNetProxyConfig& proxy, bool tunnel) {
    if (!h) return;
    const std::string url = UltraNet_DescribeProxy(proxy);
    if (url.empty()) {
        curl_easy_setopt(h, CURLOPT_PROXY, "");
        return;
    }
    curl_easy_setopt(h, CURLOPT_PROXY, url.c_str());
    curl_easy_setopt(h, CURLOPT_NOPROXY, "");
    if (!proxy.credentials.username.empty()) {
        curl_easy_setopt(h, CURLOPT_PROXYUSERNAME, proxy.credentials.username.c_str());
        curl_easy_setopt(h, CURLOPT_PROXYPASSWORD, proxy.credentials.password.c_str());
        curl_easy_setopt(h, CURLOPT_PROXYAUTH, static_cast<long>(CURLAUTH_ANY));
    }
#if defined(_WIN32)
    else if (proxy.type == UltraNetProxyType::Http || proxy.type == UltraNetProxyType::Https) {
        // A Windows proxy that asks who is connecting is answered as the
        // signed-in user (SSPI: Negotiate or NTLM), the way the system's own
        // programs pass a company proxy. Harmless when the proxy asks nothing.
        curl_easy_setopt(h, CURLOPT_PROXYUSERPWD, ":");
        curl_easy_setopt(h, CURLOPT_PROXYAUTH,
                         static_cast<long>(CURLAUTH_NEGOTIATE | CURLAUTH_NTLM));
    }
#endif
    if (tunnel && (proxy.type == UltraNetProxyType::Http || proxy.type == UltraNetProxyType::Https))
        curl_easy_setopt(h, CURLOPT_HTTPPROXYTUNNEL, 1L);
}

// The proxy for `url` - the module-wide setting, or `requested` when it is
// enabled - on `h`.
inline void ApplyFor(CURL* h, const std::string& url, bool tunnel,
                     const UltraNetProxyConfig& requested = UltraNetProxyConfig()) {
    Apply(h, UltraNet_ResolveProxy(url, requested), tunnel);
}

} // namespace ultranet_curlproxy
