// Tests/UltraStoreOperator/test_operator.cpp
// The operator's client against a call that answers from a map: the bearer
// header on every request, the bodies the service's models read, the
// records parsed from its answers (a merchant, a submission with its
// checks and decisions, a report with its reporter, a notice, the policy,
// the audit rows), the publish report, the settings file round trip and
// the environment over it, the age of a timestamp.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "Operator.h"

#include "DataFormats/UltraCanvasJSON.h"
#include "UltraCanvasPathUtf8.h"

#include <filesystem>
#include <map>
#include <random>

using namespace UltraStoreOperator;
using UltraCanvas::JSONValue;

namespace {

struct FakeService {
    std::map<std::string, std::string> answers;   // "METHOD path" -> body
    std::vector<std::string> calls;                // "METHOD url | authorization | body"
    int status = 200;
    MarketCall Call() {
        return [this](const std::string& method, const std::string& url, const std::string& authorization, const std::string& body) {
            calls.push_back(method + " " + url + " | " + authorization + " | " + body);
            MarketReply r;
            const size_t at = url.find("/v1/");
            const std::string path = at == std::string::npos ? url.substr(url.find("/healthz")) : url.substr(at);
            auto it = answers.find(method + " " + path);
            if (it == answers.end()) { r.status = 404; r.body = R"({"error":"no_such","error_description":"No such thing"})"; return r; }
            r.status = status;
            r.body = it->second;
            return r;
        };
    }
};

OperatorSettings TestSettings() {
    OperatorSettings s;
    s.url = "https://market.example/";
    s.token = "op-secret";
    s.name = "Alex";
    return s;
}

std::string TempDir() {
    std::mt19937_64 rng{std::random_device{}()};
    const auto dir = std::filesystem::temp_directory_path() / ("ultrastoreop-" + std::to_string(rng()));
    std::filesystem::create_directories(dir);
    return UltraCanvas::PathToUtf8(dir);
}

} // namespace

TEST(every_request_carries_the_bearer_token_on_the_service_url) {
    FakeService fake;
    fake.answers["GET /v1/merchants"] = R"({"merchants":[]})";
    OperatorClient client(TestSettings(), fake.Call());
    const auto list = client.Merchants();
    REQUIRE(list.Ok());
    REQUIRE(list.Value().empty());
    REQUIRE_EQ(fake.calls.size(), size_t(1));
    REQUIRE_EQ(fake.calls[0], std::string("GET https://market.example/v1/merchants | Bearer op-secret | "));
}

TEST(no_url_or_token_is_refused_before_any_call) {
    FakeService fake;
    OperatorSettings s;
    OperatorClient client(s, fake.Call());
    REQUIRE(!client.Merchants().Ok());
    s.url = "https://market.example";
    REQUIRE(!OperatorClient(s, fake.Call()).Merchants().Ok());
    REQUIRE(fake.calls.empty());
}

TEST(an_error_answer_becomes_the_service_description) {
    FakeService fake;
    fake.status = 409;
    fake.answers["POST /v1/submissions/s-00000001/decide"] = R"({"error":"wrong_state","error_description":"A submission released cannot be release"})";
    OperatorClient client(TestSettings(), fake.Call());
    const auto r = client.Decide("s-00000001", "release", "");
    REQUIRE(!r.Ok());
    REQUIRE_EQ(r.error.message, std::string("A submission released cannot be release"));
    REQUIRE_EQ(r.error.detail, std::string("HTTP 409"));
    REQUIRE(!client.GetMerchant("nobody").Ok());
    REQUIRE(client.GetMerchant("nobody").error.code == ErrorCode::NotFound);
}

