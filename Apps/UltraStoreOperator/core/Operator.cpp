// Apps/UltraStoreOperator/core/Operator.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "Operator.h"

#include "UltraCanvasDataFolder.h"
#include "UltraCanvasPathUtf8.h"

#include <cctype>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace UltraStoreOperator {

using UltraCanvas::JSONValue;

namespace {

namespace fs = std::filesystem;

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

std::vector<std::string> Strings(const JSONValue& v) {
    std::vector<std::string> out;
    for (const JSONValue& e : v.GetElements()) out.push_back(e.GetString());
    return out;
}

JSONValue StringsJson(const std::vector<std::string>& list) {
    JSONValue out = JSONValue::MakeArray();
    for (const std::string& s : list) out.Append(JSONValue(s));
    return out;
}

Acceptance AcceptanceOf(const JSONValue& v) {
    Acceptance a;
    a.merchant = v.Get("merchant").GetString();
    a.version = static_cast<int>(v.Get("version").GetInteger(0));
    a.acceptedBy = v.Get("accepted_by").GetString();
    a.acceptedAt = v.Get("accepted_at").GetString();
    return a;
}

Decision DecisionOf(const JSONValue& v) {
    Decision d;
    d.action = v.Get("action").GetString();
    d.message = v.Get("message").GetString();
    d.actor = v.Get("actor").GetString();
    d.at = v.Get("at").GetString();
    d.sequence = static_cast<int>(v.Get("sequence").GetInteger(0));
    return d;
}

TermsVersion TermsOf(const JSONValue& v) {
    TermsVersion t;
    t.version = static_cast<int>(v.Get("version").GetInteger(0));
    t.text = v.Get("text").GetString();
    t.changes = v.Get("changes").GetString();
    t.inForceFrom = v.Get("in_force_from").GetString();
    t.createdAt = v.Get("created_at").GetString();
    t.createdBy = v.Get("created_by").GetString();
    return t;
}

// 2026-11-11T18:00:00Z as seconds since the epoch, 0 when it does not parse.
int64_t ParseIso(const std::string& at) {
    if (at.size() < 19) return 0;
    auto digits = [&](size_t from, size_t count) {
        int value = 0;
        for (size_t i = from; i < from + count; ++i) {
            if (i >= at.size() || !std::isdigit(static_cast<unsigned char>(at[i]))) return -1;
            value = value * 10 + (at[i] - '0');
        }
        return value;
    };
    const int year = digits(0, 4), month = digits(5, 2), day = digits(8, 2), hour = digits(11, 2), minute = digits(14, 2), second = digits(17, 2);
    if (year < 0 || month < 1 || month > 12 || day < 1 || day > 31 || hour < 0 || minute < 0 || second < 0) return 0;
    // Days since the epoch, the civil-from-days arithmetic (no timegm on every platform).
    const int y = year - (month <= 2 ? 1 : 0);
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * static_cast<unsigned>(month + (month > 2 ? -3 : 9)) + 2) / 5 + static_cast<unsigned>(day) - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const int64_t days = static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
    return days * 86400 + hour * 3600 + minute * 60 + second;
}

} // namespace

// ===========================================================================
// The settings
// ===========================================================================
std::string OperatorSettings::DefaultPath() {
    return UltraCanvas::PathToUtf8(UltraCanvas::UltraCanvasDataFolderRoot() / "UltraStoreOperator" / "settings.json");
}

Result<OperatorSettings> OperatorSettings::Load(const std::string& pathUtf8) {
    using R = Result<OperatorSettings>;
    OperatorSettings s;
    std::ifstream in(UltraCanvas::PathFromUtf8(pathUtf8), std::ios::binary);
    if (!in) return R::Succeed(s);   // not there yet: empty settings
    std::ostringstream buffer;
    buffer << in.rdbuf();
    UltraCanvas::JSONParseResult parse;
    const JSONValue v = UltraCanvas::JSON::Parse(buffer.str(), &parse);
    if (!parse.success || !v.IsObject()) return R::Failure(ErrorCode::ParseError, "The settings file is not JSON: " + pathUtf8, parse.errorMessage);
    s.url = TrimSlash(v.Get("url").GetString());
    s.token = v.Get("token").GetString();
    s.name = v.Get("name").GetString();
    return R::Succeed(s);
}

