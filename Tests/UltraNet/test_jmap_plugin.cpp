// Tests/UltraNet/test_jmap_plugin.cpp
// Unit tests for the JMAP plug-in's pure wire layer in JmapCore.h (session
// parsing, URI-template expansion, request building, method-response
// navigation, mailbox/envelope mapping, keyword↔flag conversion, the
// uid↔Email-id map, the write path: keyword and mailbox patches, destroy,
// upload + Email/import, identities and the EmailSubmission request, and
// push: the event-source URL and StateChange events), plus a
// check that the built DSO exposes the IMailboxProtocolPlugin interface and
// rejects malformed server URLs. The pure tests need no server; the
// interface test loads the plug-in DSO.
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

// ---- nulls where strings and numbers are expected --------------------------

TEST(jmap_null_fields_read_as_absent) {
    // Fastmail's Email/get answer for a draft without a subject or recipients
    // (verbatim from a crash report): "subject": null, "to": null, "cc": null,
    // "inReplyTo": null, "name": null. nlohmann's value() throws on a null
    // where the key exists; this took UltraMail down with SIGABRT.
    const char* body = R"json({"methodResponses":[["Email/get",{"state":"2011","list":[
        {"id":"Me867e35d297249bcea045fe0","blobId":"Ge867e35d297249bcea045fe023156e43d164b83c",
         "keywords":{"$draft":true},"size":246,"receivedAt":"2026-10-06T12:28:38Z",
         "messageId":["d554df52-03b4-471e-a073-252930b41613@vmd202955"],"inReplyTo":null,
         "from":[{"name":null,"email":"info@ultraos.eu"}],"to":[],"cc":null,"subject":"(No Subject)"},
        {"id":"M5bcb02aa26d432ec547f8ab7","blobId":"G5bcb02aa26d432ec547f8ab7c354e23eeffdbf9d",
         "keywords":{"$draft":true,"nonjunk":true,"$seen":true},"size":461686,
         "receivedAt":"2026-02-09T12:06:18Z","messageId":["57722084-1f16-47b5-8981-5d2b7a857aa8@ultraos.eu"],
         "inReplyTo":null,"from":[{"name":"info@ultraos.eu","email":"info@ultraos.eu"}],
         "to":null,"cc":null,"subject":null}],
        "notFound":[],"accountId":"info@ultraos.eu"},"c0"]],"sessionState":"0"})json";
    json args;
    std::string err;
    REQUIRE(FindMethodResponse(body, "Email/get", args, err));
    std::vector<JmapEnvelope> envelopes;
    REQUIRE(ParseEmailEnvelopes(args, envelopes));
    REQUIRE_EQ(envelopes.size(), (std::size_t)2);
    REQUIRE_EQ(envelopes[0].env.subject, std::string("(No Subject)"));
    REQUIRE_EQ(envelopes[0].env.from, std::string("info@ultraos.eu"));   // a null name
    CHECK(envelopes[0].env.to.empty());
    CHECK(envelopes[0].env.inReplyTo.empty());
    REQUIRE_EQ(envelopes[1].env.subject, std::string());               // a null subject
    CHECK(envelopes[1].env.to.empty());                                 // a null To
    CHECK(envelopes[1].env.cc.empty());
    REQUIRE_EQ(envelopes[1].env.size, 461686u);
    CHECK(UltraNetHasFlag(envelopes[1].env.flags, UltraNetMailFlags::Seen));

    // Every other reader shrugs at a null, a wrong type or a missing key too.
    json odd = json::parse(R"({"id":null,"blobId":42,"subject":["x"],"size":null,
                               "receivedAt":false,"messageId":null,"from":null})");
    JmapEnvelope e;
    ParseEmailObject(odd, e);
    CHECK(e.id.empty()); CHECK(e.blobId.empty()); CHECK(e.env.subject.empty());
    REQUIRE_EQ(e.env.size, 0u); CHECK(e.env.date.empty()); CHECK(e.env.from.empty());

    std::vector<JmapMailbox> boxes;
    REQUIRE(ParseMailboxes(json::parse(R"({"list":[{"id":"MB1","name":null,"role":null,
        "totalEmails":null,"unreadEmails":"7","parentId":null}]})"), boxes));
    REQUIRE_EQ(boxes.size(), (std::size_t)1);
    CHECK(boxes[0].name.empty()); REQUIRE_EQ(boxes[0].totalEmails, 0u);
    REQUIRE_EQ(boxes[0].unreadEmails, 0u);

    JmapSession session;
    CHECK(!ParseSession(R"({"apiUrl":null,"accounts":{"A1":{}}})", session, err));
    std::vector<JmapIdentity> ids;
    REQUIRE(ParseIdentities(json::parse(R"({"list":[{"id":"I1","name":null,"email":null}]})"), ids));
    REQUIRE_EQ(ids.size(), (std::size_t)1);
    CHECK(ids[0].email.empty());
    std::string blob;
    CHECK(!ParseUploadResponse(R"({"blobId":null})", blob, err));
    json set = json::parse(R"({"created":{"m":{"id":null}},"notCreated":{"x":{"type":null,"description":null}}})");
    CHECK(CreatedId(set, "m").empty());
    REQUIRE(SetErrorFor(set, "notCreated", "x", err));
    REQUIRE_EQ(err, std::string("serverFail"));

    // The helpers themselves: a number that is not a uint32 is clamped.
    json nums = json::parse(R"({"big":99999999999,"neg":-5,"frac":12.9,"s":"3"})");
    REQUIRE_EQ(Uint(nums, "big"), 0xFFFFFFFFu);
    REQUIRE_EQ(Uint(nums, "neg"), 0u);
    REQUIRE_EQ(Uint(nums, "frac"), 12u);
    REQUIRE_EQ(Uint(nums, "s", 9), 9u);
    REQUIRE_EQ(Str(json(7), "x", "d"), std::string("d"));   // not even an object
}

