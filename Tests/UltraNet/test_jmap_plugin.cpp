// Tests/UltraNet/test_jmap_plugin.cpp
// Unit tests for the JMAP plug-in's pure wire layer in JmapCore.h (session
// parsing, URI-template expansion, request building, method-response
// navigation, mailbox/envelope mapping, keyword↔flag conversion and the
// uid↔Email-id map), plus a check that the built DSO exposes the
// IMailboxProtocolPlugin interface and rejects malformed server URLs. The
// pure tests need no server; the interface test loads the plug-in DSO.
#include "test_framework.h"

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetPlugins.h>

// Pure wire layer lives in the plug-in's header — include it directly.
#include "../../UltraCanvas/Plugins/UltraNet/jmap/JmapCore.h"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace ultranet_jmap;

// ---- session resource ------------------------------------------------------

static const char* kSessionJson = R"({
    "capabilities": {
        "urn:ietf:params:jmap:core": { "maxSizeUpload": 50000000 },
        "urn:ietf:params:jmap:mail": {}
    },
    "accounts": {
        "A13824": { "name": "user@example.com", "isPersonal": true },
        "A97813": { "name": "shared@example.com", "isPersonal": false }
    },
    "primaryAccounts": { "urn:ietf:params:jmap:mail": "A13824" },
    "username": "user@example.com",
    "apiUrl": "https://jmap.example.com/api/",
    "downloadUrl": "https://jmap.example.com/download/{accountId}/{blobId}/{name}?accept={type}",
    "uploadUrl": "https://jmap.example.com/upload/{accountId}/",
    "eventSourceUrl": "https://jmap.example.com/eventsource/?types={types}",
    "state": "75128aab4b1b"
})";

TEST(jmap_parse_session) {
    JmapSession s;
    std::string err;
    REQUIRE(ParseSession(kSessionJson, s, err));
    REQUIRE_EQ(s.apiUrl, std::string("https://jmap.example.com/api/"));
    REQUIRE_EQ(s.accountId, std::string("A13824"));   // primaryAccounts wins
    REQUIRE_EQ(s.username, std::string("user@example.com"));
    CHECK(!s.downloadUrl.empty());
    CHECK(!s.eventSourceUrl.empty());
}

TEST(jmap_parse_session_rejects_garbage) {
    JmapSession s;
    std::string err;
    CHECK(!ParseSession("not json at all", s, err));
    CHECK(!ParseSession("{\"apiUrl\":\"\"}", s, err));                  // no apiUrl
    CHECK(!ParseSession("{\"apiUrl\":\"https://x/api\"}", s, err));    // no account
}

TEST(jmap_expand_url_template) {
    const std::string out = ExpandUrlTemplate(
        "https://x/download/{accountId}/{blobId}/{name}?accept={type}",
        {{"accountId", "A1"}, {"blobId", "Bab/cd"}, {"name", "message"},
         {"type", "message/rfc822"}});
    // '/' inside values must be percent-encoded, template separators kept.
    REQUIRE_EQ(out, std::string(
        "https://x/download/A1/Bab%2Fcd/message?accept=message%2Frfc822"));

    // Unknown variables expand to nothing; unclosed braces pass through.
    REQUIRE_EQ(ExpandUrlTemplate("a{nope}b", {}), std::string("ab"));
    REQUIRE_EQ(ExpandUrlTemplate("a{broken", {}), std::string("a{broken"));
}

// ---- keywords ↔ flags ------------------------------------------------------

TEST(jmap_keywords_roundtrip) {
    UltraNetMailFlags f = UltraNetMailFlags::Seen | UltraNetMailFlags::Flagged;
    json k = FlagsToKeywords(f);
    CHECK(k.value("$seen", false));
    CHECK(k.value("$flagged", false));
    CHECK(!k.contains("$answered"));

    UltraNetMailFlags back = KeywordsToFlags(k);
    REQUIRE(UltraNetHasFlag(back, UltraNetMailFlags::Seen));
    REQUIRE(UltraNetHasFlag(back, UltraNetMailFlags::Flagged));
    REQUIRE(!UltraNetHasFlag(back, UltraNetMailFlags::Answered));

    // false-valued and unknown keywords are ignored.
    json odd = {{"$seen", false}, {"$answered", true}, {"custom", true}};
    UltraNetMailFlags g = KeywordsToFlags(odd);
    REQUIRE(!UltraNetHasFlag(g, UltraNetMailFlags::Seen));
    REQUIRE(UltraNetHasFlag(g, UltraNetMailFlags::Answered));
}

