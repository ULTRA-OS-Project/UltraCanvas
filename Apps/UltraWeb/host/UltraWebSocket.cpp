// Apps/UltraWeb/host/UltraWebSocket.cpp
// An app's WebSockets: the rules (UltraWebSocket.h). The UltraNet service is
// in UltraWebNetworkSocket.cpp, so the browser runtime, which has no UltraNet,
// builds the rules alone.
// Version: 0.1.0
// Last Modified: 2026-11-11
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraWebSocket.h"
#include "UltraWebFetch.h"   // FetchRules::OriginOf

#include "ultraweb.h"   // UltraWeb/guest: the result codes

#include "UltraNet/UltraNetUrl.h"

#include <algorithm>
#include <cctype>

namespace UltraWeb {

namespace {

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) --b;
    return s.substr(a, b - a);
}

// A subprotocol is an HTTP token (RFC 6455 4.1, RFC 7230 3.2.6).
bool IsToken(const std::string& s) {
    if (s.empty()) return false;
    for (unsigned char c : s) {
        if (c <= 32 || c >= 127) return false;
        if (std::string("()<>@,;:\\\"/[]?={}").find(char(c)) != std::string::npos) return false;
    }
    return true;
}

} // namespace

namespace SocketRules {

int32_t Prepare(const std::string& appAddress, const std::string& reference, const std::string& protocols,
                SocketRequest& out, std::string& reason) {
    out = SocketRequest{};
    const std::string trimmed = Trim(reference);
    if (trimmed.empty()) { reason = "no address"; return UC_ERR_STATE; }

    // An absolute ws:/wss: address as it is; anything else is read as a
    // page reads it - against the app's address - and http(s) becomes ws(s).
    std::string absolute;
    UltraNetUrlComponents parts;
    const bool parsed = UltraNet_ParseUrl(trimmed, parts) && !parts.scheme.empty();
    const std::string givenScheme = parsed ? Lower(parts.scheme) : std::string();
    if (givenScheme == "ws" || givenScheme == "wss") {
        absolute = trimmed;
    } else if (givenScheme == "http" || givenScheme == "https") {
        absolute = (givenScheme == "https" ? "wss" : "ws") + trimmed.substr(parts.scheme.size());
    } else if (!givenScheme.empty() && trimmed.find("://") != std::string::npos) {
        reason = givenScheme + ": is not a WebSocket address (ws: or wss:)";
        return UC_ERR_DENIED;
    } else {
        if (FetchRules::OriginOf(appAddress).empty()) {
            reason = "an app from a file or about: has no address to read " + trimmed + " against";
            return UC_ERR_STATE;
        }
        std::string resolved;
        if (!UltraNet_ResolveUrl(appAddress, trimmed, resolved)) {
            reason = trimmed + " is not an address";
            return UC_ERR_STATE;
        }
        const std::string scheme = Lower(resolved.substr(0, resolved.find(':')));
        if (scheme == "https") absolute = "wss" + resolved.substr(5);
        else if (scheme == "http") absolute = "ws" + resolved.substr(4);
        else { reason = scheme + ": is not a WebSocket address (ws: or wss:)"; return UC_ERR_DENIED; }
    }

    if (!UltraNet_ParseUrl(absolute, parts) || parts.host.empty()) {
        reason = absolute + " is not an address";
        return UC_ERR_STATE;
    }
    if (!parts.username.empty() || !parts.password.empty()) {
        reason = "an address with a user name or password is refused";
        return UC_ERR_DENIED;
    }
    const std::string scheme = Lower(parts.scheme);
    const std::string origin = FetchRules::OriginOf(appAddress);
    if (scheme == "ws" && origin.rfind("https:", 0) == 0) {
        reason = "an https app cannot open a plain ws: socket (use wss:)";
        return UC_ERR_DENIED;
    }
    // The fragment never goes on the wire (RFC 6455 3).
    const size_t hash = absolute.find('#');
    if (hash != std::string::npos) absolute.erase(hash);

    if (protocols.size() > kMaxProtocolsBytes) { reason = "the subprotocol list is too long"; return UC_ERR_LIMIT; }
    size_t at = 0;
    while (at <= protocols.size()) {
        size_t comma = protocols.find(',', at);
        if (comma == std::string::npos) comma = protocols.size();
        const std::string item = Trim(protocols.substr(at, comma - at));
        at = comma + 1;
        if (item.empty()) continue;
        if (!IsToken(item)) { reason = "\"" + item + "\" is not a subprotocol name"; return UC_ERR_STATE; }
        if (std::find(out.protocols.begin(), out.protocols.end(), item) != out.protocols.end()) {
            reason = "the subprotocol " + item + " is named twice";
            return UC_ERR_STATE;
        }
        out.protocols.push_back(item);
    }

    out.url = absolute;
    out.origin = origin.empty() ? "null" : origin;
    return UC_OK;
}

} // namespace SocketRules

} // namespace UltraWeb
