// Tests/UltraStoreAdmin/test_market.cpp
// The merchant's side of the marketplace service, against a call that
// answers from a map: the canonical JSON the signatures cover (checked
// against what the service's json.dumps writes), the signed request header
// verified with the merchant's public key, a submission's body, the
// notices, the terms, the standing, and the seed opened from the key vault.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "admin_helpers.h"

#include "KeyVault.h"
#include "Market.h"
#include "Verifier.h"

#include "DataFormats/UltraCanvasJSON.h"

#include <map>

using namespace UltraStoreAdmin;
using namespace admin_test;
using UltraCanvas::JSONValue;

namespace {

struct FakeMarket {
    std::map<std::string, std::string> answers;   // "METHOD path" -> body
    std::vector<std::string> calls;                // "METHOD url | authorization | body"
    int status = 200;
    MarketCall Call() {
        return [this](const std::string& method, const std::string& url, const std::string& authorization, const std::string& body) {
            calls.push_back(method + " " + url + " | " + authorization + " | " + body);
            MarketReply r;
            const std::string path = url.substr(url.find("/v1/"));
            auto it = answers.find(method + " " + path);
            if (it == answers.end()) { r.status = 404; r.body = R"({"error":"no_such","error_description":"No such thing"})"; return r; }
            r.status = status;
            r.body = it->second;
            return r;
        };
    }
};

bool VerifySigned(const std::string& header, const std::string& merchantId, const std::string& method, const std::string& path,
                  const std::string& body, const std::vector<uint8_t>& publicKey) {
    const std::string prefix = "Merchant " + merchantId + ":";
    if (header.rfind(prefix, 0) != 0) return false;
    const size_t colon = header.find(':', prefix.size());
    if (colon == std::string::npos) return false;
    const std::string stamp = header.substr(prefix.size(), colon - prefix.size());
    std::vector<uint8_t> signature;
    if (!UltraStore::FromHex(header.substr(colon + 1), signature)) return false;
    return UltraStore::VerifyCatalogSignature(method + " " + path + "\n" + stamp + "\n" + body, signature, {publicKey}).Ok();
}

} // namespace

TEST(the_canonical_json_is_what_the_service_signs_over) {
    // The same sample through the service's json.dumps(separators=(",", ":"), sort_keys=True, ensure_ascii=False).
    const std::string text = "{\"id\":\"org.example.hello\",\"name\":\"H\xc3\xa4llo \\\"W\\\" \\\\ \\/\\t\\n\\u0001 \xe2\x82\xac\xf0\x9f\x98\x80\",\"kind\":\"app\","
                             "\"releases\":[{\"version\":\"1.0.0\",\"packages\":[{\"os\":\"linux\",\"size\":10,\"sha256\":\"" + std::string(64, 'a') + "\"}]}],"
                             "\"free\":true,\"nothing\":null,\"n\":-3,\"f\":2.5,\"empty\":{},\"list\":[]}";
    UltraCanvas::JSONParseResult parse;
    const JSONValue v = UltraCanvas::JSON::Parse(text, &parse);
    REQUIRE(parse.success);
    const std::string expected = "{\"empty\":{},\"f\":2.5,\"free\":true,\"id\":\"org.example.hello\",\"kind\":\"app\",\"list\":[],\"n\":-3,"
                                 "\"name\":\"H\xc3\xa4llo \\\"W\\\" \\\\ /\\t\\n\\u0001 \xe2\x82\xac\xf0\x9f\x98\x80\",\"nothing\":null,"
                                 "\"releases\":[{\"packages\":[{\"os\":\"linux\",\"sha256\":\"" + std::string(64, 'a') + "\",\"size\":10}],\"version\":\"1.0.0\"}]}";
    REQUIRE_EQ(CanonicalJson(v), expected);
    REQUIRE_EQ(CanonicalJson(UltraCanvas::JSON::Parse("{\"b\":[1,2.0,\"x\"],\"a\":{\"z\":false,\"y\":1e3}}")), std::string("{\"a\":{\"y\":1000.0,\"z\":false},\"b\":[1,2.0,\"x\"]}"));
    // Floats as Python's repr writes them: the shortest digits, plain up to 1e16, else d.ddde+XX.
    REQUIRE_EQ(CanonicalJson(UltraCanvas::JSON::Parse("[0.1, 2.5, 1e-05, 1.5e16, -0.25, 123456.789, 1e+16, 0.0001]")),
               std::string("[0.1,2.5,1e-05,1.5e+16,-0.25,123456.789,1e+16,0.0001]"));
}