// ---- request builders ------------------------------------------------------

TEST(jmap_build_request_shape) {
    const std::string body = BuildEmailQueryRequest("A1", "MBinbox", 0, 50);
    json j = json::parse(body);
    REQUIRE(j["using"].is_array());
    CHECK(j["using"][0] == kCapCore);
    CHECK(j["using"][1] == kCapMail);
    REQUIRE_EQ(j["methodCalls"].size(), (size_t)1);
    const json& call = j["methodCalls"][0];
    REQUIRE_EQ(call[0].get<std::string>(), std::string("Email/query"));
    REQUIRE_EQ(call[2].get<std::string>(), std::string("c0"));
    const json& args = call[1];
    REQUIRE_EQ(args["accountId"].get<std::string>(), std::string("A1"));
    REQUIRE_EQ(args["filter"]["inMailbox"].get<std::string>(), std::string("MBinbox"));
    CHECK(args["sort"][0]["isAscending"].get<bool>());
    REQUIRE_EQ(args["limit"].get<int>(), 50);
}

TEST(jmap_build_mailbox_get) {
    json j = json::parse(BuildMailboxGetRequest("A1"));
    const json& call = j["methodCalls"][0];
    REQUIRE_EQ(call[0].get<std::string>(), std::string("Mailbox/get"));
    CHECK(call[1]["ids"].is_null());          // null ids == fetch all
}

// ---- method-response navigation --------------------------------------------

TEST(jmap_find_method_response) {
    const std::string ok = R"({
        "methodResponses": [
            ["Mailbox/get", {"accountId": "A1", "list": []}, "c0"]
        ],
        "sessionState": "s1"
    })";
    json args;
    std::string err;
    REQUIRE(FindMethodResponse(ok, "Mailbox/get", args, err));
    REQUIRE_EQ(args["accountId"].get<std::string>(), std::string("A1"));

    CHECK(!FindMethodResponse(ok, "Email/get", args, err));    // absent method

    const std::string failed = R"({
        "methodResponses": [
            ["error", {"type": "accountNotFound", "description": "gone"}, "c0"]
        ]
    })";
    CHECK(!FindMethodResponse(failed, "Mailbox/get", args, err));
    CHECK(err.find("accountNotFound") != std::string::npos);
    CHECK(err.find("gone") != std::string::npos);

    CHECK(!FindMethodResponse("{}", "Mailbox/get", args, err));    // malformed
}

// ---- mailboxes -------------------------------------------------------------

static json MailboxArgs() {
    return json::parse(R"({
        "accountId": "A1",
        "state": "m1",
        "list": [
            {"id": "MB1", "name": "Inbox", "parentId": null, "role": "inbox",
             "sortOrder": 1, "totalEmails": 42, "unreadEmails": 3},
            {"id": "MB2", "name": "Projects", "parentId": null, "role": null,
             "sortOrder": 10, "totalEmails": 7, "unreadEmails": 0},
            {"id": "MB3", "name": "Ultra", "parentId": "MB2", "role": null,
             "sortOrder": 10, "totalEmails": 5, "unreadEmails": 1},
            {"id": "MB4", "name": "Sent", "parentId": null, "role": "Sent",
             "sortOrder": 2, "totalEmails": 12, "unreadEmails": 0}
        ]
    })");
}

