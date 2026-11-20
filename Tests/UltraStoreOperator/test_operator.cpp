// Tests/UltraStoreOperator/test_operator.cpp
// The operator's client against a call that answers from a map: the bearer
// header on every request, the bodies the service's models read, the
// records parsed from its answers (a merchant, a submission with its
// checks and decisions, a report with its reporter, a notice, the policy,
// the audit rows), the publish report, the settings file round trip and
// the environment over it, the age of a timestamp.
// Version: 0.3.0 - the second factor at the sign-in, the password reset, the build downloaded
// Version: 0.2.0 - the sign-in, the session as the token, the operator accounts, the account service on the session
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "Operator.h"

#include "DataFormats/UltraCanvasJSON.h"
#include "UltraCanvasPathUtf8.h"

#include <filesystem>
#include <fstream>
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
    s.accountUrl = "https://account.example/";
    s.accountToken = "account-secret";
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
                            R"("findings":{"scans":["no package store: the packages stay on the merchant's server, unscanned"],"listing":["no category","1 screenshot(s)"]},"checked_at":"2026-11-10T07:59:00Z",)"
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
    // The pipeline's findings per check, and when it ran.
    REQUIRE_EQ(s.findings.size(), size_t(2));
    REQUIRE(s.FindingsOf("listing") != nullptr);
    REQUIRE_EQ(s.FindingsOf("listing")->size(), size_t(2));
    REQUIRE_EQ((*s.FindingsOf("listing"))[0], std::string("no category"));
    REQUIRE(s.FindingsOf("entry") == nullptr);
    REQUIRE_EQ(s.checkedAt, std::string("2026-11-10T07:59:00Z"));
    // The pipeline run again: a POST with an empty body, the submission back.
    fake.answers["POST /v1/submissions/s-00000007/checks"] = sub;
    const auto again = client.RunChecks("s-00000007");
    REQUIRE(again.Ok());
    REQUIRE_EQ(again.Value().id, std::string("s-00000007"));
    REQUIRE_EQ(fake.calls.back(), std::string("POST https://market.example/v1/submissions/s-00000007/checks | Bearer op-secret | {}"));

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
    s.accountUrl = "https://account.example/";
    s.accountToken = "acc";
    REQUIRE(s.Save(path).Ok());
    const auto back = OperatorSettings::Load(path);
    REQUIRE(back.Ok());
    REQUIRE_EQ(back.Value().url, std::string("https://market.example"));   // the trailing slash dropped
    REQUIRE_EQ(back.Value().token, std::string("tok"));
    REQUIRE_EQ(back.Value().name, std::string("Alex"));
    REQUIRE_EQ(back.Value().accountUrl, std::string("https://account.example"));
    REQUIRE_EQ(back.Value().accountToken, std::string("acc"));
    REQUIRE(back.Value().Accounts());
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