Result<void> OperatorSettings::Save(const std::string& pathUtf8) const {
    const fs::path path = UltraCanvas::PathFromUtf8(pathUtf8);
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) return Result<void>::Failure(ErrorCode::IoError, "Cannot create " + UltraCanvas::PathToUtf8(path.parent_path()), ec.message());
    JSONValue v = JSONValue::MakeObject();
    v.Set("url", JSONValue(TrimSlash(url)));
    v.Set("token", JSONValue(token));
    v.Set("name", JSONValue(name));
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) return Result<void>::Failure(ErrorCode::IoError, "Cannot write " + pathUtf8);
        UltraCanvas::JSONSerializeOptions options;
        options.pretty = true;
        const std::string text = UltraCanvas::JSON::Serialize(v, options) + "\n";
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!out) return Result<void>::Failure(ErrorCode::IoError, "Cannot write " + pathUtf8);
    }
    // The token is a secret: the file is the user's alone.
    fs::permissions(path, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace, ec);
    return Result<void>::Succeed();
}

OperatorSettings OperatorSettings::WithEnvironment() const {
    OperatorSettings s = *this;
    const std::string url = UltraCanvas::GetEnvUtf8("ULTRASTORE_OPERATOR_URL");       // path-string-ok: GetEnvUtf8 is the UTF-8 read
    const std::string token = UltraCanvas::GetEnvUtf8("ULTRASTORE_OPERATOR_TOKEN");   // path-string-ok: GetEnvUtf8 is the UTF-8 read
    const std::string name = UltraCanvas::GetEnvUtf8("ULTRASTORE_OPERATOR_NAME");     // path-string-ok: GetEnvUtf8 is the UTF-8 read
    if (!url.empty()) s.url = TrimSlash(url);
    if (!token.empty()) s.token = token;
    if (!name.empty()) s.name = name;
    return s;
}

// ===========================================================================
// The records
// ===========================================================================
Merchant MerchantOf(const JSONValue& v) {
    Merchant m;
    m.id = v.Get("id").GetString();
    m.name = v.Get("name").GetString();
    m.contact = v.Get("contact").GetString();
    m.key = v.Get("key").GetString();
    m.createdAt = v.Get("created_at").GetString();
    m.standing = v.Get("standing").GetString();
    m.strikes = static_cast<int>(v.Get("strikes").GetInteger(0));
    m.termsInForce = static_cast<int>(v.Get("terms").Get("in_force").GetInteger(0));
    m.termsAccepted = static_cast<int>(v.Get("terms").Get("accepted").GetInteger(0));
    return m;
}

Suspension SuspensionOf(const JSONValue& v) {
    Suspension s;
    s.id = v.Get("id").GetInteger(0);
    s.scope = v.Get("scope").GetString();
    s.target = v.Get("target").GetString();
    s.clause = v.Get("clause").GetString();
    s.reason = v.Get("reason").GetString();
    s.start = v.Get("start").GetString();
    s.end = v.Get("end").GetString();
    s.liftedAt = v.Get("lifted_at").GetString();
    s.createdBy = v.Get("created_by").GetString();
    return s;
}

Submission SubmissionOf(const JSONValue& v) {
    Submission s;
    s.id = v.Get("id").GetString();
    s.merchant = v.Get("merchant").GetString();
    s.app = v.Get("app").GetString();
    s.version = v.Get("version").GetString();
    s.state = v.Get("state").GetString();
    s.answers = v.Get("answers").GetString();
    s.createdAt = v.Get("created_at").GetString();
    s.decidedAt = v.Get("decided_at").GetString();
    s.releasedSequence = static_cast<int>(v.Get("released_sequence").GetInteger(0));
    for (const JSONValue::Member& m : v.Get("checks").GetMembers()) s.checks.emplace_back(m.first, m.second.GetString());
    for (const JSONValue& d : v.Get("decisions").GetElements()) s.decisions.push_back(DecisionOf(d));
    s.component = v.Get("component");
    s.packages = v.Get("packages");
    return s;
}

