// UltraCanvas/Plugins/UltraNet/jmap/JmapCore.h
// Version: 0.3.3 - Guarded / GuardedVoid moved to common/UltraNetPluginGuard.h
// Version: 0.3.2 - Guarded / GuardedVoid: an exception inside a plug-in
//                  entry point becomes a PluginError result
// Version: 0.3.1 - a null where a string or number is expected (Fastmail's
//                  "subject": null, "to": null) reads as absent, never throws
// Version: 0.3.0 - push: the event-source URL and the StateChange events
//                  (phase 3)
// Version: 0.2.0 - the write path: Email/set keyword and mailbox patches,
//                  destroy, blob upload + Email/import, Identity/get and
//                  EmailSubmission/set (phase 2)
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
#include "../common/UltraNetPluginGuard.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <exception>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ultranet_jmap {

using json = nlohmann::json;

// The guard around every entry point (UltraNetPluginGuard.h), reachable as
// ultranet_jmap::Guarded too.
using ultranet_plugin::Guarded;
using ultranet_plugin::GuardedVoid;

// A string / number member of `o`, or `def` when `o` is not an object, has no
// such member, or holds another type there - a null above all. JSON mail
// servers write null for a missing subject, an empty To or an unknown name
// (Fastmail: "subject": null, "to": null), and nlohmann's value() returns its
// default only for a missing key: a null throws type_error, which took
// UltraMail down with SIGABRT on a draft without a subject. Nothing here may
// throw on what a server sends.
inline std::string Str(const json& o, const char* key, const std::string& def = {}) {
    if (!o.is_object()) return def;
    auto it = o.find(key);
    return it != o.end() && it->is_string() ? it->get<std::string>() : def;
}
inline uint32_t Uint(const json& o, const char* key, uint32_t def = 0) {
    if (!o.is_object()) return def;
    auto it = o.find(key);
    if (it == o.end() || !it->is_number()) return def;
    if (it->is_number_unsigned()) {
        const uint64_t v = it->get<uint64_t>();
        return v > 0xFFFFFFFFu ? 0xFFFFFFFFu : static_cast<uint32_t>(v);
    }
    const double v = it->get<double>();
    return v <= 0 ? 0u : v >= 4294967295.0 ? 0xFFFFFFFFu : static_cast<uint32_t>(v);
}

