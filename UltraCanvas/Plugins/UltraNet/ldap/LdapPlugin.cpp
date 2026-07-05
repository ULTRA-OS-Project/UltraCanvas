// UltraCanvas/Plugins/UltraNet/ldap/LdapPlugin.cpp
// LDAP / LDAPS plug-in. Implements IDirectoryProtocolPlugin::Search by
// asking libcurl to fetch an ldap:// URL — libcurl ships native LDAP
// support and returns the result as LDIF (RFC 2849). We parse the LDIF
// stream into one UltraNetDirectoryEntry per result.
//
// LDAP URL format (RFC 4516):
//   ldap[s]://host[:port]/baseDN?attrs?scope?filter
//
// scope is one of: base (default), one, sub
// Example: ldap://ldap.example.com/dc=example,dc=com??sub?(uid=alice)
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetPlugins.h>

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

std::size_t WriteString(char* p, std::size_t s, std::size_t n, void* ud) {
    static_cast<std::string*>(ud)->append(p, s * n);
    return s * n;
}

UltraNetResultCode MapCurlError(CURLcode rc) {
    switch (rc) {
        case CURLE_OK:                      return UltraNetResultCode::Success;
        case CURLE_URL_MALFORMAT:           return UltraNetResultCode::InvalidUrl;
        case CURLE_COULDNT_RESOLVE_HOST:    return UltraNetResultCode::HostNotFound;
        case CURLE_COULDNT_CONNECT:         return UltraNetResultCode::ConnectionRefused;
        case CURLE_OPERATION_TIMEDOUT:      return UltraNetResultCode::Timeout;
        case CURLE_LOGIN_DENIED:            return UltraNetResultCode::AuthenticationFailed;
        case CURLE_SSL_CONNECT_ERROR:       return UltraNetResultCode::TlsHandshakeFailed;
        case CURLE_PEER_FAILED_VERIFICATION:return UltraNetResultCode::TlsCertificateInvalid;
        default:                            return UltraNetResultCode::Unknown;
    }
}

std::string Trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(0, 1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))  s.pop_back();
    return s;
}

// Parses an LDIF stream (RFC 2849, simplified) into UltraNetDirectoryEntries.
// Records are separated by blank lines. Each line is "attrname: value" with
// "DN: <dn>" starting a new entry. Continuation lines (starting with space)
// are appended to the previous attribute. Base64-encoded values use "::".
// We don't bother decoding base64 here; v0.1 returns it verbatim.
void ParseLdif(const std::string& body,
               std::vector<UltraNetDirectoryEntry>& out) {
    UltraNetDirectoryEntry cur{};
    std::string lastAttr;
    auto flush = [&]() {
        if (!cur.dn.empty() || !cur.attributes.empty()) out.push_back(std::move(cur));
        cur = {};
        lastAttr.clear();
    };
    std::istringstream is(body);
    std::string line;
    while (std::getline(is, line)) {
        while (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) { flush(); continue; }
        // Continuation: leading whitespace appends to last attribute value.
        if ((line.front() == ' ' || line.front() == '\t') && !lastAttr.empty()) {
            std::string cont = line;
            cont.erase(0, 1);
            auto it = cur.attributes.find(lastAttr);
            if (it != cur.attributes.end() && !it->second.empty()) {
                it->second.back() += cont;
            }
            continue;
        }
        // "attrname: value" or "attrname:: base64value"
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string name = line.substr(0, colon);
        std::size_t valStart = colon + 1;
        if (valStart < line.size() && line[valStart] == ':') ++valStart;     // ::
        std::string value = (valStart < line.size())
                                ? Trim(line.substr(valStart))
                                : std::string{};

        std::string lower; lower.reserve(name.size());
        for (char c : name) lower.push_back(static_cast<char>(std::tolower(c)));
        if (lower == "dn") cur.dn = value;
        else               cur.attributes[name].push_back(value);
        lastAttr = name;
    }
    flush();
}

// Maps UltraNetDirectoryQuery::scope (0=base, 1=onelevel, 2=subtree) to the
// RFC 4516 scope token for the LDAP URL.
const char* ScopeToken(int s) {
    switch (s) {
        case 0: return "base";
        case 1: return "one";
        case 2:
        default: return "sub";
    }
}

class LdapPlugin : public IDirectoryProtocolPlugin {
public:
    std::string GetName() const override { return "UltraNet-LDAP"; }
    std::string GetVersion() const override { return "0.1.0"; }
    std::vector<std::string> GetSupportedSchemes() const override {
        return {"ldap", "ldaps"};
    }
    UltraNetResult Initialize(const UltraNetConfig&) override {
        return UltraNetResult::Ok();
    }
    void Shutdown() override {}

    UltraNetResult Search(const std::string& url,
                          const UltraNetDirectoryQuery& q,
                          std::vector<UltraNetDirectoryEntry>& out) override {
        out.clear();

        // Build an LDAP URL by appending baseDN ? attrs ? scope ? filter
        // when the caller's URL is bare (e.g. "ldap://server/"). Otherwise
        // trust whatever they provided.
        std::string full = url;
        if (full.find('?') == std::string::npos) {
            if (!full.empty() && full.back() != '/') full.push_back('/');
            full += q.baseDn;
            full += '?';
            for (std::size_t i = 0; i < q.attributes.size(); ++i) {
                if (i) full += ',';
                full += q.attributes[i];
            }
            full += '?';
            full += ScopeToken(q.scope);
            full += '?';
            full += q.filter.empty() ? "(objectClass=*)" : q.filter;
        }

        std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> h(
            curl_easy_init(), curl_easy_cleanup);
        if (!h) return UltraNetResult::Error(
            UltraNetResultCode::InsufficientMemory, "curl_easy_init failed");

        std::string body;
        curl_easy_setopt(h.get(), CURLOPT_URL, full.c_str());
        curl_easy_setopt(h.get(), CURLOPT_WRITEFUNCTION, &WriteString);
        curl_easy_setopt(h.get(), CURLOPT_WRITEDATA, &body);
        curl_easy_setopt(h.get(), CURLOPT_NOSIGNAL, 1L);
        if (q.timeLimitSeconds > 0) {
            curl_easy_setopt(h.get(), CURLOPT_TIMEOUT,
                             static_cast<long>(q.timeLimitSeconds));
        }
        // libcurl's LDAP backend doesn't really honour Basic auth in URL,
        // but we set USERNAME/PASSWORD anyway for parity with other plug-ins.

        CURLcode rc = curl_easy_perform(h.get());
        if (rc != CURLE_OK) {
            return UltraNetResult::Error(MapCurlError(rc),
                                         curl_easy_strerror(rc));
        }
        ParseLdif(body, out);
        if (q.sizeLimit > 0 && out.size() > static_cast<std::size_t>(q.sizeLimit)) {
            out.resize(q.sizeLimit);
        }
        return UltraNetResult::Ok();
    }
};

} // namespace

extern "C" ULTRANET_PLUGIN_EXPORT
void UltraNet_PluginInit(const UltraNetPluginHost* host) {
    if (!host || host->abiVersion < 1 || !host->RegisterPlugin) return;
    host->RegisterPlugin(std::make_shared<LdapPlugin>());
}
extern "C" void UltraNet_PluginRegister(void) {
    UltraNet_RegisterPlugin(std::make_shared<LdapPlugin>());
}