bool Submission::ChecksFailed() const {
    for (const auto& [name, outcome] : checks) if (outcome == "failed") return true;
    return false;
}

std::string Submission::Name() const {
    const std::string name = component.Get("name").GetString();
    return name.empty() ? app : name;
}

bool MerchantFile::Suspended() const {
    for (const Suspension& s : suspensions) if (!s.Lifted()) return true;
    return false;
}

Report ReportOf(const JSONValue& v) {
    Report r;
    r.id = v.Get("id").GetString();
    r.source = v.Get("source").GetString();
    r.targetKind = v.Get("target_kind").GetString();
    r.target = v.Get("target").GetString();
    r.kind = v.Get("kind").GetString();
    r.text = v.Get("text").GetString();
    r.state = v.Get("state").GetString();
    r.createdAt = v.Get("created_at").GetString();
    r.resolvedAt = v.Get("resolved_at").GetString();
    r.outcome = v.Get("outcome").GetString();
    r.reporter = v.Get("reporter").GetString();
    r.reporterMerchant = v.Get("reporter_merchant").GetString();
    r.assignee = v.Get("assignee").GetString();
    r.findings = v.Get("findings").GetString();
    r.attachments = Strings(v.Get("attachments"));
    return r;
}

Notice NoticeOf(const JSONValue& v) {
    Notice n;
    n.id = v.Get("id").GetString();
    n.kind = v.Get("kind").GetString();
    n.subject = v.Get("subject").GetString();
    n.text = v.Get("text").GetString();
    n.clause = v.Get("clause").GetString();
    n.createdAt = v.Get("created_at").GetString();
    n.createdBy = v.Get("created_by").GetString();
    n.expiresAt = v.Get("expires_at").GetString();
    n.deadline = v.Get("deadline").GetString();
    n.recipients = Strings(v.Get("recipients"));
    return n;
}

AuditRow AuditRowOf(const JSONValue& v) {
    AuditRow r;
    r.id = v.Get("id").GetInteger(0);
    r.at = v.Get("at").GetString();
    r.actor = v.Get("actor").GetString();
    r.action = v.Get("action").GetString();
    r.targetKind = v.Get("target_kind").GetString();
    r.target = v.Get("target").GetString();
    r.clause = v.Get("clause").GetString();
    r.actedOn = v.Get("acted_on").GetString();
    r.hash = v.Get("hash").GetString();
    r.before = v.Get("before");
    r.after = v.Get("after");
    return r;
}

std::string AuditRow::Line() const {
    std::string line = at + "  " + actor + " " + action + " " + targetKind + " " + target;
    if (!clause.empty()) line += " (clause " + clause + ")";
    const std::string message = after.Get("message").GetString();
    if (!message.empty()) line += ": " + message;
    const std::string reason = after.Get("reason").GetString();
    if (!reason.empty()) line += ": " + reason;
    return line;
}

Policy PolicyOf(const JSONValue& v) {
    Policy p;
    p.strikeExpiryDays = static_cast<int>(v.Get("strike_expiry_days").GetInteger(p.strikeExpiryDays));
    p.strikesBeforeSuspension = static_cast<int>(v.Get("strikes_before_suspension").GetInteger(p.strikesBeforeSuspension));
    p.suspensionDays = static_cast<int>(v.Get("suspension_days").GetInteger(p.suspensionDays));
    p.answerDeadlineDays = static_cast<int>(v.Get("answer_deadline_days").GetInteger(p.answerDeadlineDays));
    p.clausesThatSuspend = Strings(v.Get("clauses_that_suspend"));
    return p;
}