TEST(a_merchant_is_registered_suspended_and_reinstated) {
    FakeService fake;
    const std::string merchant = R"({"id":"m-1","name":"Pixelwerk","contact":"https://pixelwerk.example","key":")" + std::string(64, 'a') +
                                 R"(","created_at":"2026-11-11T10:00:00Z","standing":"active","strikes":1,"terms":{"in_force":2,"accepted":1}})";
    fake.answers["POST /v1/merchants"] = merchant;
    fake.answers["PUT /v1/merchants/m-1"] = merchant;
    fake.answers["POST /v1/merchants/m-1/suspend"] = R"({"suspension":{"id":3,"scope":"merchant","target":"m-1","clause":"9.1","reason":"malware","start":"2026-11-11","end":"2026-12-11","lifted_at":"","created_by":"operator"},"merchant":)" + merchant + "}";
    fake.answers["POST /v1/merchants/m-1/reinstate"] = R"({"lifted":1,"merchant":)" + merchant + "}";
    fake.answers["GET /v1/merchants/m-1"] = merchant.substr(0, merchant.size() - 1) +
        R"(,"notes":"","suspensions":[{"id":3,"scope":"merchant","target":"m-1","clause":"9.1","reason":"malware","start":"2026-11-11","end":"","lifted_at":"","created_by":"operator"}],)"
        R"("acceptances":[{"merchant":"m-1","version":1,"accepted_by":"Jo","accepted_at":"2026-11-01T09:00:00Z"}],"submissions":[]})";
    OperatorClient client(TestSettings(), fake.Call());

    const auto added = client.RegisterMerchant("", "Pixelwerk", "https://pixelwerk.example", std::string(64, 'a'));
    REQUIRE(added.Ok());
    REQUIRE_EQ(added.Value().id, std::string("m-1"));
    REQUIRE_EQ(added.Value().strikes, 1);
    REQUIRE(added.Value().TermsDue());
    // The body: no id when empty, the name, contact and key.
    REQUIRE(fake.calls.back().find("\"name\":\"Pixelwerk\"") != std::string::npos);
    REQUIRE(fake.calls.back().find("\"id\"") == std::string::npos);

    REQUIRE(client.UpdateMerchant("m-1", "", "mail@pixelwerk.example", "").Ok());
    REQUIRE(fake.calls.back().rfind("PUT ", 0) == 0);
    REQUIRE_EQ(fake.calls.back().substr(fake.calls.back().rfind(" | ") + 3), std::string(R"({"contact":"mail@pixelwerk.example"})"));

    const auto suspended = client.SuspendMerchant("m-1", "9.1", "malware", 30);
    REQUIRE(suspended.Ok());
    REQUIRE(fake.calls.back().find("\"days\":30") != std::string::npos);
    const auto reinstated = client.ReinstateMerchant("m-1", "the fix shipped");
    REQUIRE(reinstated.Ok());
    REQUIRE_EQ(reinstated.Value().name, std::string("Pixelwerk"));

    const auto file = client.GetMerchant("m-1");
    REQUIRE(file.Ok());
    REQUIRE(file.Value().Suspended());
    REQUIRE_EQ(file.Value().suspensions.size(), size_t(1));
    REQUIRE_EQ(file.Value().suspensions[0].clause, std::string("9.1"));
    REQUIRE_EQ(file.Value().acceptances.size(), size_t(1));
    REQUIRE_EQ(file.Value().acceptances[0].acceptedBy, std::string("Jo"));
}

TEST(a_submission_carries_its_checks_and_decisions) {
    FakeService fake;
    const std::string sub = R"({"id":"s-00000007","merchant":"m-1","app":"org.pixelwerk.notenkiste","version":"1.0.0",)"
                            R"("component":{"id":"org.pixelwerk.notenkiste","name":"Notenkiste","publisher":"pixelwerk","categories":["office"],)"
                            R"("summary":{"en":"Sheet music library"},"pricing":{"model":"paid","price":"24.90","currency":"EUR","trial_days":14}},)"
                            R"("signature":"ab","packages":[{"file":"packages/a.zip","sha256":"ff","size":2800000}],)"
                            R"("checks":{"signature":"ok","standing":"active","packages":"ok","scans":"not run","privacy_url":"failed"},)"
                            R"("state":"in_review","answers":"","created_at":"2026-11-09T09:14:00Z","decided_at":"","released_sequence":0,)"
                            R"("decisions":[{"action":"start_review","message":"","actor":"operator","at":"2026-11-10T08:00:00Z","sequence":0}]})";
    fake.answers["GET /v1/submissions?state=in_review"] = "{\"submissions\":[" + sub + "]}";
    fake.answers["GET /v1/submissions/s-00000007"] = sub;
    fake.answers["POST /v1/submissions/s-00000007/decide"] = sub;
    OperatorClient client(TestSettings(), fake.Call());

    const auto list = client.Submissions("in_review");
    REQUIRE(list.Ok());
    REQUIRE_EQ(list.Value().size(), size_t(1));
    const Submission& s = list.Value()[0];
    REQUIRE_EQ(s.Name(), std::string("Notenkiste"));
    REQUIRE(s.ChecksFailed());
    REQUIRE_EQ(s.checks.size(), size_t(5));
    REQUIRE_EQ(s.decisions.size(), size_t(1));
    REQUIRE_EQ(s.decisions[0].action, std::string("start_review"));
    REQUIRE_EQ(s.packages.GetSize(), size_t(1));
    REQUIRE_EQ(s.component.Get("pricing").Get("trial_days").GetInteger(0), int64_t(14));

    REQUIRE(client.Decide("s-00000007", "request_changes", "the privacy policy link answers 404", 0).Ok());
    REQUIRE_EQ(fake.calls.back().substr(fake.calls.back().rfind(" | ") + 3),
               std::string(R"({"action":"request_changes","message":"the privacy policy link answers 404","sequence":0})"));
}