// ---- the write path (phase 2) ---------------------------------------------

TEST(jmap_keyword_patch_sets_and_clears) {
    json set = KeywordPatch(UltraNetMailFlags::Seen | UltraNetMailFlags::Flagged, true);
    REQUIRE_EQ(set.size(), (std::size_t)2);
    CHECK(set["keywords/$seen"] == true);
    CHECK(set["keywords/$flagged"] == true);

    json clear = KeywordPatch(UltraNetMailFlags::Answered, false);
    REQUIRE_EQ(clear.size(), (std::size_t)1);
    CHECK(clear["keywords/$answered"].is_null());

    // \Deleted and \Recent have no keyword: nothing to patch.
    CHECK(KeywordPatch(UltraNetMailFlags::Deleted, true).empty());
    CHECK(KeywordPatch(UltraNetMailFlags::Recent, true).empty());

    json req = json::parse(BuildEmailSetKeywordsRequest("A1", "M7", UltraNetMailFlags::Seen, true));
    REQUIRE_EQ(req["methodCalls"][0][0].get<std::string>(), std::string("Email/set"));
    REQUIRE_EQ(req["methodCalls"][0][1]["accountId"].get<std::string>(), std::string("A1"));
    CHECK(req["methodCalls"][0][1]["update"]["M7"]["keywords/$seen"] == true);
}

TEST(jmap_mailbox_patch_and_destroy_requests) {
    json mv = json::parse(BuildEmailSetMailboxRequest("A1", "M7", "MB-trash"));
    const json& upd = mv["methodCalls"][0][1]["update"]["M7"];
    // The whole mailboxIds set is replaced: the message leaves where it was.
    REQUIRE_EQ(upd["mailboxIds"].size(), (std::size_t)1);
    CHECK(upd["mailboxIds"]["MB-trash"] == true);

    json del = json::parse(BuildEmailDestroyRequest("A1", {"M7", "M8"}));
    REQUIRE_EQ(del["methodCalls"][0][0].get<std::string>(), std::string("Email/set"));
    REQUIRE_EQ(del["methodCalls"][0][1]["destroy"].size(), (std::size_t)2);
    REQUIRE_EQ(del["methodCalls"][0][1]["destroy"][1].get<std::string>(), std::string("M8"));
}

TEST(jmap_import_request_and_upload_response) {
    json req = json::parse(BuildEmailImportRequest("A1", "m", "B9", "MB-drafts",
                                                   UltraNetMailFlags::Draft | UltraNetMailFlags::Seen));
    REQUIRE_EQ(req["methodCalls"][0][0].get<std::string>(), std::string("Email/import"));
    const json& email = req["methodCalls"][0][1]["emails"]["m"];
    REQUIRE_EQ(email["blobId"].get<std::string>(), std::string("B9"));
    CHECK(email["mailboxIds"]["MB-drafts"] == true);
    CHECK(email["keywords"]["$draft"] == true);
    CHECK(email["keywords"]["$seen"] == true);
    CHECK(!email["keywords"].contains("$flagged"));

    std::string blob, err;
    REQUIRE(ParseUploadResponse(R"({"accountId":"A1","blobId":"B9","type":"message/rfc822","size":120})",
                                blob, err));
    REQUIRE_EQ(blob, std::string("B9"));
    CHECK(!ParseUploadResponse(R"({"accountId":"A1"})", blob, err));
    CHECK(!ParseUploadResponse("not json", blob, err));
}