JSONValue PolicyJson(const Policy& p) {
    JSONValue v = JSONValue::MakeObject();
    v.Set("strike_expiry_days", JSONValue(p.strikeExpiryDays));
    v.Set("strikes_before_suspension", JSONValue(p.strikesBeforeSuspension));
    v.Set("suspension_days", JSONValue(p.suspensionDays));
    v.Set("answer_deadline_days", JSONValue(p.answerDeadlineDays));
    v.Set("clauses_that_suspend", StringsJson(p.clausesThatSuspend));
    return v;
}

std::string Age(const std::string& at, int64_t now) {
    const int64_t then = ParseIso(at);
    if (then == 0) return {};
    if (now == 0) now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    int64_t seconds = now - then;
    if (seconds < 0) seconds = 0;
    if (seconds >= 86400) return std::to_string(seconds / 86400) + " d";
    if (seconds >= 3600) return std::to_string(seconds / 3600) + " h";
    return std::to_string(seconds / 60) + " min";
}

// ===========================================================================
// The client
// ===========================================================================
OperatorClient::OperatorClient(OperatorSettings settings, MarketCall call) : settings_(std::move(settings)), call_(std::move(call)) {
    settings_.url = TrimSlash(settings_.url);
}

Result<JSONValue> OperatorClient::Ask(const std::string& method, const std::string& path, const std::string& jsonBody) const {
    using R = Result<JSONValue>;
    if (settings_.url.empty()) return R::Failure(ErrorCode::InvalidArgument, "No marketplace service URL: `settings set url <https://...>`");
    if (settings_.token.empty()) return R::Failure(ErrorCode::InvalidArgument, "No operator token: `settings set token <token>` (the service's ULTRASTORE_MARKET_OPERATOR_TOKEN)");
    const MarketReply reply = call_(method, settings_.url + path, "Bearer " + settings_.token, jsonBody);
    if (!reply.error.empty() || reply.status == 0) return R::Failure(ErrorCode::NetworkError, "The marketplace service could not be reached", reply.error);
    UltraCanvas::JSONParseResult parse;
    const JSONValue v = UltraCanvas::JSON::Parse(reply.body, &parse);
    if (reply.status < 200 || reply.status >= 300) {
        const std::string description = parse.success && v.IsObject() ? v.Get("error_description").GetString() : std::string();
        return R::Failure(reply.status == 404 ? ErrorCode::NotFound : reply.status == 401 ? ErrorCode::SignatureInvalid : ErrorCode::NetworkError,
                          description.empty() ? "The marketplace service answered HTTP " + std::to_string(reply.status) : description,
                          "HTTP " + std::to_string(reply.status));
    }
    if (!parse.success || !v.IsObject()) return R::Failure(ErrorCode::ParseError, "The marketplace service's answer is not JSON", parse.errorMessage);
    return R::Succeed(v);
}

Result<std::string> OperatorClient::Health() const {
    using R = Result<std::string>;
    if (settings_.url.empty()) return R::Failure(ErrorCode::InvalidArgument, "No marketplace service URL: `settings set url <https://...>`");
    const MarketReply reply = call_("GET", settings_.url + "/healthz", {}, {});
    if (!reply.error.empty() || reply.status == 0) return R::Failure(ErrorCode::NetworkError, "The marketplace service could not be reached", reply.error);
    if (reply.status < 200 || reply.status >= 300) return R::Failure(ErrorCode::NetworkError, "The marketplace service answered HTTP " + std::to_string(reply.status), reply.body);
    return R::Succeed(reply.body);
}

// ----- merchants -----
Result<std::vector<Merchant>> OperatorClient::Merchants() const {
    using R = Result<std::vector<Merchant>>;
    const auto v = Ask("GET", "/v1/merchants");
    if (!v) return R::Failure(v.error.code, v.error.message, v.error.detail);
    std::vector<Merchant> out;
    for (const JSONValue& m : v.Value().Get("merchants").GetElements()) out.push_back(MerchantOf(m));
    return R::Succeed(out);
}

