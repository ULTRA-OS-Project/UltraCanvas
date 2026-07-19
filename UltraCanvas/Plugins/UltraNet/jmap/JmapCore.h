// UltraCanvas/Plugins/UltraNet/jmap/JmapCore.h
// Version: 0.1.0
// Pure JMAP wire layer (no I/O): session-resource parsing, request building,
// method-response navigation and the JMAP↔UltraNet type mapping used by the
// JMAP plug-in. Everything here is deterministic and unit-tested without a
// server (Tests/UltraNet/test_jmap_plugin.cpp) — the same split as
// imap/ImapParse.h.
//
// Speaks RFC 8620 (JMAP core) + RFC 8621 (JMAP mail). JSON handling is
// nlohmann/json (vendored, UltraCanvas/third_party/nlohmann/json.hpp).
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <UltraNet/UltraNetPlugins.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ultranet_jmap {

using json = nlohmann::json;

inline constexpr const char* kCapCore = "urn:ietf:params:jmap:core";
inline constexpr const char* kCapMail = "urn:ietf:params:jmap:mail";

// ============================================================================
// Session resource (RFC 8620 §2)
// ============================================================================
struct JmapSession {
    std::string apiUrl;
    std::string downloadUrl;      // URI template: {accountId} {blobId} {type} {name}
    std::string uploadUrl;        // URI template: {accountId}
    std::string eventSourceUrl;   // URI template: {types} {closeafter} {ping}
    std::string accountId;        // primary mail account
    std::string username;
};

// Parse the session resource. Picks primaryAccounts[urn:ietf:params:jmap:mail],
// falling back to the first listed account. Returns false (with `err` set) on
// malformed JSON or a session without an apiUrl / usable account.
inline bool ParseSession(const std::string& body, JmapSession& out, std::string& err) {
    json j = json::parse(body, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object()) { err = "session resource is not a JSON object"; return false; }

    out.apiUrl         = j.value("apiUrl", "");
    out.downloadUrl    = j.value("downloadUrl", "");
    out.uploadUrl      = j.value("uploadUrl", "");
    out.eventSourceUrl = j.value("eventSourceUrl", "");
    out.username       = j.value("username", "");
    if (out.apiUrl.empty()) { err = "session resource has no apiUrl"; return false; }

    out.accountId.clear();
    if (j.contains("primaryAccounts") && j["primaryAccounts"].is_object())
        out.accountId = j["primaryAccounts"].value(kCapMail, "");
    if (out.accountId.empty() && j.contains("accounts") && j["accounts"].is_object() &&
        !j["accounts"].empty())
        out.accountId = j["accounts"].begin().key();
    if (out.accountId.empty()) { err = "session resource lists no account"; return false; }
    return true;
}

// ============================================================================
// URI templates (RFC 6570 level 1 — the only level RFC 8620 uses)
// ============================================================================
inline std::string PercentEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                                (c >= '0' && c <= '9') || c == '-' || c == '.' ||
                                c == '_' || c == '~';
        if (unreserved) { out.push_back(static_cast<char>(c)); }
        else { out.push_back('%'); out.push_back(hex[c >> 4]); out.push_back(hex[c & 15]); }
    }
    return out;
}

// Replace every {var} in `tmpl` with the percent-encoded value from `vars`.
// Unknown variables expand to the empty string (per RFC 6570 undef handling).
inline std::string ExpandUrlTemplate(const std::string& tmpl,
                                     const std::map<std::string, std::string>& vars) {
    std::string out;
    out.reserve(tmpl.size());
    std::size_t i = 0;
    while (i < tmpl.size()) {
        if (tmpl[i] == '{') {
            std::size_t close = tmpl.find('}', i + 1);
            if (close == std::string::npos) { out.append(tmpl, i, std::string::npos); break; }
            const std::string name = tmpl.substr(i + 1, close - i - 1);
            auto it = vars.find(name);
            if (it != vars.end()) out += PercentEncode(it->second);
            i = close + 1;
        } else {
            out.push_back(tmpl[i++]);
        }
    }
    return out;
}

