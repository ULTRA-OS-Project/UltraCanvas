// Tests/UltraStore/test_report.cpp
// Reporting a problem to the marketplace service, against an HTTP call that
// answers from a map: the report sent with its target, kind, text and the
// reporter, the receipt, the outcome fetched with the secret, and the
// refusals.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"
#include "helpers.h"

#include "Report.h"

#include "DataFormats/UltraCanvasJSON.h"

#include <map>

using namespace UltraStore;

namespace {

struct FakeMarket {
    std::map<std::string, std::string> answers;   // "METHOD url" -> body
    std::vector<std::string> calls;
    int status = 200;
    HttpCall Call() {
        return [this](const std::string& method, const std::string& url, const std::string& body) {
            calls.push_back(method + " " + url + " " + body);
            HttpReply r;
            auto it = answers.find(method + " " + url);
            if (it == answers.end()) { r.status = 404; r.body = R"({"error":"no_such_report","error_description":"No such report"})"; return r; }
            r.status = status;
            r.body = it->second;
            return r;
        };
    }
};

} // namespace

TEST(a_report_is_sent_and_its_outcome_fetched) {
    FakeMarket m;
    m.answers["POST https://market.example/v1/reports"] = R"({"id":"r-00000003","secret":"s3cret","state":"open","created_at":"2026-11-11T12:00:00Z"})";
    REQUIRE(SendReport("", "app", "org.x.app", "crash", "It crashes.", "", m.Call()).error.code == ErrorCode::NotAvailable);
    REQUIRE(SendReport("https://market.example", "app", "org.x.app", "crash", "", "", m.Call()).error.code == ErrorCode::InvalidArgument);
    const auto sent = SendReport("https://market.example/", "app", "org.x.app", "crash", "It crashes on start.", "lic-77", m.Call());
    REQUIRE(sent);
    REQUIRE_EQ(sent.Value().id, std::string("r-00000003"));
    REQUIRE_EQ(sent.Value().secret, std::string("s3cret"));
    REQUIRE_EQ(m.calls.size(), size_t(1));
    const UltraCanvas::JSONValue body = UltraCanvas::JSON::Parse(m.calls[0].substr(m.calls[0].find('{')));
    REQUIRE_EQ(body.Get("target_kind").GetString(), std::string("app"));
    REQUIRE_EQ(body.Get("target").GetString(), std::string("org.x.app"));
    REQUIRE_EQ(body.Get("kind").GetString(), std::string("crash"));
    REQUIRE_EQ(body.Get("text").GetString(), std::string("It crashes on start."));
    REQUIRE_EQ(body.Get("reporter").GetString(), std::string("lic-77"));
    // Anonymous: no reporter field; an empty kind is "other".
    REQUIRE(SendReport("https://market.example", "app", "org.x.app", "", "Something.", "", m.Call()));
    const UltraCanvas::JSONValue anon = UltraCanvas::JSON::Parse(m.calls[1].substr(m.calls[1].find('{')));
    REQUIRE(!anon.Contains("reporter"));
    REQUIRE_EQ(anon.Get("kind").GetString(), std::string("other"));
    // The outcome, with the secret; the wrong secret is a 404.
    m.answers["GET https://market.example/v1/reports/r-00000003/outcome?secret=s3cret"] =
        R"({"id":"r-00000003","state":"resolved","outcome":"Fixed in 1.0.1.","resolved_at":"2026-11-12T09:00:00Z"})";
    const auto outcome = FetchReportOutcome("https://market.example", "r-00000003", "s3cret", m.Call());
    REQUIRE(outcome);
    REQUIRE(outcome.Value().Closed());
    REQUIRE_EQ(outcome.Value().outcome, std::string("Fixed in 1.0.1."));
    REQUIRE(FetchReportOutcome("https://market.example", "r-00000003", "wrong", m.Call()).error.code == ErrorCode::NotFound);
    // A receipt without a secret is no receipt; the service's words come back on a refusal.
    m.answers["POST https://market.example/v1/reports"] = R"({"id":"r-1"})";
    REQUIRE(SendReport("https://market.example", "app", "org.x.app", "crash", "x", "", m.Call()).error.code == ErrorCode::ParseError);
    m.status = 422;
    m.answers["POST https://market.example/v1/reports"] = R"({"error":"bad","error_description":"The kind is too long"})";
    const auto refused = SendReport("https://market.example", "app", "org.x.app", "crash", "x", "", m.Call());
    REQUIRE(!refused);
    REQUIRE(refused.error.message.find("too long") != std::string::npos);
}