Result<MerchantFile> OperatorClient::GetMerchant(const std::string& id) const {
    using R = Result<MerchantFile>;
    const auto v = Ask("GET", "/v1/merchants/" + Encode(id));
    if (!v) return R::Failure(v.error.code, v.error.message, v.error.detail);
    MerchantFile f;
    f.merchant = MerchantOf(v.Value());
    f.notes = v.Value().Get("notes").GetString();
    for (const JSONValue& s : v.Value().Get("suspensions").GetElements()) f.suspensions.push_back(SuspensionOf(s));
    for (const JSONValue& a : v.Value().Get("acceptances").GetElements()) f.acceptances.push_back(AcceptanceOf(a));
    for (const JSONValue& s : v.Value().Get("submissions").GetElements()) f.submissions.push_back(SubmissionOf(s));
    return R::Succeed(f);
}

Result<Merchant> OperatorClient::RegisterMerchant(const std::string& id, const std::string& name, const std::string& contact, const std::string& keyHex) const {
    using R = Result<Merchant>;
    JSONValue body = JSONValue::MakeObject();
    if (!id.empty()) body.Set("id", JSONValue(id));
    body.Set("name", JSONValue(name));
    body.Set("contact", JSONValue(contact));
    body.Set("key", JSONValue(keyHex));
    const auto v = Ask("POST", "/v1/merchants", UltraCanvas::JSON::Serialize(body));
    if (!v) return R::Failure(v.error.code, v.error.message, v.error.detail);
    return R::Succeed(MerchantOf(v.Value()));
}

Result<Merchant> OperatorClient::UpdateMerchant(const std::string& id, const std::string& name, const std::string& contact, const std::string& keyHex) const {
    using R = Result<Merchant>;
    JSONValue body = JSONValue::MakeObject();
    if (!name.empty()) body.Set("name", JSONValue(name));
    if (!contact.empty()) body.Set("contact", JSONValue(contact));
    if (!keyHex.empty()) body.Set("key", JSONValue(keyHex));
    const auto v = Ask("PUT", "/v1/merchants/" + Encode(id), UltraCanvas::JSON::Serialize(body));
    if (!v) return R::Failure(v.error.code, v.error.message, v.error.detail);
    return R::Succeed(MerchantOf(v.Value()));
}

Result<Merchant> OperatorClient::SuspendMerchant(const std::string& id, const std::string& clause, const std::string& reason, int days) const {
    using R = Result<Merchant>;
    JSONValue body = JSONValue::MakeObject();
    body.Set("clause", JSONValue(clause));
    body.Set("reason", JSONValue(reason));
    body.Set("days", JSONValue(days));
    const auto v = Ask("POST", "/v1/merchants/" + Encode(id) + "/suspend", UltraCanvas::JSON::Serialize(body));
    if (!v) return R::Failure(v.error.code, v.error.message, v.error.detail);
    return R::Succeed(MerchantOf(v.Value().Get("merchant")));
}

Result<Merchant> OperatorClient::ReinstateMerchant(const std::string& id, const std::string& reason) const {
    using R = Result<Merchant>;
    JSONValue body = JSONValue::MakeObject();
    body.Set("reason", JSONValue(reason));
    const auto v = Ask("POST", "/v1/merchants/" + Encode(id) + "/reinstate", UltraCanvas::JSON::Serialize(body));
    if (!v) return R::Failure(v.error.code, v.error.message, v.error.detail);
    return R::Succeed(MerchantOf(v.Value().Get("merchant")));
}

Result<Suspension> OperatorClient::SuspendApp(const std::string& appId, const std::string& clause, const std::string& reason, int days) const {
    using R = Result<Suspension>;
    JSONValue body = JSONValue::MakeObject();
    body.Set("clause", JSONValue(clause));
    body.Set("reason", JSONValue(reason));
    body.Set("days", JSONValue(days));
    const auto v = Ask("POST", "/v1/apps/" + Encode(appId) + "/suspend", UltraCanvas::JSON::Serialize(body));
    if (!v) return R::Failure(v.error.code, v.error.message, v.error.detail);
    return R::Succeed(SuspensionOf(v.Value().Get("suspension")));
}

