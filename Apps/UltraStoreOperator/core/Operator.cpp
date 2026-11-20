// Apps/UltraStoreOperator/core/Operator.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "Operator.h"

#include "UltraCanvasDataFolder.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraNet/UltraNetCore.h"
#include "UltraNet/UltraNetHttp.h"

#include <algorithm>
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
    s.role = v.Get("role").GetString();
    s.accountUrl = TrimSlash(v.Get("account_url").GetString());
    s.accountToken = v.Get("account_token").GetString();
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
    v.Set("role", JSONValue(role));
    v.Set("account_url", JSONValue(TrimSlash(accountUrl)));
    v.Set("account_token", JSONValue(accountToken));
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
    const std::string accountUrl = UltraCanvas::GetEnvUtf8("ULTRASTORE_OPERATOR_ACCOUNT_URL");       // path-string-ok: GetEnvUtf8 is the UTF-8 read
    const std::string accountToken = UltraCanvas::GetEnvUtf8("ULTRASTORE_OPERATOR_ACCOUNT_TOKEN");   // path-string-ok: GetEnvUtf8 is the UTF-8 read
    if (!accountUrl.empty()) s.accountUrl = TrimSlash(accountUrl);
    if (!accountToken.empty()) s.accountToken = accountToken;
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
    m.website = v.Get("website").GetString();
    m.description = v.Get("description").GetString();
    m.key = v.Get("key").GetString();
    m.createdAt = v.Get("created_at").GetString();
    m.standing = v.Get("standing").GetString();
    m.strikes = static_cast<int>(v.Get("strikes").GetInteger(0));
    m.termsInForce = static_cast<int>(v.Get("terms").Get("in_force").GetInteger(0));
    m.termsAccepted = static_cast<int>(v.Get("terms").Get("accepted").GetInteger(0));
    return m;
}