TEST(jmap_set_response_verdicts) {
    json args = json::parse(R"({
        "accountId": "A1",
        "created": { "m": { "id": "M100", "blobId": "B9", "threadId": "T1", "size": 120 } },
        "notCreated": { "x": { "type": "overQuota", "description": "mailbox full" } },
        "updated": { "M7": null },
        "notUpdated": { "M8": { "type": "notFound" } },
        "notDestroyed": { "M9": { "type": "forbidden" } }
    })");
    REQUIRE_EQ(CreatedId(args, "m"), std::string("M100"));
    CHECK(CreatedId(args, "x").empty());
    std::string err;
    REQUIRE(SetErrorFor(args, "notCreated", "x", err));
    REQUIRE_EQ(err, std::string("overQuota: mailbox full"));
    CHECK(!SetErrorFor(args, "notUpdated", "M7", err));
    REQUIRE(SetErrorFor(args, "notUpdated", "M8", err));
    REQUIRE_EQ(err, std::string("notFound"));
    REQUIRE(SetErrorFor(args, "notDestroyed", "M9", err));
    CHECK(!SetErrorFor(json::object(), "notUpdated", "M8", err));
}

TEST(jmap_identities_and_bare_addresses) {
    REQUIRE_EQ(BareAddress("Erika Example <Erika@Example.com>"), std::string("erika@example.com"));
    REQUIRE_EQ(BareAddress("<bob@example.org>"), std::string("bob@example.org"));
    REQUIRE_EQ(BareAddress("  bob@example.org "), std::string("bob@example.org"));

    json req = json::parse(BuildIdentityGetRequest("A1"));
    REQUIRE_EQ(req["methodCalls"][0][0].get<std::string>(), std::string("Identity/get"));
    bool usesSubmission = false;
    for (const auto& cap : req["using"])
        if (cap.get<std::string>() == kCapSubmission) usesSubmission = true;
    CHECK(usesSubmission);

    json args = json::parse(R"({"accountId":"A1","list":[
        {"id":"I1","name":"Erika","email":"erika@example.com"},
        {"id":"I2","name":"Any","email":"*@example.com"},
        {"id":"I3","name":"Old","email":"old@example.net"}]})");
    std::vector<JmapIdentity> ids;
    REQUIRE(ParseIdentities(args, ids));
    REQUIRE_EQ(ids.size(), (std::size_t)3);
    REQUIRE_EQ(PickIdentity(ids, "Erika <ERIKA@example.com>")->id, std::string("I1"));
    REQUIRE_EQ(PickIdentity(ids, "alias@example.com")->id, std::string("I2"));   // the wildcard
    REQUIRE_EQ(PickIdentity(ids, "nobody@elsewhere.org")->id, std::string("I1")); // the first
    CHECK(PickIdentity({}, "erika@example.com") == nullptr);
}

TEST(jmap_send_request_imports_then_submits) {
    const std::string body = BuildSendRequest("A1", "B9", "MB-drafts", "MB-sent", "I1",
                                              "Erika <erika@example.com>",
                                              {"Bob <bob@example.org>", "cc@example.org"});
    json req = json::parse(body);
    bool usesSubmission = false;
    for (const auto& cap : req["using"])
        if (cap.get<std::string>() == kCapSubmission) usesSubmission = true;
    CHECK(usesSubmission);
    REQUIRE_EQ(req["methodCalls"].size(), (std::size_t)2);
    REQUIRE_EQ(req["methodCalls"][0][0].get<std::string>(), std::string("Email/import"));
    REQUIRE_EQ(req["methodCalls"][1][0].get<std::string>(), std::string("EmailSubmission/set"));

    const json& imp = req["methodCalls"][0][1]["emails"]["m"];
    CHECK(imp["mailboxIds"]["MB-drafts"] == true);
    CHECK(imp["keywords"]["$draft"] == true);

    const json& sub = req["methodCalls"][1][1];
    REQUIRE_EQ(sub["create"]["s"]["emailId"].get<std::string>(), std::string("#m"));   // back-reference
    REQUIRE_EQ(sub["create"]["s"]["identityId"].get<std::string>(), std::string("I1"));
    const json& env = sub["create"]["s"]["envelope"];
    REQUIRE_EQ(env["mailFrom"]["email"].get<std::string>(), std::string("erika@example.com"));
    REQUIRE_EQ(env["rcptTo"].size(), (std::size_t)2);
    REQUIRE_EQ(env["rcptTo"][0]["email"].get<std::string>(), std::string("bob@example.org"));
    // Filed in Sent once it has gone, no longer a draft, out of Drafts.
    const json& on = sub["onSuccessUpdateEmail"]["#s"];
    CHECK(on["mailboxIds/MB-sent"] == true);
    CHECK(on["mailboxIds/MB-drafts"].is_null());
    CHECK(on["keywords/$draft"].is_null());
    CHECK(on["keywords/$seen"] == true);

    // Without a Sent mailbox nothing of the send is left behind.
    json noSent = json::parse(BuildSendRequest("A1", "B9", "MB-inbox", "", "I1",
                                               "erika@example.com", {"bob@example.org"}));
    const json& sub2 = noSent["methodCalls"][1][1];
    CHECK(!sub2.contains("onSuccessUpdateEmail"));
    REQUIRE_EQ(sub2["onSuccessDestroyEmail"][0].get<std::string>(), std::string("#s"));
}