TEST(the_publish_and_the_released_list_are_read) {
    FakeService fake;
    fake.answers["GET /v1/released"] = R"({"released":[{"app":"org.a","merchant":"m-1","submission":"s-00000001","sequence":5,"component":{"id":"org.a"},"packages":[]}],"sequence":5,"published":4})";
    fake.answers["POST /v1/publish"] = R"({"sequence":5,"apps":1,"components":0,"publishers":1,"left_out":["org.b: merchant m-2 is suspended"],"files":["catalog.json","catalog.json.sig"],"public_key":"cd"})";
    fake.answers["GET /v1/publish/key"] = R"({"public_key":"cd","trusted_keys":["cd","ef"]})";
    OperatorClient client(TestSettings(), fake.Call());
    const auto released = client.GetReleased();
    REQUIRE(released.Ok());
    REQUIRE_EQ(released.Value().sequence, 5);
    REQUIRE_EQ(released.Value().published, 4);
    REQUIRE_EQ(released.Value().entries.size(), size_t(1));
    REQUIRE_EQ(released.Value().entries[0].sequence, 5);
    const auto published = client.Publish();
    REQUIRE(published.Ok());
    REQUIRE_EQ(published.Value().sequence, 5);
    REQUIRE_EQ(published.Value().leftOut.size(), size_t(1));
    REQUIRE_EQ(published.Value().files.size(), size_t(2));
    REQUIRE_EQ(published.Value().publicKey, std::string("cd"));
    const auto key = client.GetPublishKey();
    REQUIRE(key.Ok());
    REQUIRE_EQ(key.Value().trustedKeys.size(), size_t(2));
}

TEST(a_report_shows_the_operator_its_reporter_and_is_closed_with_an_outcome) {
    FakeService fake;
    const std::string report = R"({"id":"r-00000042","source":"store","target_kind":"app","target":"org.bitfalke.downloader","kind":"malware","text":"bfupd talks home",)"
                               R"("attachments":["log.txt"],"state":"open","created_at":"2026-11-11T12:04:00Z","resolved_at":"","outcome":"",)"
                               R"("reporter":"ULIC1.x","reporter_merchant":"","assignee":"","findings":""})";
    fake.answers["GET /v1/reports?state=open"] = "{\"reports\":[" + report + "]}";
    fake.answers["GET /v1/reports/r-00000042"] = report;
    fake.answers["POST /v1/reports/r-00000042/assign"] = report;
    fake.answers["POST /v1/reports/r-00000042/resolve"] = report;
    fake.answers["POST /v1/reports"] = R"({"id":"r-00000043","secret":"s","state":"open","created_at":"2026-11-11T13:00:00Z"})";
    fake.answers["GET /v1/reports/r-00000043"] = report;
    OperatorClient client(TestSettings(), fake.Call());

    const auto list = client.Reports("open");
    REQUIRE(list.Ok());
    REQUIRE_EQ(list.Value().size(), size_t(1));
    REQUIRE_EQ(list.Value()[0].reporter, std::string("ULIC1.x"));
    REQUIRE(list.Value()[0].Open());
    REQUIRE_EQ(list.Value()[0].attachments.size(), size_t(1));

    REQUIRE(client.AssignReport("r-00000042", "Mia").Ok());
    REQUIRE_EQ(fake.calls.back().substr(fake.calls.back().rfind(" | ") + 3), std::string(R"({"assignee":"Mia"})"));
    REQUIRE(client.CloseReport("r-00000042", true, "a second executable", "The app was withdrawn").Ok());
    REQUIRE(fake.calls.back().find("/resolve | ") != std::string::npos);
    REQUIRE(fake.calls.back().find("\"outcome\":\"The app was withdrawn\"") != std::string::npos);

    // One from mail: the operator's bearer on POST /v1/reports, then the report read back.
    const auto opened = client.OpenReport("merchant", "m-1", "support", "no answer in 9 days", "mail from u@example");
    REQUIRE(opened.Ok());
    REQUIRE(fake.calls[fake.calls.size() - 2].find("POST https://market.example/v1/reports | Bearer op-secret | {\"target_kind\":\"merchant\"") == 0);
}