TEST(the_users_are_read_from_the_account_service_with_its_own_token) {
    FakeService fake;
    fake.answers["GET /v1/users?state=suspended&q=anna"] = R"({"users":[{"id":"u-1","email":"an***@example.org","display_name":"Anna K.","handle":"annak","country":"DE","state":"suspended",
        "created_at":"2026-11-01T10:00:00Z","two_factor":true,"devices":3,"licences":2,"request":"archive","suspended_reason":"review spam","suspended_until":"2026-12-19"}],
        "stats":{"accounts":48212,"new_in_30_days":1204,"two_factor_percent":61,"archive_requests":2,"archive_requests_due":1,"deletion_requests":1,"suspended":6}})";
    fake.answers["GET /v1/users/u-1"] = R"({"id":"u-1","email":"an***@example.org","display_name":"Anna K.","handle":"annak","state":"active","created_at":"2026-11-01T10:00:00Z","two_factor":false,
        "devices":[{"id":"d-1","device_name":"Anna's laptop","os":"linux","client":"UltraStore 0.2","last_seen":"2026-11-19T09:00:00Z","last_ip":"10.0.0.1"}],
        "licences":[{"id":"lic-1","app":"org.example.app","order":"o-1","issued":"2026-11-11","seats":3,"seats_used":1}],
        "notices":[{"id":"n-1","kind":"notice","subject":"About your review","text":"Thank you.","created_at":"2026-11-18T10:00:00Z","created_by":"operator","read_at":""}],
        "archives":[{"id":"a-1","state":"built","due":"2026-11-20","requested_at":"2026-11-19T08:00:00Z","released_at":""}],
        "history":[{"id":7,"at":"2026-11-18T10:00:00Z","actor":"operator","action":"notify","target_kind":"user","target":"u-1","hash":"x"}],
        "sign_ins":[{"at":"2026-11-19T09:00:00Z","ip":"10.0.0.1","device":"Anna's laptop","ok":true,"reason":""}]})";
    fake.answers["POST /v1/users/u-1/suspend"] = R"({"user":{"id":"u-1","state":"suspended","suspended_until":"2026-12-19"}})";
    fake.answers["POST /v1/users/u-1/notice"] = R"({"notice":{"id":"n-2","kind":"warning","subject":"Review spam","text":"Stop.","created_at":"2026-11-19T10:00:00Z","created_by":"operator"}})";
    fake.answers["POST /v1/users/u-1/signout-all"] = R"({"signed_out":3})";
    fake.answers["POST /v1/archives/a-1/release"] = R"({"archive":{"id":"a-1","state":"released","due":"2026-11-20"}})";
    fake.answers["POST /v1/users/u-1/delete"] = R"({"deleted":true})";
    OperatorClient client(TestSettings(), fake.Call());

    const auto page = client.Users("suspended", "anna");
    REQUIRE(page.Ok());
    REQUIRE_EQ(fake.calls.back(), std::string("GET https://account.example/v1/users?state=suspended&q=anna | Bearer account-secret | "));
    REQUIRE_EQ(page.Value().users.size(), size_t(1));
    const User& u = page.Value().users[0];
    REQUIRE_EQ(u.Name(), std::string("Anna K."));
    REQUIRE_EQ(u.email, std::string("an***@example.org"));
    REQUIRE(u.twoFactor && u.devices == 3 && u.licences == 2 && u.request == "archive");
    REQUIRE_EQ(u.suspendedUntil, std::string("2026-12-19"));
    REQUIRE_EQ(page.Value().stats.accounts, 48212);
    REQUIRE_EQ(page.Value().stats.twoFactorPercent, 61);

    const auto file = client.GetUser("u-1");
    REQUIRE(file.Ok());
    REQUIRE_EQ(file.Value().devices.size(), size_t(1));
    REQUIRE_EQ(file.Value().devices[0].name, std::string("Anna's laptop"));
    REQUIRE_EQ(file.Value().licences[0].seatsUsed, 1);
    REQUIRE_EQ(file.Value().notices[0].createdBy, std::string("operator"));
    REQUIRE_EQ(file.Value().archives[0].state, std::string("built"));
    REQUIRE_EQ(file.Value().history[0].action, std::string("notify"));
    REQUIRE(file.Value().signIns[0].ok);
    REQUIRE_EQ(file.Value().user.devices, 1);   // counted from the list in the file

    const auto suspended = client.SuspendUser("u-1", "review spam", 30);
    REQUIRE(suspended.Ok());
    REQUIRE(fake.calls.back().find(R"({"reason":"review spam","days":30})") != std::string::npos);
    REQUIRE_EQ(suspended.Value().state, std::string("suspended"));
    const auto notice = client.NotifyUser("u-1", "warning", "Review spam", "Stop.");
    REQUIRE(notice.Ok() && notice.Value().id == "n-2");
    REQUIRE(fake.calls.back().find(R"("kind":"warning")") != std::string::npos);
    const auto out = client.SignOutUser("u-1", "stolen laptop");
    REQUIRE(out.Ok() && out.Value() == 3);
    const auto released = client.ReleaseArchive("a-1");
    REQUIRE(released.Ok() && released.Value().state == "released");
    REQUIRE(client.DeleteUser("u-1", "requested").Ok());

    // No account service: refused before any call, the marketplace untouched.
    OperatorSettings s = TestSettings();
    s.accountUrl.clear();
    const size_t calls = fake.calls.size();
    REQUIRE(!OperatorClient(s, fake.Call()).Users().Ok());
    REQUIRE_EQ(fake.calls.size(), calls);
}