TEST(jmap_mailbox_by_role_and_membership) {
    std::vector<JmapMailbox> boxes;
    JmapMailbox a; a.id = "MB1"; a.name = "Inbox"; a.role = "inbox"; boxes.push_back(a);
    JmapMailbox b; b.id = "MB2"; b.name = "Trash"; b.role = "trash"; boxes.push_back(b);
    REQUIRE(FindMailboxByRole(boxes, "trash") != nullptr);
    REQUIRE_EQ(FindMailboxByRole(boxes, "trash")->id, std::string("MB2"));
    CHECK(FindMailboxByRole(boxes, "sent") == nullptr);

    json email = json::parse(R"({"id":"M1","mailboxIds":{"MB2":true}})");
    CHECK(EmailInMailbox(email, "MB2"));
    CHECK(!EmailInMailbox(email, "MB1"));
    CHECK(!EmailInMailbox(json::object(), "MB1"));
}

// ---- push (phase 3) -------------------------------------------------------

TEST(jmap_event_source_url_and_state_changes) {
    const std::string url = BuildEventSourceUrl(
        "https://jmap.example.com/eventsource/?types={types}&closeafter={closeafter}&ping={ping}",
        300);
    REQUIRE_EQ(url, std::string("https://jmap.example.com/eventsource/"
                                "?types=Email%2CMailbox&closeafter=no&ping=300"));
    REQUIRE(BuildEventSourceUrl("https://x/es?ping={ping}", -5).find("ping=0") != std::string::npos);

    std::map<std::string, std::string> states;
    REQUIRE(ParseStateChange(R"({"@type":"StateChange","changed":{"A1":{"Email":"e1","Mailbox":"m1"},
                                 "A2":{"Email":"zzz"}}})", "A1", states));
    REQUIRE_EQ(states.size(), (std::size_t)2);
    REQUIRE_EQ(states["Email"], std::string("e1"));
    REQUIRE_EQ(states["Mailbox"], std::string("m1"));
    // Another account, a ping, garbage: not a change of this account.
    CHECK(!ParseStateChange(R"({"changed":{"A2":{"Email":"zzz"}}})", "A1", states));
    CHECK(!ParseStateChange(R"({"ping":1})", "A1", states));
    CHECK(!ParseStateChange("not json", "A1", states));

    // What to fetch again: a state that is new or differs.
    std::map<std::string, std::string> known = {{"Email", "e1"}, {"Mailbox", "m1"}};
    CHECK(ChangedTypes(known, {{"Email", "e1"}, {"Mailbox", "m1"}}).empty());
    std::vector<std::string> changed = ChangedTypes(known, {{"Email", "e2"}, {"Mailbox", "m1"}});
    REQUIRE_EQ(changed.size(), (std::size_t)1);
    REQUIRE_EQ(changed[0], std::string("Email"));
    changed = ChangedTypes(known, {{"Email", "e2"}, {"Mailbox", "m2"}, {"Thread", "t1"}});
    REQUIRE_EQ(changed.size(), (std::size_t)3);
    // A type the event leaves out has not changed.
    CHECK(ChangedTypes(known, {{"Mailbox", "m1"}}).empty());
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

    // The write path checks its URL before touching the network too.
    auto w = mailbox->StoreFlags("imap://not-jmap/", "INBOX", 1,
                                 UltraNetMailFlags::Seen, true, opt);
    CHECK(!bool(w));
    REQUIRE_EQ(w.code, UltraNetResultCode::InvalidUrl);
    // SendMail takes the jmap:// URL from the options, as the SMTP plug-in does.
    UltraNetMailMessage msg;
    msg.from = "erika@example.com"; msg.to = {"bob@example.org"};
    auto s = mailbox->SendMail(msg, opt);
    CHECK(!bool(s));
    REQUIRE_EQ(s.code, UltraNetResultCode::InvalidUrl);
    // So does a watch: a bad URL never opens a stream.
    UltraNetHandle watch = 42;
    auto w2 = mailbox->WatchMailbox("imap://not-jmap/", opt,
                                    [](const UltraNetMailChange&) {},
                                    [](const UltraNetResult&) {}, watch);
    CHECK(!bool(w2));
    REQUIRE_EQ(w2.code, UltraNetResultCode::InvalidUrl);
    REQUIRE_EQ(watch, UltraNetInvalidHandle);
    mailbox->StopWatching(UltraNetInvalidHandle);   // an unknown handle is ignored
}
