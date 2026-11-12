// Apps/UltraWeb/host/UltraWebRtc.cpp
// An app's peer connections: the rules (UltraWebRtc.h). The UltraRTC service
// is in UltraWebNetworkRtc.cpp, so the browser runtime builds the rules alone.
// Version: 0.1.0
// Last Modified: 2026-11-11
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraWebRtc.h"
#include "UltraWebFetch.h"   // FetchRules::OriginOf

#include "ultraweb.h"   // UltraWeb/guest: the result codes

#include "DataFormats/UltraCanvasJSON.h"

#include <algorithm>
#include <cctype>

using namespace UltraCanvas;

namespace UltraWeb {

namespace RtcRules {

namespace {

bool IsIceUrl(const std::string& url) {
    std::string lower = url;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (lower.rfind("stun:", 0) != 0 && lower.rfind("turn:", 0) != 0 && lower.rfind("turns:", 0) != 0) return false;
    for (unsigned char c : url) {
        if (c <= 32 || c >= 127) return false;
    }
    return url.size() <= 512;
}

bool IsTurn(const std::string& url) {
    return url.rfind("turn:", 0) == 0 || url.rfind("turns:", 0) == 0 || url.rfind("TURN:", 0) == 0 ||
           url.rfind("TURNS:", 0) == 0;
}

} // namespace

int32_t ParseConfig(const std::string& json, const std::string& appAddress, RtcConfig& out, std::string& reason) {
    out = RtcConfig{};
    // An app from the network keeps this machine's addresses to itself: only
    // relayed (TURN) candidates are gathered, until UltraRTC can hide host
    // candidates as browsers do (mDNS).
    out.relayOnly = !FetchRules::OriginOf(appAddress).empty();
    if (json.size() > kMaxConfigBytes) { reason = "the configuration is too long"; return UC_ERR_LIMIT; }
    if (json.find_first_not_of(" \t\r\n") == std::string::npos) return UC_OK;

    JSONParseResult parsed;
    const JSONValue doc = JSON::Parse(json, &parsed);
    if (!parsed.success || !doc.IsObject()) { reason = "the configuration is not a JSON object"; return UC_ERR_STATE; }
    const JSONValue& servers = doc["iceServers"];
    if (servers.IsNull()) return UC_OK;
    if (!servers.IsArray()) { reason = "iceServers is not a list"; return UC_ERR_STATE; }
    for (const JSONValue& server : servers.GetElements()) {
        if (!server.IsObject()) { reason = "an ICE server is not an object"; return UC_ERR_STATE; }
        std::vector<std::string> urls;
        const JSONValue& given = server["urls"];
        if (given.IsString()) urls.push_back(given.GetString());
        else if (given.IsArray()) {
            for (const JSONValue& url : given.GetElements()) {
                if (!url.IsString()) { reason = "an ICE server's urls are not text"; return UC_ERR_STATE; }
                urls.push_back(url.GetString());
            }
        } else { reason = "an ICE server has no urls"; return UC_ERR_STATE; }
        const std::string username = server["username"].GetString();
        const std::string credential = server["credential"].GetString();
        for (const std::string& url : urls) {
            if (!IsIceUrl(url)) { reason = "\"" + url + "\" is not a stun:, turn: or turns: address"; return UC_ERR_STATE; }
            if (out.iceServers.size() >= kMaxIceServers) {
                reason = "more than " + std::to_string(kMaxIceServers) + " ICE servers";
                return UC_ERR_LIMIT;
            }
            RtcIceServer entry;
            entry.url = url;
            if (IsTurn(url)) {
                entry.username = username;
                entry.credential = credential;
            }
            out.iceServers.push_back(std::move(entry));
        }
    }
    return UC_OK;
}

} // namespace RtcRules

} // namespace UltraWeb