TEST(an_application_for_a_shop_is_listed_approved_and_rejected) {
    FakeService fake;
    fake.answers["GET /v1/applications?state=applied"] = R"({"applications":[{"id":"ap-000001","name":"Pixelwerk","contact":"mail@pixelwerk.example","email":"jo@pixelwerk.example",
        "website":"https://pixelwerk.example","description":"Games.","key":")" + std::string(64, 'b') + R"(","state":"applied","message":"","created_at":"2026-11-19T10:00:00Z","decided_at":"","decided_by":"","merchant":""}]})";
    fake.answers["POST /v1/applications/ap-000001/approve"] = R"({"application":{"id":"ap-000001","state":"approved","merchant":"pixelwerk"},
        "merchant":{"id":"pixelwerk","name":"Pixelwerk","contact":"mail@pixelwerk.example","website":"https://pixelwerk.example","description":"Games.","key":")" + std::string(64, 'b') +
        R"(","created_at":"2026-11-19T11:00:00Z","standing":"active","strikes":0,"terms":{"in_force":0,"accepted":0}}})";
    fake.answers["POST /v1/applications/ap-000002/reject"] = R"({"application":{"id":"ap-000002","name":"Copycat","state":"rejected","message":"The name is another publisher's"}})";
    OperatorClient client(TestSettings(), fake.Call());
    const auto list = client.Applications("applied");
    REQUIRE(list.Ok());
    REQUIRE_EQ(list.Value().size(), size_t(1));
    REQUIRE(list.Value()[0].Open());
    REQUIRE_EQ(list.Value()[0].email, std::string("jo@pixelwerk.example"));
    REQUIRE_EQ(list.Value()[0].website, std::string("https://pixelwerk.example"));
    const auto approved = client.ApproveApplication("ap-000001", "pixelwerk", "Welcome");
    REQUIRE(approved.Ok());
    REQUIRE_EQ(approved.Value().id, std::string("pixelwerk"));
    REQUIRE_EQ(approved.Value().website, std::string("https://pixelwerk.example"));
    REQUIRE_EQ(fake.calls.back().substr(fake.calls.back().rfind(" | ") + 3), std::string(R"({"id":"pixelwerk","message":"Welcome"})"));
    const auto rejected = client.RejectApplication("ap-000002", "The name is another publisher's");
    REQUIRE(rejected.Ok());
    REQUIRE_EQ(rejected.Value().state, std::string("rejected"));
    REQUIRE(fake.calls.back().find(R"({"message":"The name is another publisher's"})") != std::string::npos);
}


TEST(an_operator_signs_in_and_the_session_is_the_token_at_both_services) {
    FakeService fake;
    fake.answers["POST /v1/operators/signin"] = R"({"token":"op1.payload.mac","hours":12,"operator":{"id":"op-0001","email":"alex@ultra-os.example","name":"Alex","role":"admin",
        "created_at":"2026-11-19T10:00:00Z","created_by":"operator","disabled_at":"","last_signin":"2026-11-19T12:00:00Z","active":true}})";
    fake.answers["GET /v1/operators/me"] = R"({"id":"op-0001","name":"Alex","role":"admin","bootstrap":false,"permissions":["merchants.manage","operators.manage","publish"]})";
    fake.answers["GET /v1/operators"] = R"({"operators":[{"id":"op-0001","email":"alex@ultra-os.example","name":"Alex","role":"admin","created_at":"2026-11-19T10:00:00Z","created_by":"operator","disabled_at":"","last_signin":"","active":true},
        {"id":"op-0002","email":"sam@ultra-os.example","name":"Sam","role":"support","created_at":"2026-11-19T11:00:00Z","created_by":"Alex","disabled_at":"2026-11-19T13:00:00Z","last_signin":"","active":false}],"roles":["admin","reviewer","support","auditor"]})";
    fake.answers["POST /v1/operators"] = R"({"id":"op-0003","email":"ada@ultra-os.example","name":"Ada","role":"auditor","created_at":"2026-11-19T14:00:00Z","created_by":"Alex","disabled_at":"","last_signin":"","active":true})";
    fake.answers["PUT /v1/operators/op-0002"] = R"({"id":"op-0002","email":"sam@ultra-os.example","name":"Sam","role":"reviewer","created_at":"2026-11-19T11:00:00Z","created_by":"Alex","disabled_at":"","last_signin":"","active":true})";
    fake.answers["GET /v1/users"] = R"({"users":[],"stats":{"accounts":0}})";

    // The sign-in needs the URL alone, carries no bearer, and answers the session.
    OperatorSettings s;
    s.url = "https://market.example";
    OperatorClient fresh(s, fake.Call());
    REQUIRE(!fresh.SignIn("alex@ultra-os.example", "").Ok());
    const auto in = fresh.SignIn("alex@ultra-os.example", "a long enough password");
    REQUIRE(in.Ok());
    REQUIRE_EQ(in.Value().token, std::string("op1.payload.mac"));
    REQUIRE_EQ(in.Value().account.role, std::string("admin"));
    REQUIRE_EQ(in.Value().hours, 12);
    REQUIRE_EQ(fake.calls.back(), std::string(R"(POST https://market.example/v1/operators/signin |  | {"email":"alex@ultra-os.example","password":"a long enough password"})"));

    // The session is the token from then on, and reaches the account service without a token of its own.
    s.token = in.Value().token;
    s.name = in.Value().account.Name();
    s.role = in.Value().account.role;
    s.accountUrl = "https://account.example";
    REQUIRE(s.SignedIn());
    REQUIRE(s.Accounts());
    REQUIRE_EQ(s.AccountBearer(), std::string("op1.payload.mac"));
    OperatorClient client(s, fake.Call());
    REQUIRE(client.Users().Ok());
    REQUIRE_EQ(fake.calls.back(), std::string("GET https://account.example/v1/users | Bearer op1.payload.mac | "));
    s.accountToken = "account-secret";
    REQUIRE_EQ(s.AccountBearer(), std::string("account-secret"));   // a token of its own wins
    OperatorSettings plain = s;
    plain.token = "op-static";
    plain.accountToken.clear();
    REQUIRE(!plain.SignedIn());
    REQUIRE(!plain.Accounts());   // the static token is the marketplace's alone

    const auto me = client.Me();
    REQUIRE(me.Ok());
    REQUIRE(!me.Value().bootstrap);
    REQUIRE(me.Value().May("operators.manage"));
    REQUIRE(!me.Value().May("users.manage"));
    const auto list = client.Operators();
    REQUIRE(list.Ok());
    REQUIRE_EQ(list.Value().size(), size_t(2));
    REQUIRE(list.Value()[0].active);
    REQUIRE(!list.Value()[1].active);
    REQUIRE_EQ(list.Value()[1].createdBy, std::string("Alex"));
    const auto added = client.AddOperator("ada@ultra-os.example", "Ada", "auditor", "a long enough password");
    REQUIRE(added.Ok());
    REQUIRE_EQ(added.Value().id, std::string("op-0003"));
    REQUIRE(fake.calls.back().find(R"({"email":"ada@ultra-os.example","name":"Ada","role":"auditor","password":"a long enough password"})") != std::string::npos);
    const auto changed = client.UpdateOperator("op-0002", "", "reviewer", "", 0);
    REQUIRE(changed.Ok());
    REQUIRE_EQ(changed.Value().role, std::string("reviewer"));
    REQUIRE(fake.calls.back().find(R"({"role":"reviewer","disabled":false})") != std::string::npos);
    REQUIRE(client.UpdateOperator("op-0002", "Samira", "", "").Ok());
    REQUIRE(fake.calls.back().find(R"({"name":"Samira"})") != std::string::npos);

    // The settings file keeps the role with the session.
    const std::string dir = TempDir();
    const std::string path = dir + "/settings.json";
    REQUIRE(s.Save(path).Ok());
    const auto back = OperatorSettings::Load(path);
    REQUIRE(back.Ok());
    REQUIRE_EQ(back.Value().role, std::string("admin"));
    REQUIRE_EQ(back.Value().token, std::string("op1.payload.mac"));
    std::filesystem::remove_all(UltraCanvas::PathFromUtf8(dir));
}