Result<int> OperatorClient::ReinstateApp(const std::string& appId, const std::string& reason) const {
    using R = Result<int>;
    JSONValue body = JSONValue::MakeObject();
    body.Set("reason", JSONValue(reason));
    const auto v = Ask("POST", "/v1/apps/" + Encode(appId) + "/reinstate", UltraCanvas::JSON::Serialize(body));
    if (!v) return R::Failure(v.error.code, v.error.message, v.error.detail);
    return R::Succeed(static_cast<int>(v.Value().Get("lifted").GetInteger(0)));
}

// ----- the review -----
Result<std::vector<Submission>> OperatorClient::Submissions(const std::string& state, const std::string& merchant, const std::string& app) const {
    using R = Result<std::vector<Submission>>;
    std::string query;
    auto add = [&](const char* key, const std::string& value) { if (!value.empty()) query += (query.empty() ? "?" : "&") + std::string(key) + "=" + Encode(value); };
    add("state", state);
    add("merchant", merchant);
    add("app", app);
    const auto v = Ask("GET", "/v1/submissions" + query);
    if (!v) return R::Failure(v.error.code, v.error.message, v.error.detail);
    std::vector<Submission> out;
    for (const JSONValue& s : v.Value().Get("submissions").GetElements()) out.push_back(SubmissionOf(s));
    return R::Succeed(out);
}

Result<Submission> OperatorClient::GetSubmission(const std::string& id) const {
    using R = Result<Submission>;
    const auto v = Ask("GET", "/v1/submissions/" + Encode(id));
    if (!v) return R::Failure(v.error.code, v.error.message, v.error.detail);
    return R::Succeed(SubmissionOf(v.Value()));
}

Result<Submission> OperatorClient::Decide(const std::string& id, const std::string& action, const std::string& message, int sequence) const {
    using R = Result<Submission>;
    JSONValue body = JSONValue::MakeObject();
    body.Set("action", JSONValue(action));
    body.Set("message", JSONValue(message));
    body.Set("sequence", JSONValue(sequence));
    const auto v = Ask("POST", "/v1/submissions/" + Encode(id) + "/decide", UltraCanvas::JSON::Serialize(body));
    if (!v) return R::Failure(v.error.code, v.error.message, v.error.detail);
    return R::Succeed(SubmissionOf(v.Value()));
}

Result<Released> OperatorClient::GetReleased() const {
    using R = Result<Released>;
    const auto v = Ask("GET", "/v1/released");
    if (!v) return R::Failure(v.error.code, v.error.message, v.error.detail);
    Released out;
    out.sequence = static_cast<int>(v.Value().Get("sequence").GetInteger(0));
    out.published = static_cast<int>(v.Value().Get("published").GetInteger(0));
    for (const JSONValue& e : v.Value().Get("released").GetElements()) {
        ReleasedEntry entry;
        entry.app = e.Get("app").GetString();
        entry.merchant = e.Get("merchant").GetString();
        entry.submission = e.Get("submission").GetString();
        entry.sequence = static_cast<int>(e.Get("sequence").GetInteger(0));
        entry.component = e.Get("component");
        entry.packages = e.Get("packages");
        out.entries.push_back(entry);
    }
    return R::Succeed(out);
}

Result<PublishReport> OperatorClient::Publish() const {
    using R = Result<PublishReport>;
    const auto v = Ask("POST", "/v1/publish", "{}");
    if (!v) return R::Failure(v.error.code, v.error.message, v.error.detail);
    PublishReport p;
    p.sequence = static_cast<int>(v.Value().Get("sequence").GetInteger(0));
    p.apps = static_cast<int>(v.Value().Get("apps").GetInteger(0));
    p.components = static_cast<int>(v.Value().Get("components").GetInteger(0));
    p.publishers = static_cast<int>(v.Value().Get("publishers").GetInteger(0));
    p.leftOut = Strings(v.Value().Get("left_out"));
    p.files = Strings(v.Value().Get("files"));
    p.publicKey = v.Value().Get("public_key").GetString();
    return R::Succeed(p);
}