// ============================================================================
// Keywords ↔ UltraNetMailFlags (RFC 8621 §4.1.1). JMAP has no \Recent and no
// \Deleted (deletion is move-to-Trash / Email/set destroy).
// ============================================================================
inline UltraNetMailFlags KeywordsToFlags(const json& keywords) {
    UltraNetMailFlags f = UltraNetMailFlags::None;
    if (!keywords.is_object()) return f;
    for (auto it = keywords.begin(); it != keywords.end(); ++it) {
        if (!it.value().is_boolean() || !it.value().get<bool>()) continue;
        if      (it.key() == "$seen")     f |= UltraNetMailFlags::Seen;
        else if (it.key() == "$answered") f |= UltraNetMailFlags::Answered;
        else if (it.key() == "$flagged")  f |= UltraNetMailFlags::Flagged;
        else if (it.key() == "$draft")    f |= UltraNetMailFlags::Draft;
    }
    return f;
}

inline json FlagsToKeywords(UltraNetMailFlags flags) {
    json k = json::object();
    if (UltraNetHasFlag(flags, UltraNetMailFlags::Seen))     k["$seen"] = true;
    if (UltraNetHasFlag(flags, UltraNetMailFlags::Answered)) k["$answered"] = true;
    if (UltraNetHasFlag(flags, UltraNetMailFlags::Flagged))  k["$flagged"] = true;
    if (UltraNetHasFlag(flags, UltraNetMailFlags::Draft))    k["$draft"] = true;
    return k;
}

// ============================================================================
// EmailAddress formatting — {name,email} → "Name <email>" / bare address.
// ============================================================================
inline std::string FormatAddress(const json& a) {
    if (!a.is_object()) return {};
    const std::string email = a.contains("email") && a["email"].is_string()
                                  ? a["email"].get<std::string>() : std::string{};
    const std::string name  = a.contains("name") && a["name"].is_string()
                                  ? a["name"].get<std::string>() : std::string{};
    if (name.empty()) return email;
    if (email.empty()) return name;
    return name + " <" + email + ">";
}

inline std::vector<std::string> FormatAddressList(const json& arr) {
    std::vector<std::string> out;
    if (!arr.is_array()) return out;
    for (const auto& a : arr) {
        std::string s = FormatAddress(a);
        if (!s.empty()) out.push_back(std::move(s));
    }
    return out;
}

// ============================================================================
// Request builders. Every request `using`s core+mail; method calls get ids
// "c0", "c1", … in order.
// ============================================================================
inline std::string BuildRequest(const std::vector<std::pair<std::string, json>>& calls) {
    json req;
    req["using"] = json::array({kCapCore, kCapMail});
    json mc = json::array();
    int n = 0;
    for (const auto& [method, args] : calls)
        mc.push_back(json::array({method, args, "c" + std::to_string(n++)}));
    req["methodCalls"] = std::move(mc);
    return req.dump();
}

inline const json& MailboxProperties() {
    static const json props = json::array(
        {"id", "name", "parentId", "role", "sortOrder", "totalEmails", "unreadEmails"});
    return props;
}

inline std::string BuildMailboxGetRequest(const std::string& accountId) {
    json args = {{"accountId", accountId}, {"ids", nullptr}, {"properties", MailboxProperties()}};
    return BuildRequest({{"Mailbox/get", args}});
}

// Email/query one page of ids in a mailbox, oldest first (ascending receivedAt
// keeps uid assignment stable — see UidMap below).
inline std::string BuildEmailQueryRequest(const std::string& accountId,
                                          const std::string& mailboxId,
                                          int position, int limit) {
    json args = {
        {"accountId", accountId},
        {"filter", {{"inMailbox", mailboxId}}},
        {"sort", json::array({{{"property", "receivedAt"}, {"isAscending", true}}})},
        {"position", position},
        {"limit", limit},
        {"calculateTotal", true},
    };
    return BuildRequest({{"Email/query", args}});
}