MerchantApplication ApplicationOf(const JSONValue& v) {
    MerchantApplication a;
    a.id = v.Get("id").GetString();
    a.name = v.Get("name").GetString();
    a.contact = v.Get("contact").GetString();
    a.email = v.Get("email").GetString();
    a.website = v.Get("website").GetString();
    a.description = v.Get("description").GetString();
    a.key = v.Get("key").GetString();
    a.state = v.Get("state").GetString();
    a.message = v.Get("message").GetString();
    a.createdAt = v.Get("created_at").GetString();
    a.decidedAt = v.Get("decided_at").GetString();
    a.decidedBy = v.Get("decided_by").GetString();
    a.merchant = v.Get("merchant").GetString();
    return a;
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

User UserOf(const JSONValue& v) {
    User u;
    u.id = v.Get("id").GetString();
    u.email = v.Get("email").GetString();
    u.displayName = v.Get("display_name").GetString();
    u.handle = v.Get("handle").GetString();
    u.language = v.Get("language").GetString();
    u.country = v.Get("country").GetString();
    u.state = v.Get("state").GetString();
    u.createdAt = v.Get("created_at").GetString();
    u.deletionRequestedAt = v.Get("deletion_requested_at").GetString();
    u.deleteAfter = v.Get("delete_after").GetString();
    u.suspendedReason = v.Get("suspended_reason").GetString();
    u.suspendedUntil = v.Get("suspended_until").GetString();
    u.request = v.Get("request").GetString();
    u.emailVerified = v.Get("email_verified").GetBoolean(false);
    u.twoFactor = v.Get("two_factor").GetBoolean(false);
    u.devices = static_cast<int>(v.Get("devices").IsNumber() ? v.Get("devices").GetInteger(0) : v.Get("devices").GetSize());
    u.licences = static_cast<int>(v.Get("licences").IsNumber() ? v.Get("licences").GetInteger(0) : v.Get("licences").GetSize());
    return u;
}

UserStats UserStatsOf(const JSONValue& v) {
    UserStats s;
    s.accounts = static_cast<int>(v.Get("accounts").GetInteger(0));
    s.newIn30Days = static_cast<int>(v.Get("new_in_30_days").GetInteger(0));
    s.twoFactorPercent = static_cast<int>(v.Get("two_factor_percent").GetInteger(0));
    s.archiveRequests = static_cast<int>(v.Get("archive_requests").GetInteger(0));
    s.archiveRequestsDue = static_cast<int>(v.Get("archive_requests_due").GetInteger(0));
    s.deletionRequests = static_cast<int>(v.Get("deletion_requests").GetInteger(0));
    s.suspended = static_cast<int>(v.Get("suspended").GetInteger(0));
    return s;
}

UserFile UserFileOf(const JSONValue& v) {
    UserFile f;
    f.user = UserOf(v);
    for (const JSONValue& d : v.Get("devices").GetElements()) {
        UserDevice x;
        x.id = d.Get("id").GetString(); x.name = d.Get("device_name").GetString(); x.os = d.Get("os").GetString(); x.client = d.Get("client").GetString();
        x.lastSeen = d.Get("last_seen").GetString(); x.lastIp = d.Get("last_ip").GetString();
        f.devices.push_back(x);
    }
    for (const JSONValue& l : v.Get("licences").GetElements()) {
        UserLicence x;
        x.id = l.Get("id").GetString(); x.app = l.Get("app").GetString(); x.order = l.Get("order").GetString(); x.issued = l.Get("issued").GetString();
        x.validUntil = l.Get("valid_until").GetString(); x.seats = static_cast<int>(l.Get("seats").GetInteger(0)); x.seatsUsed = static_cast<int>(l.Get("seats_used").GetInteger(0));
        f.licences.push_back(x);
    }
    for (const JSONValue& n : v.Get("notices").GetElements()) {
        UserNotice x;
        x.id = n.Get("id").GetString(); x.kind = n.Get("kind").GetString(); x.subject = n.Get("subject").GetString(); x.text = n.Get("text").GetString();
        x.createdAt = n.Get("created_at").GetString(); x.createdBy = n.Get("created_by").GetString(); x.readAt = n.Get("read_at").GetString();
        f.notices.push_back(x);
    }
    for (const JSONValue& a : v.Get("archives").GetElements()) {
        UserArchive x;
        x.id = a.Get("id").GetString(); x.state = a.Get("state").GetString(); x.due = a.Get("due").GetString();
        x.requestedAt = a.Get("requested_at").GetString(); x.releasedAt = a.Get("released_at").GetString();
        f.archives.push_back(x);
    }
    for (const JSONValue& h : v.Get("history").GetElements()) f.history.push_back(AuditRowOf(h));
    for (const JSONValue& si : v.Get("sign_ins").GetElements()) {
        UserSignIn x;
        x.at = si.Get("at").GetString(); x.ip = si.Get("ip").GetString(); x.device = si.Get("device").GetString(); x.reason = si.Get("reason").GetString();
        x.ok = si.Get("ok").GetBoolean(false);
        f.signIns.push_back(x);
    }
    return f;
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
DownloadCall UltraNetDownloadCall() {
    return [](const std::string& url, const std::string& authorization, const std::string& destinationUtf8) {
        MarketReply out;
        if (!UltraNet_IsInitialized()) UltraNet_Initialize();
        UltraNetHttpRequest request;
        request.url = url;
        request.method = UltraNetHttpMethod::Get;
        request.options.httpsOnlyRedirects = true;
        request.options.outputFilePath = destinationUtf8;
        request.options.timeoutMs = 0;
        if (!authorization.empty()) request.headers.Set("Authorization", authorization);
        UltraNetResponse response;
        const UltraNetResult r = UltraNet_HttpRequest(request, response);
        out.status = response.statusCode;
        out.body = response.GetBodyAsString();   // the error JSON, when the service refused
        if (!r && r.code != UltraNetResultCode::HttpError) out.error = r.message.empty() ? "no answer" : r.message;
        if (!response.IsComplete()) out.error = response.transferError;
        return out;
    };
}

std::string PackageFileName(const JSONValue& package) {
    std::string sha = package.Get("sha256").GetString();
    for (char& c : sha) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::string format = package.Get("format").GetString();
    if (format.empty()) {
        std::string name = package.Get("file").GetString(package.Get("url").GetString());
        name = name.substr(name.rfind('/') == std::string::npos ? 0 : name.rfind('/') + 1);
        for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for (const char* known : {"tar.zst", "tar.gz", "tar.xz", "tgz", "zip", "7z", "appimage"}) {
            const std::string suffix = std::string(".") + known;
            if (name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) { format = known; break; }
        }
    }
    return sha + "." + (format.empty() ? std::string("bin") : format);
}

OperatorClient::OperatorClient(OperatorSettings settings, MarketCall call, DownloadCall download)
    : settings_(std::move(settings)), call_(std::move(call)), download_(std::move(download)) {
    settings_.url = TrimSlash(settings_.url);
    settings_.accountUrl = TrimSlash(settings_.accountUrl);
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

// ----- the operator accounts -----
namespace {

OperatorAccount OperatorAccountOf(const JSONValue& v) {
    OperatorAccount a;
    a.id = v.Get("id").GetString();
    a.email = v.Get("email").GetString();
    a.name = v.Get("name").GetString();
    a.role = v.Get("role").GetString();
    a.createdAt = v.Get("created_at").GetString();
    a.createdBy = v.Get("created_by").GetString();
    a.disabledAt = v.Get("disabled_at").GetString();
    a.lastSignin = v.Get("last_signin").GetString();
    a.active = v.Get("active").GetBoolean(a.disabledAt.empty());
    a.twoFactor = v.Get("two_factor").GetBoolean(false);
    a.recoveryCodesLeft = static_cast<int>(v.Get("recovery_codes_left").GetInteger(0));
    return a;
}

} // namespace

bool WhoAmI::May(const std::string& permission) const {
    for (const std::string& p : permissions) if (p == permission) return true;
    return false;
}

// A six-digit code is the authenticator's; anything else a recovery code.
static void SetSecondFactor(JSONValue& body, const std::string& code) {
    if (code.empty()) return;
    std::string plain;
    for (char c : code) if (c != ' ') plain.push_back(c);
    const bool digits = plain.size() == 6 && std::all_of(plain.begin(), plain.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; });
    body.Set(digits ? "totp" : "recovery_code", JSONValue(plain));
}

// A call without a bearer (the sign-in, a forgotten password): the answer
// parsed, a refusal's error code as the failure's detail.
Result<JSONValue> OperatorClient::Open(const std::string& path, const std::string& jsonBody) const {
    using R = Result<JSONValue>;
    if (settings_.url.empty()) return R::Failure(ErrorCode::InvalidArgument, "No marketplace service URL: `settings set url <https://...>`");
    const MarketReply reply = call_("POST", settings_.url + path, {}, jsonBody);
    if (!reply.error.empty() || reply.status == 0) return R::Failure(ErrorCode::NetworkError, "The marketplace service could not be reached", reply.error);
    UltraCanvas::JSONParseResult parse;
    const JSONValue v = UltraCanvas::JSON::Parse(reply.body, &parse);
    if (reply.status < 200 || reply.status >= 300) {
        const std::string description = parse.success && v.IsObject() ? v.Get("error_description").GetString() : std::string();
        const std::string code = parse.success && v.IsObject() ? v.Get("error").GetString() : std::string();
        return R::Failure(reply.status == 401 ? ErrorCode::SignatureInvalid : ErrorCode::NetworkError,
                          description.empty() ? "The marketplace service answered HTTP " + std::to_string(reply.status) : description,
                          code.empty() ? "HTTP " + std::to_string(reply.status) : code);
    }
    if (!parse.success || !v.IsObject()) return R::Failure(ErrorCode::ParseError, "The marketplace service's answer is not JSON", parse.errorMessage);
    return R::Succeed(v);
}

Result<OperatorSignIn> OperatorClient::SignIn(const std::string& email, const std::string& password, const std::string& code) const {
    using R = Result<OperatorSignIn>;
    if (email.empty() || password.empty()) return R::Failure(ErrorCode::InvalidArgument, "The sign-in needs the email and the password");
    JSONValue body = JSONValue::MakeObject();
    body.Set("email", JSONValue(email));
    body.Set("password", JSONValue(password));
    SetSecondFactor(body, code);
    const auto answer = Open("/v1/operators/signin", UltraCanvas::JSON::Serialize(body));
    if (!answer) return R::Failure(answer.error.code, answer.error.message, answer.error.detail);
    const JSONValue& v = answer.Value();
    OperatorSignIn in;
    in.token = v.Get("token").GetString();
    in.account = OperatorAccountOf(v.Get("operator"));
    in.hours = static_cast<int>(v.Get("hours").GetInteger(0));
    if (in.token.empty()) return R::Failure(ErrorCode::ParseError, "The sign-in answered no session");
    return R::Succeed(in);
}

Result<TwoFactorSetup> OperatorClient::TwoFactorSetup() const {
    using R = Result<struct TwoFactorSetup>;
    const auto v = Ask("POST", "/v1/operators/me/two-factor/setup", "{}");
    if (!v) return R::Failure(v.error.code, v.error.message, v.error.detail);
    struct TwoFactorSetup setup;
    setup.secret = v.Value().Get("secret").GetString();
    setup.otpauthUrl = v.Value().Get("otpauth_url").GetString();
    return R::Succeed(setup);
}

static Result<std::vector<std::string>> RecoveryCodesOf(const Result<JSONValue>& v) {
    using R = Result<std::vector<std::string>>;
    if (!v) return R::Failure(v.error.code, v.error.message, v.error.detail);
    std::vector<std::string> codes;
    for (const JSONValue& c : v.Value().Get("recovery_codes").GetElements()) codes.push_back(c.GetString());
    return R::Succeed(codes);
}

static std::string CodeBody(const std::string& code) {
    JSONValue body = JSONValue::MakeObject();
    body.Set("code", JSONValue(code));
    return UltraCanvas::JSON::Serialize(body);
}

Result<std::vector<std::string>> OperatorClient::TwoFactorEnable(const std::string& code) const {
    return RecoveryCodesOf(Ask("POST", "/v1/operators/me/two-factor/enable", CodeBody(code)));
}

Result<void> OperatorClient::TwoFactorDisable(const std::string& code) const {
    const auto v = Ask("POST", "/v1/operators/me/two-factor/disable", CodeBody(code));
    if (!v) return Result<void>::Failure(v.error.code, v.error.message, v.error.detail);
    return Result<void>::Succeed();
}

Result<std::vector<std::string>> OperatorClient::NewRecoveryCodes(const std::string& code) const {
    return RecoveryCodesOf(Ask("POST", "/v1/operators/me/two-factor/recovery-codes", CodeBody(code)));
}

Result<int> OperatorClient::ForgotPassword(const std::string& email) const {
    using R = Result<int>;
    if (email.empty()) return R::Failure(ErrorCode::InvalidArgument, "The email of the account");
    JSONValue body = JSONValue::MakeObject();
    body.Set("email", JSONValue(email));
    const auto v = Open("/v1/operators/password/forgot", UltraCanvas::JSON::Serialize(body));
    if (!v) return R::Failure(v.error.code, v.error.message, v.error.detail);
    return R::Succeed(static_cast<int>(v.Value().Get("minutes").GetInteger(10)));
}

Result<void> OperatorClient::ResetPassword(const std::string& email, const std::string& code, const std::string& newPassword, const std::string& code2) const {
    if (email.empty() || code.empty() || newPassword.size() < 12) return Result<void>::Failure(ErrorCode::InvalidArgument, "The email, the mailed code and a new password of 12 characters or more");
    JSONValue body = JSONValue::MakeObject();
    body.Set("email", JSONValue(email));
    body.Set("code", JSONValue(code));
    body.Set("new_password", JSONValue(newPassword));
    SetSecondFactor(body, code2);
    const auto v = Open("/v1/operators/password/reset", UltraCanvas::JSON::Serialize(body));
    if (!v) return Result<void>::Failure(v.error.code, v.error.message, v.error.detail);
    return Result<void>::Succeed();
}

Result<OperatorAccount> OperatorClient::ResetOperatorTwoFactor(const std::string& id) const {
    using R = Result<OperatorAccount>;
    const auto v = Ask("PUT", "/v1/operators/" + Encode(id), R"({"two_factor":false})");
    if (!v) return R::Failure(v.error.code, v.error.message, v.error.detail);
    return R::Succeed(OperatorAccountOf(v.Value()));
}

Result<WhoAmI> OperatorClient::Me() const {
    using R = Result<WhoAmI>;
    const auto v = Ask("GET", "/v1/operators/me");
    if (!v) return R::Failure(v.error.code, v.error.message, v.error.detail);
    WhoAmI me;
    me.id = v.Value().Get("id").GetString();
    me.name = v.Value().Get("name").GetString();
    me.role = v.Value().Get("role").GetString();
    me.bootstrap = v.Value().Get("bootstrap").GetBoolean(false);
    for (const JSONValue& p : v.Value().Get("permissions").GetElements()) me.permissions.push_back(p.GetString());
    return R::Succeed(me);
}

Result<std::vector<OperatorAccount>> OperatorClient::Operators() const {
    using R = Result<std::vector<OperatorAccount>>;
    const auto v = Ask("GET", "/v1/operators");
    if (!v) return R::Failure(v.error.code, v.error.message, v.error.detail);
    std::vector<OperatorAccount> out;
    for (const JSONValue& o : v.Value().Get("operators").GetElements()) out.push_back(OperatorAccountOf(o));
    return R::Succeed(out);
}

Result<OperatorAccount> OperatorClient::AddOperator(const std::string& email, const std::string& name, const std::string& role, const std::string& password) const {
    using R = Result<OperatorAccount>;
    JSONValue body = JSONValue::MakeObject();
    body.Set("email", JSONValue(email));
    body.Set("name", JSONValue(name));
    body.Set("role", JSONValue(role));
    body.Set("password", JSONValue(password));
    const auto v = Ask("POST", "/v1/operators", UltraCanvas::JSON::Serialize(body));
    if (!v) return R::Failure(v.error.code, v.error.message, v.error.detail);
    return R::Succeed(OperatorAccountOf(v.Value()));
}

Result<OperatorAccount> OperatorClient::UpdateOperator(const std::string& id, const std::string& name, const std::string& role, const std::string& password, int disabled) const {
    using R = Result<OperatorAccount>;
    JSONValue body = JSONValue::MakeObject();
    if (!name.empty()) body.Set("name", JSONValue(name));
    if (!role.empty()) body.Set("role", JSONValue(role));
    if (!password.empty()) body.Set("password", JSONValue(password));
    if (disabled >= 0) body.Set("disabled", JSONValue(disabled > 0));
    const auto v = Ask("PUT", "/v1/operators/" + Encode(id), UltraCanvas::JSON::Serialize(body));
    if (!v) return R::Failure(v.error.code, v.error.message, v.error.detail);
    return R::Succeed(OperatorAccountOf(v.Value()));
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

Result<std::vector<MerchantApplication>> OperatorClient::Applications(const std::string& state) const {
    using R = Result<std::vector<MerchantApplication>>;
    const auto v = Ask("GET", "/v1/applications" + (state.empty() ? std::string() : "?state=" + Encode(state)));
    if (!v) return R::Failure(v.error.code, v.error.message, v.error.detail);
    std::vector<MerchantApplication> out;
    for (const JSONValue& a : v.Value().Get("applications").GetElements()) out.push_back(ApplicationOf(a));
    return R::Succeed(out);
}

Result<MerchantApplication> OperatorClient::GetApplication(const std::string& id) const {
    using R = Result<MerchantApplication>;
    const auto v = Ask("GET", "/v1/applications/" + Encode(id));
    if (!v) return R::Failure(v.error.code, v.error.message, v.error.detail);
    return R::Succeed(ApplicationOf(v.Value()));
}

Result<Merchant> OperatorClient::ApproveApplication(const std::string& id, const std::string& merchantId, const std::string& message) const {
    using R = Result<Merchant>;
    JSONValue body = JSONValue::MakeObject();
    if (!merchantId.empty()) body.Set("id", JSONValue(merchantId));
    body.Set("message", JSONValue(message));
    const auto v = Ask("POST", "/v1/applications/" + Encode(id) + "/approve", UltraCanvas::JSON::Serialize(body));
    if (!v) return R::Failure(v.error.code, v.error.message, v.error.detail);
    return R::Succeed(MerchantOf(v.Value().Get("merchant")));
}

Result<MerchantApplication> OperatorClient::RejectApplication(const std::string& id, const std::string& message) const {
    using R = Result<MerchantApplication>;
    JSONValue body = JSONValue::MakeObject();
    body.Set("message", JSONValue(message));
    const auto v = Ask("POST", "/v1/applications/" + Encode(id) + "/reject", UltraCanvas::JSON::Serialize(body));
    if (!v) return R::Failure(v.error.code, v.error.message, v.error.detail);
    return R::Succeed(ApplicationOf(v.Value().Get("application")));
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

Result<DownloadedPackage> OperatorClient::DownloadPackage(const std::string& submissionId, const JSONValue& package, const std::string& folderUtf8) const {
    using R = Result<DownloadedPackage>;
    if (settings_.url.empty()) return R::Failure(ErrorCode::InvalidArgument, "No marketplace service URL: `settings set url <https://...>`");
    if (settings_.token.empty()) return R::Failure(ErrorCode::InvalidArgument, "No operator token: sign in first");
    const std::string sha = package.Get("sha256").GetString();
    if (sha.empty()) return R::Failure(ErrorCode::InvalidArgument, "The package names no SHA-256");
    std::error_code ec;
    const fs::path folder = UltraCanvas::PathFromUtf8(folderUtf8);
    fs::create_directories(folder, ec);
    if (ec) return R::Failure(ErrorCode::IoError, "Cannot create " + folderUtf8, ec.message());
    const fs::path file = folder / UltraCanvas::PathFromUtf8(PackageFileName(package));
    const MarketReply reply = download_(settings_.url + "/v1/submissions/" + Encode(submissionId) + "/packages/" + Encode(sha), "Bearer " + settings_.token,
                                        UltraCanvas::PathToUtf8(file));
    if (!reply.error.empty() || reply.status == 0) return R::Failure(ErrorCode::NetworkError, "The marketplace service could not be reached", reply.error);
    if (reply.status < 200 || reply.status >= 300) {
        fs::remove(file, ec);
        UltraCanvas::JSONParseResult parse;
        const JSONValue v = UltraCanvas::JSON::Parse(reply.body, &parse);
        const std::string description = parse.success && v.IsObject() ? v.Get("error_description").GetString() : std::string();
        return R::Failure(reply.status == 404 ? ErrorCode::NotFound : reply.status == 401 ? ErrorCode::SignatureInvalid : ErrorCode::NetworkError,
                          description.empty() ? "The marketplace service answered HTTP " + std::to_string(reply.status) : description, "HTTP " + std::to_string(reply.status));
    }
    DownloadedPackage out;
    out.sha256 = sha;
    out.fileUtf8 = UltraCanvas::PathToUtf8(file);
    out.size = fs::is_regular_file(file, ec) ? static_cast<uint64_t>(fs::file_size(file, ec)) : 0;
    return R::Succeed(out);
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

// ===========================================================================
// The users: the account service, with its own token
// ===========================================================================
Result<JSONValue> OperatorClient::AskAccount(const std::string& method, const std::string& path, const std::string& jsonBody) const {
    using R = Result<JSONValue>;
    if (settings_.accountUrl.empty()) return R::Failure(ErrorCode::InvalidArgument, "No account service URL: `settings set account_url <https://...>`");
    const std::string bearer = settings_.AccountBearer();
    if (bearer.empty()) return R::Failure(ErrorCode::InvalidArgument, "No account operator token: sign in (`signin <email>`), or `settings set account_token <token>` (the service's ULTRASTORE_ACCOUNT_OPERATOR_TOKEN)");
    const MarketReply reply = call_(method, settings_.accountUrl + path, "Bearer " + bearer, jsonBody);
    if (!reply.error.empty() || reply.status == 0) return R::Failure(ErrorCode::NetworkError, "The account service could not be reached", reply.error);
    UltraCanvas::JSONParseResult parse;
    const JSONValue v = UltraCanvas::JSON::Parse(reply.body, &parse);
    if (reply.status < 200 || reply.status >= 300) {
        const std::string description = parse.success && v.IsObject() ? v.Get("error_description").GetString() : std::string();
        return R::Failure(reply.status == 404 ? ErrorCode::NotFound : reply.status == 401 ? ErrorCode::SignatureInvalid : ErrorCode::NetworkError,
                          description.empty() ? "The account service answered HTTP " + std::to_string(reply.status) : description,
                          "HTTP " + std::to_string(reply.status));
    }
    if (!parse.success || !v.IsObject()) return R::Failure(ErrorCode::ParseError, "The account service's answer is not JSON", parse.errorMessage);
    return R::Succeed(v);
}

Result<std::string> OperatorClient::AccountHealth() const {
    using R = Result<std::string>;
    if (settings_.accountUrl.empty()) return R::Failure(ErrorCode::InvalidArgument, "No account service URL: `settings set account_url <https://...>`");
    const MarketReply reply = call_("GET", settings_.accountUrl + "/healthz", {}, {});
    if (!reply.error.empty() || reply.status == 0) return R::Failure(ErrorCode::NetworkError, "The account service could not be reached", reply.error);
    if (reply.status < 200 || reply.status >= 300) return R::Failure(ErrorCode::NetworkError, "The account service answered HTTP " + std::to_string(reply.status), reply.body);
    return R::Succeed(reply.body);
}

Result<UsersPage> OperatorClient::Users(const std::string& state, const std::string& query) const {
    using R = Result<UsersPage>;
    std::string path = "/v1/users";
    std::string sep = "?";
    if (!state.empty()) { path += sep + "state=" + Encode(state); sep = "&"; }
    if (!query.empty()) { path += sep + "q=" + Encode(query); }
    const auto v = AskAccount("GET", path);
    if (!v) return R::Failure(v.error.code, v.error.message, v.error.detail);
    UsersPage page;
    for (const JSONValue& u : v.Value().Get("users").GetElements()) page.users.push_back(UserOf(u));
    page.stats = UserStatsOf(v.Value().Get("stats"));
    return R::Succeed(page);
}

Result<UserFile> OperatorClient::GetUser(const std::string& id) const {
    using R = Result<UserFile>;
    const auto v = AskAccount("GET", "/v1/users/" + Encode(id));
    if (!v) return R::Failure(v.error.code, v.error.message, v.error.detail);
    return R::Succeed(UserFileOf(v.Value()));
}

Result<UserStats> OperatorClient::GetUserStats() const {
    using R = Result<UserStats>;
    const auto v = AskAccount("GET", "/v1/stats");
    if (!v) return R::Failure(v.error.code, v.error.message, v.error.detail);
    return R::Succeed(UserStatsOf(v.Value()));
}

Result<UserNotice> OperatorClient::NotifyUser(const std::string& id, const std::string& kind, const std::string& subject, const std::string& text) const {
    using R = Result<UserNotice>;
    JSONValue body = JSONValue::MakeObject();
    body.Set("kind", JSONValue(kind.empty() ? std::string("notice") : kind));
    body.Set("subject", JSONValue(subject));
    body.Set("text", JSONValue(text));
    const auto v = AskAccount("POST", "/v1/users/" + Encode(id) + "/notice", UltraCanvas::JSON::Serialize(body));
    if (!v) return R::Failure(v.error.code, v.error.message, v.error.detail);
    const JSONValue n = v.Value().Get("notice");
    UserNotice out;
    out.id = n.Get("id").GetString(); out.kind = n.Get("kind").GetString(); out.subject = n.Get("subject").GetString(); out.text = n.Get("text").GetString();
    out.createdAt = n.Get("created_at").GetString(); out.createdBy = n.Get("created_by").GetString();
    return R::Succeed(out);
}

namespace {
JSONValue ReasonBody(const std::string& reason) {
    JSONValue body = JSONValue::MakeObject();
    body.Set("reason", JSONValue(reason));
    return body;
}
} // namespace

Result<int> OperatorClient::SignOutUser(const std::string& id, const std::string& reason) const {
    using R = Result<int>;
    const auto v = AskAccount("POST", "/v1/users/" + Encode(id) + "/signout-all", UltraCanvas::JSON::Serialize(ReasonBody(reason)));
    if (!v) return R::Failure(v.error.code, v.error.message, v.error.detail);
    return R::Succeed(static_cast<int>(v.Value().Get("signed_out").GetInteger(0)));
}

Result<void> OperatorClient::ResetUserTwoFactor(const std::string& id, const std::string& reason) const {
    const auto v = AskAccount("POST", "/v1/users/" + Encode(id) + "/two-factor/reset", UltraCanvas::JSON::Serialize(ReasonBody(reason)));
    if (!v) return Result<void>::Failure(v.error.code, v.error.message, v.error.detail);
    return Result<void>::Succeed();
}

Result<User> OperatorClient::SuspendUser(const std::string& id, const std::string& reason, int days) const {
    using R = Result<User>;
    JSONValue body = ReasonBody(reason);
    body.Set("days", JSONValue(static_cast<int64_t>(days)));
    const auto v = AskAccount("POST", "/v1/users/" + Encode(id) + "/suspend", UltraCanvas::JSON::Serialize(body));
    if (!v) return R::Failure(v.error.code, v.error.message, v.error.detail);
    return R::Succeed(UserOf(v.Value().Get("user")));
}

Result<User> OperatorClient::ReinstateUser(const std::string& id, const std::string& reason) const {
    using R = Result<User>;
    const auto v = AskAccount("POST", "/v1/users/" + Encode(id) + "/reinstate", UltraCanvas::JSON::Serialize(ReasonBody(reason)));
    if (!v) return R::Failure(v.error.code, v.error.message, v.error.detail);
    return R::Succeed(UserOf(v.Value().Get("user")));
}

Result<void> OperatorClient::DeleteUser(const std::string& id, const std::string& reason) const {
    const auto v = AskAccount("POST", "/v1/users/" + Encode(id) + "/delete", UltraCanvas::JSON::Serialize(ReasonBody(reason)));
    if (!v) return Result<void>::Failure(v.error.code, v.error.message, v.error.detail);
    return Result<void>::Succeed();
}

Result<UserArchive> OperatorClient::ReleaseArchive(const std::string& archiveId) const {
    using R = Result<UserArchive>;
    const auto v = AskAccount("POST", "/v1/archives/" + Encode(archiveId) + "/release", "{}");
    if (!v) return R::Failure(v.error.code, v.error.message, v.error.detail);
    const JSONValue a = v.Value().Get("archive");
    UserArchive out;
    out.id = a.Get("id").GetString(); out.state = a.Get("state").GetString(); out.due = a.Get("due").GetString();
    out.requestedAt = a.Get("requested_at").GetString(); out.releasedAt = a.Get("released_at").GetString();
    return R::Succeed(out);
}

Result<std::pair<int, int>> OperatorClient::AccountMaintenance() const {
    using R = Result<std::pair<int, int>>;
    const auto v = AskAccount("POST", "/v1/maintenance", "{}");
    if (!v) return R::Failure(v.error.code, v.error.message, v.error.detail);
    return R::Succeed(std::make_pair(static_cast<int>(v.Value().Get("deleted").GetInteger(0)), static_cast<int>(v.Value().Get("reinstated").GetInteger(0))));
}

Result<AuditPage> OperatorClient::AccountAudit(int64_t since, int limit) const {
    using R = Result<AuditPage>;
    const auto v = AskAccount("GET", "/v1/audit?since=" + std::to_string(since) + "&limit=" + std::to_string(limit));
    if (!v) return R::Failure(v.error.code, v.error.message, v.error.detail);
    AuditPage page;
    for (const JSONValue& r : v.Value().Get("audit").GetElements()) page.rows.push_back(AuditRowOf(r));
    page.next = page.rows.empty() ? since : page.rows.back().id;
    return R::Succeed(page);
}

Result<AuditCheck> OperatorClient::VerifyAccountAudit() const {
    using R = Result<AuditCheck>;
    const auto v = AskAccount("GET", "/v1/audit/verify");
    if (!v) return R::Failure(v.error.code, v.error.message, v.error.detail);
    AuditCheck c;
    c.ok = v.Value().Get("ok").GetBoolean(false);
    c.rows = v.Value().Get("rows").GetInteger(0);
    c.brokenAt = v.Value().Get("broken_at").GetInteger(0);
    c.lastHash = v.Value().Get("last_hash").GetString();
    return R::Succeed(c);
}

} // namespace UltraStoreOperator