Result<PublishKey> OperatorClient::GetPublishKey() const {
    using R = Result<PublishKey>;
    const auto v = Ask("GET", "/v1/publish/key");
    if (!v) return R::Failure(v.error.code, v.error.message, v.error.detail);
    PublishKey k;
    k.publicKey = v.Value().Get("public_key").GetString();
    k.trustedKeys = Strings(v.Value().Get("trusted_keys"));
    return R::Succeed(k);
}

// ----- reports -----
Result<std::vector<Report>> OperatorClient::Reports(const std::string& state) const {
    using R = Result<std::vector<Report>>;
    const auto v = Ask("GET", "/v1/reports" + (state.empty() ? std::string() : "?state=" + Encode(state)));
    if (!v) return R::Failure(v.error.code, v.error.message, v.error.detail);
    std::vector<Report> out;
    for (const JSONValue& r : v.Value().Get("reports").GetElements()) out.push_back(ReportOf(r));
    return R::Succeed(out);
}

Result<Report> OperatorClient::GetReport(const std::string& id) const {
    using R = Result<Report>;
    const auto v = Ask("GET", "/v1/reports/" + Encode(id));
    if (!v) return R::Failure(v.error.code, v.error.message, v.error.detail);
    return R::Succeed(ReportOf(v.Value()));
}

Result<Report> OperatorClient::OpenReport(const std::string& targetKind, const std::string& target, const std::string& kind, const std::string& text,
                                          const std::string& reporter) const {
    using R = Result<Report>;
    JSONValue body = JSONValue::MakeObject();
    body.Set("target_kind", JSONValue(targetKind));
    body.Set("target", JSONValue(target));
    body.Set("kind", JSONValue(kind));
    body.Set("text", JSONValue(text));
    body.Set("reporter", JSONValue(reporter));
    const auto v = Ask("POST", "/v1/reports", UltraCanvas::JSON::Serialize(body));
    if (!v) return R::Failure(v.error.code, v.error.message, v.error.detail);
    return GetReport(v.Value().Get("id").GetString());
}

Result<Report> OperatorClient::AssignReport(const std::string& id, const std::string& assignee) const {
    using R = Result<Report>;
    JSONValue body = JSONValue::MakeObject();
    body.Set("assignee", JSONValue(assignee));
    const auto v = Ask("POST", "/v1/reports/" + Encode(id) + "/assign", UltraCanvas::JSON::Serialize(body));
    if (!v) return R::Failure(v.error.code, v.error.message, v.error.detail);
    return R::Succeed(ReportOf(v.Value()));
}

Result<Report> OperatorClient::CloseReport(const std::string& id, bool resolve, const std::string& findings, const std::string& outcome) const {
    using R = Result<Report>;
    JSONValue body = JSONValue::MakeObject();
    body.Set("findings", JSONValue(findings));
    body.Set("outcome", JSONValue(outcome));
    const auto v = Ask("POST", "/v1/reports/" + Encode(id) + (resolve ? "/resolve" : "/dismiss"), UltraCanvas::JSON::Serialize(body));
    if (!v) return R::Failure(v.error.code, v.error.message, v.error.detail);
    return R::Succeed(ReportOf(v.Value()));
}

// ----- notices -----
Result<std::vector<Notice>> OperatorClient::Notices() const {
    using R = Result<std::vector<Notice>>;
    const auto v = Ask("GET", "/v1/notices");
    if (!v) return R::Failure(v.error.code, v.error.message, v.error.detail);
    std::vector<Notice> out;
    for (const JSONValue& n : v.Value().Get("notices").GetElements()) out.push_back(NoticeOf(n));
    return R::Succeed(out);
}