TEST(a_notice_names_its_recipients_and_may_suspend) {
    FakeService fake;
    fake.answers["POST /v1/notices"] = R"({"notice":{"id":"n-00000001","kind":"warning","subject":"Support unanswered","text":"...","clause":"7.2","recipients":["m-1"],)"
                                       R"("created_at":"2026-11-11T11:52:00Z","created_by":"operator","expires_at":"2027-11-11","deadline":"2026-11-25","acknowledged_at":""},)"
                                       R"("suspended":[{"id":4,"scope":"merchant","target":"m-1","clause":"7.2","reason":"3 strikes","start":"2026-11-11","end":"2026-12-11","lifted_at":"","created_by":"policy"}]})";
    fake.answers["GET /v1/notices"] = R"({"notices":[{"id":"n-00000001","kind":"warning","subject":"S","text":"","clause":"7.2","recipients":["*"],"created_at":"","created_by":"","expires_at":"","deadline":"","acknowledged_at":""}]})";
    OperatorClient client(TestSettings(), fake.Call());
    REQUIRE(!client.SendNotice("warning", "S", "", "7.2", {}).Ok());   // no recipients
    const auto sent = client.SendNotice("warning", "Support unanswered", "...", "7.2", {"m-1"});
    REQUIRE(sent.Ok());
    REQUIRE_EQ(sent.Value().notice.deadline, std::string("2026-11-25"));
    REQUIRE_EQ(sent.Value().suspended.size(), size_t(1));
    REQUIRE_EQ(sent.Value().suspended[0].createdBy, std::string("policy"));
    REQUIRE(fake.calls.back().find("\"recipients\":[\"m-1\"]") != std::string::npos);
    const auto list = client.Notices();
    REQUIRE(list.Ok());
    REQUIRE_EQ(list.Value()[0].recipients[0], std::string("*"));
}

TEST(the_terms_the_policy_and_the_audit_log_are_read_and_written) {
    FakeService fake;
    fake.answers["GET /v1/terms"] = R"({"versions":[{"version":1,"text":"T1","changes":"first","in_force_from":"2026-01-01","created_at":"","created_by":"operator"},)"
                                    R"({"version":2,"text":"T2","changes":"refunds","in_force_from":"2026-11-30","created_at":"","created_by":"operator"}],"in_force":1})";
    fake.answers["POST /v1/terms"] = R"({"version":3,"text":"T3","changes":"","in_force_from":"2027-01-01","created_at":"","created_by":"operator"})";
    fake.answers["GET /v1/policy"] = R"({"strike_expiry_days":90,"strikes_before_suspension":3,"suspension_days":30,"answer_deadline_days":5,"clauses_that_suspend":["9.1","12"]})";
    fake.answers["PUT /v1/policy"] = fake.answers["GET /v1/policy"];
    fake.answers["GET /v1/audit?since=0&limit=500"] = R"({"audit":[{"id":1,"at":"2026-11-11T10:00:00Z","actor":"operator","action":"register","target_kind":"merchant","target":"m-1",)"
                                                     R"("clause":"","before":null,"after":{"name":"Pixelwerk"},"acted_on":"","prev_hash":"","hash":"h1"},)"
                                                     R"({"id":2,"at":"2026-11-11T11:00:00Z","actor":"operator","action":"release","target_kind":"submission","target":"s-00000001",)"
                                                     R"("clause":"","before":{"state":"in_review"},"after":{"state":"released","message":"good","sequence":5},"acted_on":"decision:1","prev_hash":"h1","hash":"h2"}],"next":2})";
    fake.answers["GET /v1/audit/verify"] = R"({"ok":true,"rows":2,"last_hash":"h2"})";
    OperatorClient client(TestSettings(), fake.Call());

    const auto terms = client.Terms();
    REQUIRE(terms.Ok());
    REQUIRE_EQ(terms.Value().inForce, 1);
    REQUIRE_EQ(terms.Value().versions.size(), size_t(2));
    REQUIRE_EQ(terms.Value().versions[1].changes, std::string("refunds"));
    const auto added = client.AddTerms("T3", "", "2027-01-01", 0);
    REQUIRE(added.Ok());
    REQUIRE_EQ(added.Value().version, 3);
    REQUIRE(fake.calls.back().find("\"in_force_from\":\"2027-01-01\"") != std::string::npos);

    const auto policy = client.GetPolicy();
    REQUIRE(policy.Ok());
    REQUIRE_EQ(policy.Value().strikeExpiryDays, 90);
    REQUIRE_EQ(policy.Value().clausesThatSuspend.size(), size_t(2));
    Policy p = policy.Value();
    p.suspensionDays = 14;
    REQUIRE(client.SetPolicy(p).Ok());
    REQUIRE(fake.calls.back().rfind("PUT ", 0) == 0);
    REQUIRE(fake.calls.back().find("\"suspension_days\":14") != std::string::npos);
    REQUIRE(fake.calls.back().find("\"clauses_that_suspend\":[\"9.1\",\"12\"]") != std::string::npos);

    const auto page = client.Audit();
    REQUIRE(page.Ok());
    REQUIRE_EQ(page.Value().rows.size(), size_t(2));
    REQUIRE_EQ(page.Value().next, int64_t(2));
    REQUIRE_EQ(page.Value().rows[1].Line(), std::string("2026-11-11T11:00:00Z  operator release submission s-00000001: good"));
    const auto check = client.VerifyAudit();
    REQUIRE(check.Ok());
    REQUIRE(check.Value().ok);
    REQUIRE_EQ(check.Value().rows, int64_t(2));
}