TEST(a_request_is_signed_the_way_the_service_verifies) {
    std::vector<uint8_t> seed, pk;
    REQUIRE(UltraStore::GenerateSigningKey(seed, pk));
    const std::string header = SignRequest(seed, "m-1", "post", "/v1/submissions", "{\"a\":1}", "1700000000");
    REQUIRE(header.rfind("Merchant m-1:1700000000:", 0) == 0);
    REQUIRE(VerifySigned(header, "m-1", "POST", "/v1/submissions", "{\"a\":1}", pk));
    REQUIRE(!VerifySigned(header, "m-1", "POST", "/v1/submissions", "{\"a\":2}", pk));   // another body
    REQUIRE(!VerifySigned(header, "m-1", "GET", "/v1/submissions", "{\"a\":1}", pk));    // another method
    std::vector<uint8_t> otherSeed, otherPk;
    REQUIRE(UltraStore::GenerateSigningKey(otherSeed, otherPk));
    REQUIRE(!VerifySigned(header, "m-1", "POST", "/v1/submissions", "{\"a\":1}", otherPk));
    // Without a timestamp given, now is used: digits only.
    const std::string now = SignRequest(seed, "m-1", "GET", "/v1/merchants/me", "");
    const std::string stamp = now.substr(std::string("Merchant m-1:").size(), now.find(':', 14) - 13);
    REQUIRE(!stamp.empty() && stamp.find_first_not_of("0123456789") == std::string::npos);
    REQUIRE(SignRequest({}, "m-1", "GET", "/", "").empty());   // no seed, no header
}

TEST(a_submission_carries_the_signed_component_and_the_packages) {
    std::vector<uint8_t> seed, pk;
    REQUIRE(UltraStore::GenerateSigningKey(seed, pk));
    FakeMarket m;
    m.answers["POST /v1/submissions"] = R"({"id":"s-00000001","app":"org.ultraos.hello","version":"1.0.0","state":"checked",)"
                                        R"("checks":{"signature":"ok","merchant_standing":"ok","packages":"ok","scans":"not run"}})";
    MarketClient client({"https://market.example/", "m-anna"}, seed, m.Call());
    REQUIRE_EQ(client.Settings().url, std::string("https://market.example"));
    UltraStore::Component hello = HelloApp();
    const auto sent = client.Submit(hello, "s-00000000");
    REQUIRE(sent);
    REQUIRE_EQ(sent.Value().id, std::string("s-00000001"));
    REQUIRE_EQ(sent.Value().state, std::string("checked"));
    REQUIRE_EQ(sent.Value().checks.size(), size_t(4));
    REQUIRE_EQ(sent.Value().checks[0].first, std::string("signature"));
    // The body: the component as the catalog writes it, the signature over its canonical form, the packages, the answered id.
    REQUIRE_EQ(m.calls.size(), size_t(1));
    const std::string call = m.calls[0];
    REQUIRE(call.rfind("POST https://market.example/v1/submissions | Merchant m-anna:", 0) == 0);
    const std::string body = call.substr(call.rfind(" | ") + 3);
    const JSONValue sentBody = UltraCanvas::JSON::Parse(body);
    REQUIRE(sentBody.IsObject());
    REQUIRE_EQ(sentBody.Get("component").Get("id").GetString(), hello.id);
    REQUIRE_EQ(sentBody.Get("answers").GetString(), std::string("s-00000000"));
    std::vector<uint8_t> signature;
    REQUIRE(UltraStore::FromHex(sentBody.Get("signature").GetString(), signature));
    REQUIRE(UltraStore::VerifyCatalogSignature(CanonicalJson(sentBody.Get("component")), signature, {pk}).Ok());
    const JSONValue& packages = sentBody.Get("packages");
    REQUIRE(packages.GetSize() >= 1);
    REQUIRE_EQ(packages.At(0).Get("sha256").GetString(), hello.releases[0].packages[0].sha256);
    REQUIRE(packages.At(0).Contains("file") || packages.At(0).Contains("url"));
    // The request itself is signed over the body.
    const std::string authorization = call.substr(call.find(" | ") + 3, call.rfind(" | ") - call.find(" | ") - 3);
    REQUIRE(VerifySigned(authorization, "m-anna", "POST", "/v1/submissions", body, pk));
    // Without the settings nothing is sent.
    MarketClient unset({"", ""}, seed, m.Call());
    REQUIRE(unset.Submit(hello).error.code == UltraStore::ErrorCode::InvalidArgument);
    REQUIRE(MarketClient({"https://market.example", "m-anna"}, {}, m.Call()).Submit(hello).error.code == UltraStore::ErrorCode::InvalidArgument);
}