TEST(the_second_factor_the_password_reset_and_the_build_download) {
    FakeService fake;
    fake.answers["POST /v1/operators/signin"] = R"({"token":"op1.p.m","hours":12,"operator":{"id":"op-0001","email":"alex@ultra-os.example","name":"Alex","role":"admin","active":true,"two_factor":true,"recovery_codes_left":7}})";
    fake.answers["POST /v1/operators/me/two-factor/setup"] = R"({"secret":"GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ","otpauth_url":"otpauth://totp/UltraStore%20operations:alex?secret=GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ"})";
    fake.answers["POST /v1/operators/me/two-factor/enable"] = R"({"two_factor":true,"recovery_codes":["abcd2345","efgh6789"]})";
    fake.answers["POST /v1/operators/me/two-factor/disable"] = R"({"two_factor":false})";
    fake.answers["POST /v1/operators/me/two-factor/recovery-codes"] = R"({"recovery_codes":["jkmn2345"]})";
    fake.answers["POST /v1/operators/password/forgot"] = R"({"sent":true,"minutes":10})";
    fake.answers["POST /v1/operators/password/reset"] = R"({"reset":true})";
    fake.answers["PUT /v1/operators/op-0002"] = R"({"id":"op-0002","email":"sam@ultra-os.example","name":"Sam","role":"support","active":true,"two_factor":false,"recovery_codes_left":0})";
    OperatorSettings s;
    s.url = "https://market.example";
    OperatorClient fresh(s, fake.Call());
    // The code: six digits are the authenticator's, anything else a recovery code; the account's two-factor state comes back.
    const auto in = fresh.SignIn("alex@ultra-os.example", "a long enough password", "123 456");
    REQUIRE(in.Ok());
    REQUIRE(in.Value().account.twoFactor);
    REQUIRE_EQ(in.Value().account.recoveryCodesLeft, 7);
    REQUIRE(fake.calls.back().find(R"("totp":"123456")") != std::string::npos);
    REQUIRE(fresh.SignIn("alex@ultra-os.example", "a long enough password", "abcd2345").Ok());
    REQUIRE(fake.calls.back().find(R"("recovery_code":"abcd2345")") != std::string::npos);
    // Without the code, the service's refusal names what is missing in the detail.
    fake.status = 401;
    fake.answers["POST /v1/operators/signin"] = R"({"error":"two_factor_required","error_description":"The six-digit code"})";
    const auto refused = fresh.SignIn("alex@ultra-os.example", "a long enough password");
    REQUIRE(!refused.Ok());
    REQUIRE_EQ(refused.error.detail, std::string("two_factor_required"));
    fake.status = 200;
    // One's own second factor, with the bearer.
    s.token = "op1.p.m";
    OperatorClient client(s, fake.Call());
    const auto setup = client.TwoFactorSetup();
    REQUIRE(setup.Ok());
    REQUIRE_EQ(setup.Value().secret, std::string("GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ"));
    REQUIRE(fake.calls.back().rfind("POST https://market.example/v1/operators/me/two-factor/setup | Bearer op1.p.m | ", 0) == 0);
    const auto codes = client.TwoFactorEnable("654321");
    REQUIRE(codes.Ok());
    REQUIRE_EQ(codes.Value().size(), size_t(2));
    REQUIRE(fake.calls.back().find(R"({"code":"654321"})") != std::string::npos);
    REQUIRE(client.NewRecoveryCodes("654321").Ok());
    REQUIRE(client.TwoFactorDisable("654321").Ok());
    // A forgotten password needs no bearer; the reset carries the mailed code and the second factor.
    const auto minutes = fresh.ForgotPassword("alex@ultra-os.example");
    REQUIRE(minutes.Ok() && minutes.Value() == 10);
    REQUIRE(fake.calls.back().find("/v1/operators/password/forgot |  | ") != std::string::npos);
    REQUIRE(fresh.ResetPassword("alex@ultra-os.example", "111222", "a brand new password", "333444").Ok());
    REQUIRE(fake.calls.back().find(R"({"email":"alex@ultra-os.example","code":"111222","new_password":"a brand new password","totp":"333444"})") != std::string::npos);
    REQUIRE(!fresh.ResetPassword("alex@ultra-os.example", "111222", "short").Ok());
    // An admin clears another's second factor.
    const auto cleared = client.ResetOperatorTwoFactor("op-0002");
    REQUIRE(cleared.Ok());
    REQUIRE(!cleared.Value().twoFactor);
    REQUIRE(fake.calls.back().find(R"(PUT https://market.example/v1/operators/op-0002 | Bearer op1.p.m | {"two_factor":false})") != std::string::npos);
    // The build: the file name from the package, the bytes into the folder through the download call.
    std::vector<std::string> downloads;
    DownloadCall download = [&downloads](const std::string& url, const std::string& authorization, const std::string& destinationUtf8) {
        downloads.push_back(url + " | " + authorization);
        std::ofstream out(UltraCanvas::PathFromUtf8(destinationUtf8), std::ios::binary);
        out << "PK the build";
        MarketReply r;
        r.status = 200;
        return r;
    };
    OperatorClient reviewer(s, fake.Call(), download);
    const std::string dir = TempDir();
    JSONValue package = UltraCanvas::JSON::Parse(R"({"file":"restricted/abc.tar.zst","sha256":"ABCDEF","size":12,"uploaded":true})");
    REQUIRE_EQ(PackageFileName(package), std::string("abcdef.tar.zst"));
    REQUIRE_EQ(PackageFileName(UltraCanvas::JSON::Parse(R"({"sha256":"ff","format":"zip"})")), std::string("ff.zip"));
    REQUIRE_EQ(PackageFileName(UltraCanvas::JSON::Parse(R"({"sha256":"ff","url":"https://x/y"})")), std::string("ff.bin"));
    const auto got = reviewer.DownloadPackage("s-00000001", package, dir);
    REQUIRE(got.Ok());
    REQUIRE_EQ(got.Value().size, uint64_t(12));
    REQUIRE(got.Value().fileUtf8.find("abcdef.tar.zst") != std::string::npos);
    REQUIRE_EQ(downloads.size(), size_t(1));
    REQUIRE_EQ(downloads[0], std::string("https://market.example/v1/submissions/s-00000001/packages/ABCDEF | Bearer op1.p.m"));
    // A refusal leaves no file behind.
    DownloadCall refusing = [](const std::string&, const std::string&, const std::string& destinationUtf8) {
        std::ofstream out(UltraCanvas::PathFromUtf8(destinationUtf8), std::ios::binary);
        out << "{}";
        MarketReply r;
        r.status = 404;
        r.body = R"({"error":"not_uploaded","error_description":"The merchant has not uploaded that package"})";
        return r;
    };
    const auto missing = OperatorClient(s, fake.Call(), refusing).DownloadPackage("s-00000001", package, dir + "/none");
    REQUIRE(!missing.Ok());
    REQUIRE(missing.error.code == UltraStore::ErrorCode::NotFound);
    REQUIRE(!std::filesystem::exists(UltraCanvas::PathFromUtf8(dir + "/none/abcdef.tar.zst")));
    std::filesystem::remove_all(UltraCanvas::PathFromUtf8(dir));
}