inline constexpr const char* kCapCore       = "urn:ietf:params:jmap:core";
inline constexpr const char* kCapMail       = "urn:ietf:params:jmap:mail";
inline constexpr const char* kCapSubmission = "urn:ietf:params:jmap:submission";

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

    out.apiUrl         = Str(j, "apiUrl");
    out.downloadUrl    = Str(j, "downloadUrl");
    out.uploadUrl      = Str(j, "uploadUrl");
    out.eventSourceUrl = Str(j, "eventSourceUrl");
    out.username       = Str(j, "username");
    if (out.apiUrl.empty()) { err = "session resource has no apiUrl"; return false; }

    out.accountId.clear();
    if (j.contains("primaryAccounts") && j["primaryAccounts"].is_object())
        out.accountId = Str(j["primaryAccounts"], kCapMail);
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
// Request builders. Every request `using`s core+mail (plus any extra
// capability given, e.g. submission); method calls get ids "c0", "c1", … in
// order.
// ============================================================================
inline std::string BuildRequest(const std::vector<std::pair<std::string, json>>& calls,
                                const std::vector<std::string>& extraCapabilities = {}) {
    json req;
    req["using"] = json::array({kCapCore, kCapMail});
    for (const auto& cap : extraCapabilities) req["using"].push_back(cap);
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
// The write path (RFC 8620 §5.3 /set, RFC 8621 §4.6 Email/set, §4.8
// Email/import, §6 Identity, §7 EmailSubmission).
// ============================================================================

// The keyword patch of one Email: "keywords/$seen": true to set a flag, null
// to clear it (RFC 8620 §5.3 PatchObject). Deleted and Recent have no keyword
// (deletion is a move to Trash / destroy, which the plug-in does itself) and
// patch nothing.
inline json KeywordPatch(UltraNetMailFlags flags, bool set) {
    json patch = json::object();
    auto one = [&](UltraNetMailFlags f, const char* keyword) {
        if (!UltraNetHasFlag(flags, f)) return;
        if (set) patch[std::string("keywords/") + keyword] = true;
        else     patch[std::string("keywords/") + keyword] = nullptr;
    };
    one(UltraNetMailFlags::Seen,     "$seen");
    one(UltraNetMailFlags::Answered, "$answered");
    one(UltraNetMailFlags::Flagged,  "$flagged");
    one(UltraNetMailFlags::Draft,    "$draft");
    return patch;
}

// Email/set with one update: the keyword patch of `emailId`.
inline std::string BuildEmailSetKeywordsRequest(const std::string& accountId,
                                                const std::string& emailId,
                                                UltraNetMailFlags flags, bool set) {
    json args = {{"accountId", accountId},
                 {"update", {{emailId, KeywordPatch(flags, set)}}}};
    return BuildRequest({{"Email/set", args}});
}

// Email/set with one update: `emailId` lives in `mailboxId` and nowhere else
// (a move; JMAP has no copy-then-delete).
inline std::string BuildEmailSetMailboxRequest(const std::string& accountId,
                                               const std::string& emailId,
                                               const std::string& mailboxId) {
    json args = {{"accountId", accountId},
                 {"update", {{emailId, {{"mailboxIds", {{mailboxId, true}}}}}}}};
    return BuildRequest({{"Email/set", args}});
}

// Email/set destroying `emailIds` for good.
inline std::string BuildEmailDestroyRequest(const std::string& accountId,
                                            const std::vector<std::string>& emailIds) {
    json args = {{"accountId", accountId}, {"destroy", emailIds}};
    return BuildRequest({{"Email/set", args}});
}

// The Email/import arguments for one uploaded blob: into `mailboxId` with
// `flags` as keywords (the shape APPEND has). `creationId` names the result
// in the response's "created" map.
inline json EmailImportArgs(const std::string& accountId, const std::string& creationId,
                            const std::string& blobId, const std::string& mailboxId,
                            UltraNetMailFlags flags) {
    json email = {{"blobId", blobId},
                  {"mailboxIds", {{mailboxId, true}}},
                  {"keywords", FlagsToKeywords(flags)}};
    return {{"accountId", accountId}, {"emails", {{creationId, email}}}};
}

inline std::string BuildEmailImportRequest(const std::string& accountId,
                                           const std::string& creationId,
                                           const std::string& blobId,
                                           const std::string& mailboxId,
                                           UltraNetMailFlags flags) {
    return BuildRequest({{"Email/import",
                          EmailImportArgs(accountId, creationId, blobId, mailboxId, flags)}});
}

// The answer of a POST to the session's uploadUrl (RFC 8620 §6.1):
// {"accountId", "blobId", "type", "size"}.
inline bool ParseUploadResponse(const std::string& body, std::string& outBlobId,
                                std::string& err) {
    json j = json::parse(body, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object()) { err = "upload response is not a JSON object"; return false; }
    outBlobId = Str(j, "blobId");
    if (outBlobId.empty()) { err = "upload response has no blobId"; return false; }
    return true;
}

// A /set-style response's verdict on one id: "notCreated" / "notUpdated" /
// "notDestroyed" entries carry a SetError {type, description}. Returns true
// (with `err` set) when the server refused that id; false when it did not
// list it there.
inline bool SetErrorFor(const json& args, const char* notKey, const std::string& id,
                        std::string& err) {
    if (!args.is_object() || !args.contains(notKey) || !args[notKey].is_object()) return false;
    const json& failures = args[notKey];
    if (!failures.contains(id) || !failures[id].is_object()) return false;
    const json& e = failures[id];
    err = Str(e, "type", "serverFail");
    const std::string desc = Str(e, "description");
    if (!desc.empty()) err += ": " + desc;
    return true;
}

// The server id of an object made by /set create or Email/import, by its
// creation id; "" when the response does not list it as created.
inline std::string CreatedId(const json& args, const std::string& creationId) {
    if (!args.is_object() || !args.contains("created") || !args["created"].is_object()) return {};
    const json& created = args["created"];
    if (!created.contains(creationId) || !created[creationId].is_object()) return {};
    return Str(created[creationId], "id");
}

// Identity/get (RFC 8621 §6): the addresses the account may send as.
inline std::string BuildIdentityGetRequest(const std::string& accountId) {
    json args = {{"accountId", accountId}, {"ids", nullptr}};
    return BuildRequest({{"Identity/get", args}}, {kCapSubmission});
}

struct JmapIdentity {
    std::string id;
    std::string name;
    std::string email;
};

inline bool ParseIdentities(const json& args, std::vector<JmapIdentity>& out) {
    out.clear();
    if (!args.is_object() || !args.contains("list") || !args["list"].is_array()) return false;
    for (const auto& i : args["list"]) {
        if (!i.is_object()) continue;
        JmapIdentity id;
        id.id    = Str(i, "id");
        id.name  = Str(i, "name");
        id.email = Str(i, "email");
        if (!id.id.empty()) out.push_back(std::move(id));
    }
    return true;
}

// The bare address of "Name <user@host>" / "<user@host>" / "user@host",
// lower-cased for comparison.
inline std::string BareAddress(const std::string& mailbox) {
    std::string a = mailbox;
    const std::size_t lt = a.find('<');
    const std::size_t gt = a.rfind('>');
    if (lt != std::string::npos && gt != std::string::npos && gt > lt)
        a = a.substr(lt + 1, gt - lt - 1);
    const std::size_t b = a.find_first_not_of(" \t");
    const std::size_t e = a.find_last_not_of(" \t");
    a = b == std::string::npos ? std::string() : a.substr(b, e - b + 1);
    std::transform(a.begin(), a.end(), a.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return a;
}

// The identity to submit as: the one whose address is the message's From,
// else one whose address is a wildcard for the domain ("*@example.com", RFC
// 8621 §6), else the first; nullptr with no identities at all.
inline const JmapIdentity* PickIdentity(const std::vector<JmapIdentity>& identities,
                                        const std::string& from) {
    if (identities.empty()) return nullptr;
    const std::string want = BareAddress(from);
    for (const auto& i : identities)
        if (BareAddress(i.email) == want) return &i;
    const std::size_t at = want.find('@');
    if (at != std::string::npos) {
        const std::string wildcard = "*" + want.substr(at);
        for (const auto& i : identities)
            if (BareAddress(i.email) == wildcard) return &i;
    }
    return &identities.front();
}

// The SMTP envelope of a submission: MAIL FROM and one RCPT TO per recipient
// (To, Cc and Bcc alike - the Bcc never appears in the message itself).
inline json SubmissionEnvelope(const std::string& from,
                               const std::vector<std::string>& recipients) {
    json rcpt = json::array();
    for (const auto& r : recipients) {
        const std::string addr = BareAddress(r);
        if (!addr.empty()) rcpt.push_back({{"email", addr}});
    }
    return {{"mailFrom", {{"email", BareAddress(from)}}}, {"rcptTo", rcpt}};
}

// The whole send in one request: Email/import of the uploaded raw message
// (creation id "m") into `holdMailboxId` (Drafts, or whatever the server
// has), then EmailSubmission/set creating submission "s" for "#m" with
// `identityId` and the envelope. On success the message leaves
// `holdMailboxId` for `sentMailboxId` (when given) with $draft cleared and
// $seen set - the copy in Sent a mail program keeps; without a Sent mailbox
// it is destroyed instead, so nothing of the send is left behind.
inline std::string BuildSendRequest(const std::string& accountId,
                                    const std::string& blobId,
                                    const std::string& holdMailboxId,
                                    const std::string& sentMailboxId,
                                    const std::string& identityId,
                                    const std::string& from,
                                    const std::vector<std::string>& recipients) {
    json importArgs = EmailImportArgs(accountId, "m", blobId, holdMailboxId,
                                      UltraNetMailFlags::Draft | UltraNetMailFlags::Seen);
    json submission = {{"emailId", "#m"},
                       {"identityId", identityId},
                       {"envelope", SubmissionEnvelope(from, recipients)}};
    json setArgs = {{"accountId", accountId}, {"create", {{"s", submission}}}};
    if (!sentMailboxId.empty()) {
        json onSuccess = {{"keywords/$draft", nullptr}, {"keywords/$seen", true},
                          {"mailboxIds/" + sentMailboxId, true}};
        if (holdMailboxId != sentMailboxId) onSuccess["mailboxIds/" + holdMailboxId] = nullptr;
        setArgs["onSuccessUpdateEmail"] = {{"#s", onSuccess}};
    } else {
        setArgs["onSuccessDestroyEmail"] = json::array({"#s"});
    }
    return BuildRequest({{"Email/import", importArgs}, {"EmailSubmission/set", setArgs}},
                        {kCapSubmission});
}

// ============================================================================
// Push (RFC 8620 §7.3): the event source streams StateChange events.
// ============================================================================

// The types a mail client watches: new or changed mail, and the folder list.
inline constexpr const char* kWatchedTypes = "Email,Mailbox";

// The URL to open the event source at: the session's eventSourceUrl template
// with the watched types, no closeafter and a ping every `pingSeconds` (a
// keep-alive the server sends when nothing changes; 0 = none).
inline std::string BuildEventSourceUrl(const std::string& eventSourceUrlResolved,
                                       int pingSeconds) {
    return ExpandUrlTemplate(eventSourceUrlResolved,
                             {{"types", kWatchedTypes},
                              {"closeafter", "no"},
                              {"ping", std::to_string(pingSeconds < 0 ? 0 : pingSeconds)}});
}

// A "state" event's data: {"@type": "StateChange", "changed": {"<accountId>":
// {"Email": "<state>", "Mailbox": "<state>", …}}}. Fills `outStates` with
// the states of `accountId` (type -> state); false for anything else, or
// when the event names no state of that account.
inline bool ParseStateChange(const std::string& data, const std::string& accountId,
                             std::map<std::string, std::string>& outStates) {
    outStates.clear();
    json j = json::parse(data, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object() || !j.contains("changed") ||
        !j["changed"].is_object())
        return false;
    const json& changed = j["changed"];
    if (!changed.contains(accountId) || !changed[accountId].is_object()) return false;
    for (auto it = changed[accountId].begin(); it != changed[accountId].end(); ++it)
        if (it.value().is_string()) outStates[it.key()] = it.value().get<std::string>();
    return !outStates.empty();
}

// The types whose state `now` reports as new or other than `known` had
// them: what to fetch again. A type `now` leaves out has not changed.
inline std::vector<std::string> ChangedTypes(const std::map<std::string, std::string>& known,
                                             const std::map<std::string, std::string>& now) {
    std::vector<std::string> out;
    for (const auto& [type, state] : now) {
        auto it = known.find(type);
        if (it == known.end() || it->second != state) out.push_back(type);
    }
    return out;
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
            err = a.is_object() ? Str(a, "type", "serverFail") : std::string("serverFail");
            const std::string desc = a.is_object() ? Str(a, "description") : std::string{};
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
        b.id   = Str(m, "id");
        b.name = Str(m, "name");
        if (m.contains("parentId") && m["parentId"].is_string())
            b.parentId = m["parentId"].get<std::string>();
        if (m.contains("role") && m["role"].is_string()) {
            b.role = m["role"].get<std::string>();
            std::transform(b.role.begin(), b.role.end(), b.role.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        }
        b.totalEmails  = Uint(m, "totalEmails");
        b.unreadEmails = Uint(m, "unreadEmails");
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

// The mailbox with `role` ("inbox", "drafts", "sent", "trash", …); nullptr
// when the account has none.
inline const JmapMailbox* FindMailboxByRole(const std::vector<JmapMailbox>& mailboxes,
                                            const std::string& role) {
    for (const auto& m : mailboxes)
        if (m.role == role) return &m;
    return nullptr;
}

// Whether `emailObject` (an Email/get object with "mailboxIds") is in
// `mailboxId`.
inline bool EmailInMailbox(const json& emailObject, const std::string& mailboxId) {
    if (!emailObject.is_object() || !emailObject.contains("mailboxIds") ||
        !emailObject["mailboxIds"].is_object())
        return false;
    const json& boxes = emailObject["mailboxIds"];
    return boxes.contains(mailboxId) && boxes[mailboxId].is_boolean() &&
           boxes[mailboxId].get<bool>();
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
    if (outTotal) *outTotal = Uint(args, "total");
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
    e.id     = Str(m, "id");
    e.blobId = Str(m, "blobId");
    e.env.subject = Str(m, "subject");
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
    e.env.date  = Str(m, "receivedAt");        // UTCDate, e.g. 2026-07-17T09:00:00Z
    e.env.size  = Uint(m, "size");
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
            const std::string partId = Str(part, "partId");
            if (partId.empty() || !values.contains(partId)) continue;
            const json& v = values[partId];
            if (!v.is_object() || !v.contains("value") || !v["value"].is_string()) continue;
            outBody        = v["value"].get<std::string>();
            outContentType = Str(part, "type",
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
