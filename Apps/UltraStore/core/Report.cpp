// Apps/UltraStore/core/Report.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "Report.h"

#include "DataFormats/UltraCanvasJSON.h"

namespace UltraStore {

namespace {

std::string TrimSlash(std::string s) {
    while (!s.empty() && s.back() == '/') s.pop_back();
    return s;
}

std::string Encode(const std::string& text) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : text) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += static_cast<char>(c);
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
}

Result<UltraCanvas::JSONValue> Answer(const HttpReply& reply, const std::string& what) {
    using R = Result<UltraCanvas::JSONValue>;
    if (!reply.error.empty() || reply.status == 0) return R::Failure(ErrorCode::NetworkError, "The marketplace service could not be reached", reply.error);
    UltraCanvas::JSONParseResult parse;
    const UltraCanvas::JSONValue v = UltraCanvas::JSON::Parse(reply.body, &parse);
    if (reply.status < 200 || reply.status >= 300) {
        const std::string description = parse.success && v.IsObject() ? v.Get("error_description").GetString() : std::string();
        return R::Failure(reply.status == 404 ? ErrorCode::NotFound : ErrorCode::NetworkError,
                          description.empty() ? what + ": the marketplace service answered HTTP " + std::to_string(reply.status) : description,
                          "HTTP " + std::to_string(reply.status));
    }
    if (!parse.success || !v.IsObject()) return R::Failure(ErrorCode::ParseError, what + ": the marketplace service's answer is not JSON", parse.errorMessage);
    return R::Succeed(v);
}

} // namespace

Result<ReportReceipt> SendReport(const std::string& marketUrl, const std::string& targetKind, const std::string& target, const std::string& kind,
                                 const std::string& text, const std::string& reporter, const HttpCall& http) {
    using R = Result<ReportReceipt>;
    if (marketUrl.empty()) return R::Failure(ErrorCode::NotAvailable, "The catalog names no marketplace service to report to");
    if (text.empty()) return R::Failure(ErrorCode::InvalidArgument, "A report says what is wrong");
    UltraCanvas::JSONValue body = UltraCanvas::JSONValue::MakeObject();
    body.Set("target_kind", targetKind);
    body.Set("target", target);
    body.Set("kind", kind.empty() ? std::string("other") : kind);
    body.Set("text", text);
    if (!reporter.empty()) body.Set("reporter", reporter);
    const auto answer = Answer(http("POST", TrimSlash(marketUrl) + "/v1/reports", UltraCanvas::JSON::Serialize(body)), "report");
    if (!answer) return R::Failure(answer.error.code, answer.error.message, answer.error.detail);
    ReportReceipt receipt;
    receipt.id = answer.Value().Get("id").GetString();
    receipt.secret = answer.Value().Get("secret").GetString();
    receipt.createdAt = answer.Value().Get("created_at").GetString();
    if (receipt.id.empty() || receipt.secret.empty()) return R::Failure(ErrorCode::ParseError, "The marketplace service's receipt lacks the id or the secret");
    return R::Succeed(std::move(receipt));
}

Result<ReportOutcome> FetchReportOutcome(const std::string& marketUrl, const std::string& id, const std::string& secret, const HttpCall& http) {
    using R = Result<ReportOutcome>;
    const auto answer = Answer(http("GET", TrimSlash(marketUrl) + "/v1/reports/" + Encode(id) + "/outcome?secret=" + Encode(secret), ""), "outcome");
    if (!answer) return R::Failure(answer.error.code, answer.error.message, answer.error.detail);
    ReportOutcome out;
    out.id = answer.Value().Get("id").GetString();
    out.state = answer.Value().Get("state").GetString();
    out.outcome = answer.Value().Get("outcome").GetString();
    out.resolvedAt = answer.Value().Get("resolved_at").GetString();
    return R::Succeed(std::move(out));
}

} // namespace UltraStore