TEST(jmap_parse_mailboxes_and_paths) {
    std::vector<JmapMailbox> boxes;
    REQUIRE(ParseMailboxes(MailboxArgs(), boxes));
    REQUIRE_EQ(boxes.size(), (size_t)4);

    std::vector<UltraNetMailFolder> folders;
    BuildFolderList(boxes, folders);
    REQUIRE_EQ(folders.size(), (size_t)4);
    REQUIRE_EQ(folders[0].name, std::string("Inbox"));
    REQUIRE_EQ(folders[0].role, std::string("inbox"));
    REQUIRE_EQ(folders[2].name, std::string("Projects/Ultra"));   // parent chain
    REQUIRE_EQ(folders[2].delimiter, std::string("/"));
    REQUIRE_EQ(folders[3].role, std::string("sent"));             // lowercased
    CHECK(folders[0].attributes.front() == "jmap-id=MB1");

    // Path lookup, role fallback for "INBOX", and a miss.
    REQUIRE(FindMailboxByPath(boxes, "Projects/Ultra") == &boxes[2]);
    REQUIRE(FindMailboxByPath(boxes, "INBOX") == &boxes[0]);
    REQUIRE(FindMailboxByPath(boxes, "") == &boxes[0]);
    REQUIRE(FindMailboxByPath(boxes, "Nope") == nullptr);
}

// ---- Email/query + Email/get ----------------------------------------------

TEST(jmap_parse_query_ids) {
    json args = json::parse(R"({
        "accountId": "A1", "queryState": "q7", "canCalculateChanges": true,
        "position": 0, "total": 3, "ids": ["Ma", "Mb", "Mc"]
    })");
    std::vector<std::string> ids;
    uint32_t total = 0;
    std::string state;
    REQUIRE(ParseQueryIds(args, ids, &total, &state));
    REQUIRE_EQ(ids.size(), (size_t)3);
    REQUIRE_EQ(ids[1], std::string("Mb"));
    REQUIRE_EQ(total, (uint32_t)3);
    REQUIRE_EQ(state, std::string("q7"));
}

TEST(jmap_parse_email_envelopes) {
    json args = json::parse(R"({
        "accountId": "A1", "state": "e1",
        "list": [{
            "id": "Ma", "blobId": "Ba",
            "messageId": ["m1@example.com"], "inReplyTo": ["m0@example.com"],
            "subject": "JMAP in UltraNet",
            "from": [{"name": "Alice Example", "email": "alice@example.com"}],
            "to":   [{"name": null, "email": "bob@example.com"}],
            "cc":   [],
            "receivedAt": "2026-07-17T09:00:00Z",
            "size": 4096,
            "keywords": {"$seen": true, "$flagged": true}
        }]
    })");
    std::vector<JmapEnvelope> out;
    REQUIRE(ParseEmailEnvelopes(args, out));
    REQUIRE_EQ(out.size(), (size_t)1);
    const JmapEnvelope& e = out[0];
    REQUIRE_EQ(e.id, std::string("Ma"));
    REQUIRE_EQ(e.blobId, std::string("Ba"));
    REQUIRE_EQ(e.env.subject, std::string("JMAP in UltraNet"));
    REQUIRE_EQ(e.env.from, std::string("Alice Example <alice@example.com>"));
    REQUIRE_EQ(e.env.to.size(), (size_t)1);
    REQUIRE_EQ(e.env.to[0], std::string("bob@example.com"));
    REQUIRE_EQ(e.env.messageId, std::string("<m1@example.com>"));   // brackets restored
    REQUIRE_EQ(e.env.inReplyTo, std::string("<m0@example.com>"));
    REQUIRE_EQ(e.env.date, std::string("2026-07-17T09:00:00Z"));
    REQUIRE_EQ(e.env.size, (uint32_t)4096);
    REQUIRE(UltraNetHasFlag(e.env.flags, UltraNetMailFlags::Seen));
    REQUIRE(UltraNetHasFlag(e.env.flags, UltraNetMailFlags::Flagged));
    REQUIRE(!UltraNetHasFlag(e.env.flags, UltraNetMailFlags::Answered));
}