// Envelope-level Email properties (header view, no body).
inline const json& EnvelopeProperties() {
    static const json props = json::array({"id", "blobId", "messageId", "inReplyTo",
                                           "subject", "from", "to", "cc", "receivedAt",
                                           "size", "keywords"});
    return props;
}

inline std::string BuildEmailGetRequest(const std::string& accountId,
                                        const std::vector<std::string>& ids,
                                        const json& properties) {
    json args = {{"accountId", accountId}, {"ids", ids}, {"properties", properties}};
    return BuildRequest({{"Email/get", args}});
}

// Full-message view for the legacy bulk FetchMessages: envelope properties plus
// the decoded text/html body parts.
inline std::string BuildEmailGetBodyRequest(const std::string& accountId,
                                            const std::vector<std::string>& ids) {
    json props = EnvelopeProperties();
    props.push_back("textBody");
    props.push_back("htmlBody");
    props.push_back("bodyValues");
    json args = {{"accountId", accountId}, {"ids", ids}, {"properties", props},
                 {"fetchAllBodyValues", true}};
    return BuildRequest({{"Email/get", args}});
}

// ============================================================================
// Response navigation. A JMAP response is
//   {"methodResponses": [["Mailbox/get", {…}, "c0"], …], "sessionState": "…"}
// where any entry may instead be ["error", {"type": …, "description": …}, id].
// ============================================================================
inline bool FindMethodResponse(const std::string& body, const std::string& method,
                               json& outArgs, std::string& err) {
    json j = json::parse(body, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object() || !j.contains("methodResponses") ||
        !j["methodResponses"].is_array()) {
        err = "malformed JMAP response";
        return false;
    }
    for (const auto& entry : j["methodResponses"]) {
        if (!entry.is_array() || entry.size() < 2 || !entry[0].is_string()) continue;
        const std::string name = entry[0].get<std::string>();
        if (name == method) { outArgs = entry[1]; return true; }
        if (name == "error") {
            const json& a = entry[1];
            err = a.is_object() ? a.value("type", "serverFail") : std::string("serverFail");
            const std::string desc = a.is_object() ? a.value("description", "") : std::string{};
            if (!desc.empty()) err += ": " + desc;
            return false;
        }
    }
    err = "response contains no " + method;
    return false;
}

// ============================================================================
// Mailbox objects → UltraNetMailFolder
// ============================================================================
struct JmapMailbox {
    std::string id;
    std::string parentId;             // empty when top-level
    std::string name;                 // display name of this node only
    std::string role;                 // "inbox"|"sent"|… or ""
    uint32_t totalEmails  = 0;
    uint32_t unreadEmails = 0;
};