TEST(the_notices_the_terms_and_the_standing) {
    std::vector<uint8_t> seed, pk;
    REQUIRE(UltraStore::GenerateSigningKey(seed, pk));
    FakeMarket m;
    m.answers["GET /v1/merchants/me"] = R"({"id":"m-anna","name":"Anna & Co","standing":"paused","strikes":1,"terms":{"in_force":2,"accepted":1}})";
    m.answers["GET /v1/merchants/me/notices"] = R"({"notices":[
        {"id":"n-00000002","kind":"warning","subject":"Late answer","text":"...","clause":"4.1","created_at":"2026-11-11T10:00:00Z","deadline":"","acknowledged_at":""},
        {"id":"n-00000001","kind":"announcement","subject":"Maintenance","text":"Sunday.","clause":"","created_at":"2026-11-10T10:00:00Z","deadline":"","acknowledged_at":"2026-11-10T11:00:00Z"}]})";
    m.answers["POST /v1/notices/n-00000002/ack"] = R"({"id":"n-00000002","acknowledged_at":"2026-11-11T12:00:00Z"})";
    m.answers["GET /v1/terms/current"] = R"({"version":2,"text":"Be nicer.","changes":"nicer","in_force_from":"2026-11-01","created_at":"","created_by":"operator"})";
    m.answers["POST /v1/terms/accept"] = R"({"merchant":"m-anna","version":2,"accepted_by":"Anna","accepted_at":"2026-11-11T12:01:00Z","standing":"active"})";
    m.answers["GET /v1/merchants/me/reports"] = R"({"reports":[{"id":"r-00000001","kind":"crash","state":"open","text":"It crashes."}]})";
    MarketClient client({"https://market.example", "m-anna"}, seed, m.Call());
    const auto standing = client.GetStanding();
    REQUIRE(standing);
    REQUIRE_EQ(standing.Value().standing, std::string("paused"));
    REQUIRE_EQ(standing.Value().strikes, 1);
    REQUIRE(standing.Value().TermsDue());
    const auto notices = client.Notices();
    REQUIRE(notices);
    REQUIRE_EQ(notices.Value().size(), size_t(2));
    REQUIRE_EQ(notices.Value()[0].kind, std::string("warning"));
    REQUIRE(!notices.Value()[0].Acknowledged() && notices.Value()[1].Acknowledged());
    REQUIRE_EQ(client.Acknowledge("n-00000002").Value(), std::string("2026-11-11T12:00:00Z"));
    REQUIRE(m.calls.back().rfind("POST https://market.example/v1/notices/n-00000002/ack | Merchant m-anna:", 0) == 0);
    const auto terms = client.CurrentTerms();
    REQUIRE(terms);
    REQUIRE_EQ(terms.Value().version, 2);
    REQUIRE_EQ(terms.Value().text, std::string("Be nicer."));
    m.answers["GET /v1/merchants/me"] = R"({"id":"m-anna","name":"Anna & Co","standing":"active","strikes":1,"terms":{"in_force":2,"accepted":2}})";
    const auto accepted = client.AcceptTerms(2, "Anna");
    REQUIRE(accepted);
    REQUIRE_EQ(accepted.Value().standing, std::string("active"));
    REQUIRE(!accepted.Value().TermsDue());
    REQUIRE(m.calls[m.calls.size() - 2].find(R"("accepted_by":"Anna")") != std::string::npos);
    const auto reports = client.Reports();
    REQUIRE(reports);
    REQUIRE_EQ(reports.Value().Get("reports").GetSize(), size_t(1));
    // The service refusing: its words come back; 401 is a signature the service did not accept.
    m.status = 401;
    m.answers["GET /v1/merchants/me"] = R"({"error":"unauthorized","error_description":"The request's signature is not the merchant's"})";
    const auto refused = client.GetStanding();
    REQUIRE(!refused);
    REQUIRE(refused.error.code == UltraStore::ErrorCode::SignatureInvalid);
    REQUIRE(refused.error.message.find("not the merchant's") != std::string::npos);
    m.status = 200;
    m.answers.erase("GET /v1/terms/current");
    REQUIRE(client.CurrentTerms().error.code == UltraStore::ErrorCode::NotFound);
}

TEST(the_seed_is_opened_from_the_key_vault_for_the_signing) {
    const std::string dir = TempDir("market-key");
    AdminDatabase db;
    REQUIRE(db.Open(Join(dir, "admin.sqlite")).Ok());
    REQUIRE(OpenKeySeed(db, 0, "pw").error.code == UltraStore::ErrorCode::NotFound);   // no key yet
    const auto made = NewSigningKey("pw", KeyCost::Fast());
    REQUIRE(made.Ok());
    const auto added = db.AddKey("merchant", made.Value().publicKeyHex, made.Value().sealed.Bytes());
    REQUIRE(added.Ok());
    const auto seed = OpenKeySeed(db, 0, "pw");   // the first active key
    REQUIRE(seed.Ok());
    REQUIRE_EQ(seed.Value().size(), size_t(32));
    REQUIRE(OpenKeySeed(db, added.Value().id, "wrong").error.code == UltraStore::ErrorCode::SignatureInvalid);
    REQUIRE(db.SetSetting("market.key", std::to_string(added.Value().id)).Ok());
    REQUIRE(OpenKeySeed(db, 0, "pw").Ok());
    // What the seed signs verifies with the key's public half.
    std::vector<uint8_t> pk;
    REQUIRE(UltraStore::FromHex(made.Value().publicKeyHex, pk));
    const std::string header = SignRequest(seed.Value(), "m-1", "GET", "/v1/merchants/me", "", "1700000000");
    REQUIRE(VerifySigned(header, "m-1", "GET", "/v1/merchants/me", "", pk));
    RemoveDir(dir);
}