TEST(jmap_extract_best_body) {
    json email = json::parse(R"({
        "textBody": [{"partId": "1", "type": "text/plain"}],
        "htmlBody": [{"partId": "2", "type": "text/html"}],
        "bodyValues": {
            "1": {"value": "plain text wins", "isTruncated": false},
            "2": {"value": "<p>html</p>", "isTruncated": false}
        }
    })");
    std::string body, type;
    REQUIRE(ExtractBestBody(email, body, type));
    REQUIRE_EQ(body, std::string("plain text wins"));
    REQUIRE_EQ(type, std::string("text/plain"));

    // html fallback when there is no text part value
    json htmlOnly = json::parse(R"({
        "htmlBody": [{"partId": "2", "type": "text/html"}],
        "bodyValues": {"2": {"value": "<p>html</p>"}}
    })");
    REQUIRE(ExtractBestBody(htmlOnly, body, type));
    REQUIRE_EQ(type, std::string("text/html"));

    json empty = json::parse("{}");
    CHECK(!ExtractBestBody(empty, body, type));
}

// ---- uid map ---------------------------------------------------------------

TEST(jmap_uid_map) {
    UidMap map;
    REQUIRE_EQ(map.Assign("Ma"), (uint32_t)1);
    REQUIRE_EQ(map.Assign("Mb"), (uint32_t)2);
    REQUIRE_EQ(map.Assign("Ma"), (uint32_t)1);     // stable on re-feed
    REQUIRE_EQ(map.Assign("Mc"), (uint32_t)3);     // new id → next uid
    REQUIRE_EQ(map.HighestUid(), (uint32_t)3);
    REQUIRE_EQ(map.UidFor("Mb"), (uint32_t)2);
    REQUIRE_EQ(map.UidFor("Nope"), (uint32_t)0);
    REQUIRE_EQ(map.IdFor(3), std::string("Mc"));
    REQUIRE_EQ(map.IdFor(0), std::string(""));
    REQUIRE_EQ(map.IdFor(99), std::string(""));
}

TEST(jmap_fnv1a_stable) {
    // Known FNV-1a vectors: hash must be deterministic across runs/platforms
    // (it synthesises uidValidity).
    REQUIRE_EQ(Fnv1a32(""), (uint32_t)2166136261u);
    REQUIRE_EQ(Fnv1a32("a"), (uint32_t)0xE40C292Cu);
    REQUIRE_EQ(Fnv1a32("A1/MB1"), Fnv1a32("A1/MB1"));
    CHECK(Fnv1a32("A1/MB1") != Fnv1a32("A1/MB2"));
}

// ---- plug-in interface test (loads the DSO if available) -------------------

namespace {
fs::path JmapPluginPath() {
    if (const char* env = std::getenv("ULTRANET_JMAP_PLUGIN_PATH"))
        if (fs::exists(env)) return env;
#ifdef ULTRANET_JMAP_PLUGIN_PATH_DEFINE
    if (fs::exists(ULTRANET_JMAP_PLUGIN_PATH_DEFINE))
        return fs::path{ULTRANET_JMAP_PLUGIN_PATH_DEFINE};
#endif
    return {};
}
} // namespace

TEST(jmap_plugin_exposes_mailbox_interface) {
    const fs::path p = JmapPluginPath();
    if (p.empty()) SKIP("JMAP plug-in DSO not available in this env");

    UltraNet_Initialize();
    UltraNet_SetPluginDirectory(p.parent_path().string());
    UltraNet_RefreshPlugins();

    auto plugin = UltraNet_GetPlugin("jmap");
    REQUIRE(plugin != nullptr);
    auto* mailbox = dynamic_cast<IMailboxProtocolPlugin*>(plugin.get());
    REQUIRE(mailbox != nullptr);   // the richer interface is exposed

    // A malformed server URL must be rejected without touching the network.
    std::vector<UltraNetMailFolder> folders;
    UltraNetMailOptions opt;
    auto r = mailbox->ListFolders("imap://not-jmap/", folders, opt);
    CHECK(!bool(r));
    REQUIRE_EQ(r.code, UltraNetResultCode::InvalidUrl);

    // Phase 1 is read-only: mutations report a clear plug-in error.
    auto w = mailbox->StoreFlags("jmap://mail.example/", "INBOX", 1,
                                 UltraNetMailFlags::Seen, true, opt);
    CHECK(!bool(w));
    REQUIRE_EQ(w.code, UltraNetResultCode::PluginError);
}