TEST(health_is_plain_text_without_the_token) {
    FakeService fake;
    fake.answers["GET /healthz"] = "ok";
    OperatorClient client(TestSettings(), fake.Call());
    const auto h = client.Health();
    REQUIRE(h.Ok());
    REQUIRE_EQ(h.Value(), std::string("ok"));
    REQUIRE_EQ(fake.calls.back(), std::string("GET https://market.example/healthz |  | "));
}

TEST(the_settings_file_round_trips_and_the_environment_overrides_it) {
    const std::string dir = TempDir();
    const std::string path = UltraCanvas::PathToUtf8(UltraCanvas::PathFromUtf8(dir) / "nested" / "settings.json");
    const auto none = OperatorSettings::Load(path);
    REQUIRE(none.Ok());
    REQUIRE(!none.Value().Complete());
    OperatorSettings s;
    s.url = "https://market.example/";
    s.token = "tok";
    s.name = "Alex";
    REQUIRE(s.Save(path).Ok());
    const auto back = OperatorSettings::Load(path);
    REQUIRE(back.Ok());
    REQUIRE_EQ(back.Value().url, std::string("https://market.example"));   // the trailing slash dropped
    REQUIRE_EQ(back.Value().token, std::string("tok"));
    REQUIRE_EQ(back.Value().name, std::string("Alex"));
    REQUIRE(back.Value().Complete());
#ifndef _WIN32
    const auto perms = std::filesystem::status(UltraCanvas::PathFromUtf8(path)).permissions();
    REQUIRE((perms & std::filesystem::perms::group_read) == std::filesystem::perms::none);
    REQUIRE((perms & std::filesystem::perms::others_read) == std::filesystem::perms::none);
#endif
    const OperatorSettings withEnv = back.Value().WithEnvironment();
    REQUIRE_EQ(withEnv.token, std::string("tok"));   // nothing set in the test's environment
    std::error_code ec;
    std::filesystem::remove_all(UltraCanvas::PathFromUtf8(dir), ec);
}

TEST(the_age_of_a_timestamp) {
    const int64_t midnight = 1794355200;   // 2026-11-11T00:00:00Z
    REQUIRE_EQ(Age("2026-11-11T00:00:00Z", midnight + 3 * 86400 + 7200), std::string("3 d"));
    REQUIRE_EQ(Age("2026-11-11T00:00:00Z", midnight + 6 * 3600 + 60), std::string("6 h"));
    REQUIRE_EQ(Age("2026-11-11T00:00:00Z", midnight + 12 * 60), std::string("12 min"));
    REQUIRE_EQ(Age("2026-11-11T00:00:00Z", midnight - 5), std::string("0 min"));
    REQUIRE_EQ(Age("2026-03-01T12:00:00Z", 1772366400 + 86400), std::string("1 d"));   // the day after a leap-year February
    const int64_t now = midnight;
    REQUIRE_EQ(Age("not a time", now), std::string());
    REQUIRE_EQ(Age("", now), std::string());
}