Result<NoticeReceipt> OperatorClient::SendNotice(const std::string& kind, const std::string& subject, const std::string& text, const std::string& clause,
                                                 const std::vector<std::string>& recipients) const {
    using R = Result<NoticeReceipt>;
    if (recipients.empty()) return R::Failure(ErrorCode::InvalidArgument, "A notice names its recipients: merchant ids, or * for everyone");
    JSONValue body = JSONValue::MakeObject();
    body.Set("kind", JSONValue(kind));
    body.Set("subject", JSONValue(subject));
    body.Set("text", JSONValue(text));
    body.Set("clause", JSONValue(clause));
    body.Set("recipients", StringsJson(recipients));
    const auto v = Ask("POST", "/v1/notices", UltraCanvas::JSON::Serialize(body));
    if (!v) return R::Failure(v.error.code, v.error.message, v.error.detail);
    NoticeReceipt r;
    r.notice = NoticeOf(v.Value().Get("notice"));
    for (const JSONValue& s : v.Value().Get("suspended").GetElements()) r.suspended.push_back(SuspensionOf(s));
    return R::Succeed(r);
}

// ----- terms and policy -----
Result<TermsList> OperatorClient::Terms() const {
    using R = Result<TermsList>;
    const auto v = Ask("GET", "/v1/terms");
    if (!v) return R::Failure(v.error.code, v.error.message, v.error.detail);
    TermsList t;
    t.inForce = static_cast<int>(v.Value().Get("in_force").GetInteger(0));
    for (const JSONValue& e : v.Value().Get("versions").GetElements()) t.versions.push_back(TermsOf(e));
    return R::Succeed(t);
}

Result<TermsVersion> OperatorClient::AddTerms(const std::string& text, const std::string& changes, const std::string& inForceFrom, int version) const {
    using R = Result<TermsVersion>;
    JSONValue body = JSONValue::MakeObject();
    body.Set("text", JSONValue(text));
    body.Set("changes", JSONValue(changes));
    body.Set("in_force_from", JSONValue(inForceFrom));
    body.Set("version", JSONValue(version));
    const auto v = Ask("POST", "/v1/terms", UltraCanvas::JSON::Serialize(body));
    if (!v) return R::Failure(v.error.code, v.error.message, v.error.detail);
    return R::Succeed(TermsOf(v.Value()));
}

Result<Policy> OperatorClient::GetPolicy() const {
    using R = Result<Policy>;
    const auto v = Ask("GET", "/v1/policy");
    if (!v) return R::Failure(v.error.code, v.error.message, v.error.detail);
    return R::Succeed(PolicyOf(v.Value()));
}

Result<Policy> OperatorClient::SetPolicy(const Policy& policy) const {
    using R = Result<Policy>;
    const auto v = Ask("PUT", "/v1/policy", UltraCanvas::JSON::Serialize(PolicyJson(policy)));
    if (!v) return R::Failure(v.error.code, v.error.message, v.error.detail);
    return R::Succeed(PolicyOf(v.Value()));
}

// ----- the audit log -----
Result<AuditPage> OperatorClient::Audit(int64_t since, int limit) const {
    using R = Result<AuditPage>;
    const auto v = Ask("GET", "/v1/audit?since=" + std::to_string(since) + "&limit=" + std::to_string(limit));
    if (!v) return R::Failure(v.error.code, v.error.message, v.error.detail);
    AuditPage page;
    page.next = v.Value().Get("next").GetInteger(since);
    for (const JSONValue& r : v.Value().Get("audit").GetElements()) page.rows.push_back(AuditRowOf(r));
    return R::Succeed(page);
}

Result<AuditCheck> OperatorClient::VerifyAudit() const {
    using R = Result<AuditCheck>;
    const auto v = Ask("GET", "/v1/audit/verify");
    if (!v) return R::Failure(v.error.code, v.error.message, v.error.detail);
    AuditCheck c;
    c.ok = v.Value().Get("ok").GetBoolean(false);
    c.rows = v.Value().Get("rows").GetInteger(0);
    c.brokenAt = v.Value().Get("broken_at").GetInteger(0);
    c.lastHash = v.Value().Get("last_hash").GetString();
    return R::Succeed(c);
}

} // namespace UltraStoreOperator