inline bool ParseMailboxes(const json& args, std::vector<JmapMailbox>& out) {
    out.clear();
    if (!args.is_object() || !args.contains("list") || !args["list"].is_array()) return false;
    for (const auto& m : args["list"]) {
        if (!m.is_object()) continue;
        JmapMailbox b;
        b.id   = m.value("id", "");
        b.name = m.value("name", "");
        if (m.contains("parentId") && m["parentId"].is_string())
            b.parentId = m["parentId"].get<std::string>();
        if (m.contains("role") && m["role"].is_string()) {
            b.role = m["role"].get<std::string>();
            std::transform(b.role.begin(), b.role.end(), b.role.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        }
        b.totalEmails  = m.value("totalEmails", 0u);
        b.unreadEmails = m.value("unreadEmails", 0u);
        if (!b.id.empty()) out.push_back(std::move(b));
    }
    return true;
}

// Full path of a mailbox ("Parent/Child"), walking the parentId chain.
// Depth-capped so a cyclic parentId graph from a broken server can't loop.
inline std::string FolderPathFor(const JmapMailbox& m,
                                 const std::unordered_map<std::string, const JmapMailbox*>& byId) {
    std::string path = m.name;
    const JmapMailbox* cur = &m;
    for (int depth = 0; depth < 32 && !cur->parentId.empty(); ++depth) {
        auto it = byId.find(cur->parentId);
        if (it == byId.end()) break;
        cur = it->second;
        path = cur->name + "/" + path;
    }
    return path;
}

// Flatten the mailbox tree into the IMAP-shaped folder list. The JMAP mailbox
// id is carried in attributes as "jmap-id=<id>" so richer callers can reach it.
inline void BuildFolderList(const std::vector<JmapMailbox>& mailboxes,
                            std::vector<UltraNetMailFolder>& out) {
    out.clear();
    std::unordered_map<std::string, const JmapMailbox*> byId;
    for (const auto& m : mailboxes) byId[m.id] = &m;
    for (const auto& m : mailboxes) {
        UltraNetMailFolder f;
        f.name       = FolderPathFor(m, byId);
        f.delimiter  = "/";
        f.role       = m.role;
        f.selectable = true;
        f.attributes.push_back("jmap-id=" + m.id);
        out.push_back(std::move(f));
    }
}

// Resolve a folder path (as produced by BuildFolderList) back to a mailbox.
// An empty path or "INBOX" (any case) falls back to the role=inbox mailbox.
// Returns nullptr when nothing matches.
inline const JmapMailbox* FindMailboxByPath(const std::vector<JmapMailbox>& mailboxes,
                                            const std::string& folderPath) {
    std::unordered_map<std::string, const JmapMailbox*> byId;
    for (const auto& m : mailboxes) byId[m.id] = &m;

    for (const auto& m : mailboxes)
        if (FolderPathFor(m, byId) == folderPath) return &m;

    std::string upper = folderPath;
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    if (folderPath.empty() || upper == "INBOX")
        for (const auto& m : mailboxes)
            if (m.role == "inbox") return &m;
    return nullptr;
}

// ============================================================================
// Email/query and Email/get parsing
// ============================================================================
inline bool ParseQueryIds(const json& args, std::vector<std::string>& outIds,
                          uint32_t* outTotal = nullptr, std::string* outState = nullptr) {
    outIds.clear();
    if (!args.is_object() || !args.contains("ids") || !args["ids"].is_array()) return false;
    for (const auto& id : args["ids"])
        if (id.is_string()) outIds.push_back(id.get<std::string>());
    if (outTotal) *outTotal = args.value("total", 0u);
    if (outState && args.contains("queryState") && args["queryState"].is_string())
        *outState = args["queryState"].get<std::string>();
    return true;
}

// Envelope view of one Email object; `env.uid` is left 0 — the plug-in assigns
// it through UidMap.
struct JmapEnvelope {
    std::string id;
    std::string blobId;
    UltraNetMailEnvelope env;
};

inline void ParseEmailObject(const json& m, JmapEnvelope& e) {
    e.id     = m.value("id", "");
    e.blobId = m.value("blobId", "");
    e.env.subject = m.value("subject", "");
    // messageId / inReplyTo are arrays of bare ids; re-add the RFC 5322 angle
    // brackets so values compare equal with the IMAP plug-in's header parse.
    if (m.contains("messageId") && m["messageId"].is_array() && !m["messageId"].empty() &&
        m["messageId"][0].is_string())
        e.env.messageId = "<" + m["messageId"][0].get<std::string>() + ">";
    if (m.contains("inReplyTo") && m["inReplyTo"].is_array() && !m["inReplyTo"].empty() &&
        m["inReplyTo"][0].is_string())
        e.env.inReplyTo = "<" + m["inReplyTo"][0].get<std::string>() + ">";
    if (m.contains("from")) {
        auto from = FormatAddressList(m["from"]);
        if (!from.empty()) e.env.from = from.front();
    }
    if (m.contains("to")) e.env.to = FormatAddressList(m["to"]);
    if (m.contains("cc")) e.env.cc = FormatAddressList(m["cc"]);
    e.env.date  = m.value("receivedAt", "");        // UTCDate, e.g. 2026-07-17T09:00:00Z
    e.env.size  = m.value("size", 0u);
    if (m.contains("keywords")) e.env.flags = KeywordsToFlags(m["keywords"]);
}

inline bool ParseEmailEnvelopes(const json& args, std::vector<JmapEnvelope>& out) {
    out.clear();
    if (!args.is_object() || !args.contains("list") || !args["list"].is_array()) return false;
    for (const auto& m : args["list"]) {
        if (!m.is_object()) continue;
        JmapEnvelope e;
        ParseEmailObject(m, e);
        if (!e.id.empty()) out.push_back(std::move(e));
    }
    return true;
}

// Best displayable body of a full Email object (textBody preferred, htmlBody
// fallback), resolved through bodyValues. Returns false when the object
// carries no fetched body value.
inline bool ExtractBestBody(const json& m, std::string& outBody, std::string& outContentType) {
    if (!m.is_object() || !m.contains("bodyValues") || !m["bodyValues"].is_object()) return false;
    const json& values = m["bodyValues"];
    for (const char* listName : {"textBody", "htmlBody"}) {
        if (!m.contains(listName) || !m[listName].is_array()) continue;
        for (const auto& part : m[listName]) {
            if (!part.is_object()) continue;
            const std::string partId = part.value("partId", "");
            if (partId.empty() || !values.contains(partId)) continue;
            const json& v = values[partId];
            if (!v.is_object() || !v.contains("value") || !v["value"].is_string()) continue;
            outBody        = v["value"].get<std::string>();
            outContentType = part.value("type",
                std::string(listName) == std::string("textBody") ? "text/plain" : "text/html");
            return true;
        }
    }
    return false;
}

// ============================================================================
// Uid mapping. JMAP Email ids are opaque strings; the IMailboxProtocolPlugin
// surface (and UltraMail's LocalStore) key messages by uint32_t uid. The
// plug-in feeds every folder's ids through a UidMap in ascending-receivedAt
// order: an id keeps its uid for the map's lifetime, new ids get the next
// uid — so "uid > sinceUid" keeps meaning "newer than what I've seen", the
// invariant SyncEngine's incremental fetch relies on.
// ============================================================================
class UidMap {
public:
    // Returns the existing uid for `id`, or assigns the next free one.
    uint32_t Assign(const std::string& id) {
        auto it = idToUid_.find(id);
        if (it != idToUid_.end()) return it->second;
        uidToId_.push_back(id);
        const uint32_t uid = static_cast<uint32_t>(uidToId_.size());
        idToUid_.emplace(id, uid);
        return uid;
    }

    uint32_t UidFor(const std::string& id) const {
        auto it = idToUid_.find(id);
        return it == idToUid_.end() ? 0u : it->second;
    }

    std::string IdFor(uint32_t uid) const {
        if (uid == 0 || uid > uidToId_.size()) return {};
        return uidToId_[uid - 1];
    }

    uint32_t HighestUid() const { return static_cast<uint32_t>(uidToId_.size()); }

private:
    std::unordered_map<std::string, uint32_t> idToUid_;
    std::vector<std::string> uidToId_;    // index == uid - 1
};

// FNV-1a — stable 32-bit hash used to synthesise uidValidity from the
// account/mailbox identity (JMAP has no uidValidity of its own).
inline uint32_t Fnv1a32(const std::string& s) {
    uint32_t h = 2166136261u;
    for (unsigned char c : s) { h ^= c; h *= 16777619u; }
    return h;
}

} // namespace ultranet_jmap
