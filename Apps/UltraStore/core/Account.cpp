// Apps/UltraStore/core/Account.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "Account.h"

#include "UltraCanvasPathUtf8.h"
#include "DataFormats/UltraCanvasJSON.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include "UltraNet/UltraNetCore.h"
#include "UltraNet/UltraNetHttp.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using UltraCanvas::JSONValue;
using UltraCanvas::Tr;
using UltraCanvas::TrF;

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

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

JSONValue DeviceJson(const DeviceInfo& device) {
    JSONValue d = JSONValue::MakeObject();
    d.Set("name", JSONValue(device.name));
    d.Set("os", JSONValue(device.os));
    d.Set("client", JSONValue(device.client));
    return d;
}

std::vector<std::string> StringsOf(const JSONValue& v) {
    std::vector<std::string> out;
    if (!v.IsArray()) return out;
    for (const JSONValue& item : v.GetElements()) out.push_back(item.GetString());
    return out;
}

std::string ReadWhole(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

} // namespace

// ===========================================================================
// The HTTP call
// ===========================================================================
AccountCall UltraNetAccountCall() {
    return [](const std::string& method, const std::string& url, const std::string& authorization, const std::string& jsonBody) {
        HttpReply out;
        if (!UltraNet_IsInitialized()) UltraNet_Initialize();
        UltraNetHttpRequest request;
        request.url = url;
        request.method = method == "POST" ? UltraNetHttpMethod::Post : method == "PUT" ? UltraNetHttpMethod::Put
                       : method == "DELETE" ? UltraNetHttpMethod::Delete : UltraNetHttpMethod::Get;
        request.options.httpsOnlyRedirects = true;
        request.options.maxReceiveSize = int64_t(8) << 20;   // a data archive can be a few MB
        if (!authorization.empty()) request.headers.Set("Authorization", authorization);
        if (method == "POST" || method == "PUT") {
            request.headers.Set("Content-Type", "application/json");
            request.body.assign(jsonBody.begin(), jsonBody.end());
        }
        UltraNetResponse response;
        const UltraNetResult r = UltraNet_HttpRequest(request, response);
        out.status = response.statusCode;
        out.body = response.GetBodyAsString();
        if (!r && r.code != UltraNetResultCode::HttpError) out.error = r.message.empty() ? "no answer" : r.message;
        if (!response.IsComplete()) out.error = response.transferError;
        return out;
    };
}

std::string DefaultDeviceName() {
#ifdef _WIN32
    wchar_t name[256];
    DWORD size = 256;
    if (GetComputerNameW(name, &size)) return UltraCanvas::PathUtf8Detail::Utf16ToUtf8(std::wstring(name, size));
#else
    char name[256];
    if (gethostname(name, sizeof name) == 0) { name[sizeof name - 1] = 0; if (name[0]) return name; }
#endif
    return Tr("This machine");
}

// ===========================================================================
// The records
// ===========================================================================
bool AccountLicence::InstalledOn(const std::string& deviceId) const {
    return std::find(installedOn.begin(), installedOn.end(), deviceId) != installedOn.end();
}

std::string AccountProfile::Name() const {
    if (!displayName.empty()) return displayName;
    const auto at = email.find('@');
    return at == std::string::npos ? email : email.substr(0, at);
}

std::string AccountProfile::Initials() const {
    const std::string name = Name();
    std::string out;
    bool start = true;
    for (unsigned char c : name) {
        if (std::isspace(c) || c == '.' || c == '_' || c == '-') { start = true; continue; }
        if (start && std::isalnum(c) && out.size() < 2) out += static_cast<char>(std::toupper(c));
        start = false;
    }
    if (out.empty() && !name.empty()) out = std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(name[0]))));
    return out;
}

AccountDevice DeviceOf(const JSONValue& v) {
    AccountDevice d;
    d.id = v.Get("id").GetString();
    d.name = v.Get("device_name").GetString();
    d.os = v.Get("os").GetString();
    d.client = v.Get("client").GetString();
    d.createdAt = v.Get("created_at").GetString();
    d.lastSeen = v.Get("last_seen").GetString();
    d.lastIp = v.Get("last_ip").GetString();
    d.current = v.Get("current").GetBoolean(false);
    return d;
}

AccountLicence AccountLicenceOf(const JSONValue& v) {
    AccountLicence l;
    l.id = v.Get("id").GetString();
    l.app = v.Get("app").GetString();
    l.orderId = v.Get("order").GetString();
    l.issued = v.Get("issued").GetString();
    l.updates = v.Get("updates").GetString();
    l.updatesUntil = v.Get("updates_until").GetString();
    l.validUntil = v.Get("valid_until").GetString();
    l.attachedAt = v.Get("attached_at").GetString();
    l.token = v.Get("licence").GetString();
    l.seats = static_cast<int>(v.Get("seats").GetInteger(0));
    l.seatsUsed = static_cast<int>(v.Get("seats_used").GetInteger(0));
    const JSONValue installs = v.Get("installs");
    if (installs.IsArray()) {
        for (const JSONValue& i : installs.GetElements()) {
            if (i.Get("removed_at").GetString().empty()) l.installedOn.push_back(i.Get("session").GetString());
        }
    }
    return l;
}

AccountNotice NoticeOf(const JSONValue& v) {
    AccountNotice n;
    n.id = v.Get("id").GetString();
    n.kind = v.Get("kind").GetString();
    n.subject = v.Get("subject").GetString();
    n.text = v.Get("text").GetString();
    n.createdAt = v.Get("created_at").GetString();
    n.createdBy = v.Get("created_by").GetString();
    n.readAt = v.Get("read_at").GetString();
    return n;
}

AccountArchive ArchiveOf(const JSONValue& v) {
    AccountArchive a;
    a.id = v.Get("id").GetString();
    a.state = v.Get("state").GetString();
    a.due = v.Get("due").GetString();
    a.requestedAt = v.Get("requested_at").GetString();
    a.releasedAt = v.Get("released_at").GetString();
    a.downloadedAt = v.Get("downloaded_at").GetString();
    return a;
}

AccountProfile ProfileOf(const JSONValue& v) {
    AccountProfile p;
    p.id = v.Get("id").GetString();
    p.email = v.Get("email").GetString();
    p.emailVerified = v.Get("email_verified").GetBoolean(false);
    p.displayName = v.Get("display_name").GetString();
    p.handle = v.Get("handle").GetString();
    p.language = v.Get("language").GetString();
    p.country = v.Get("country").GetString();
    p.yearOfBirth = static_cast<int>(v.Get("year_of_birth").GetInteger(0));
    p.about = v.Get("about").GetString();
    p.state = v.Get("state").GetString();
    p.createdAt = v.Get("created_at").GetString();
    p.passwordChangedAt = v.Get("password_changed_at").GetString();
    p.twoFactor = v.Get("two_factor").GetBoolean(false);
    p.recoveryCodesLeft = static_cast<int>(v.Get("recovery_codes_left").GetInteger(0));
    p.deletionRequestedAt = v.Get("deletion_requested_at").GetString();
    p.deleteAfter = v.Get("delete_after").GetString();
    const JSONValue s = v.Get("settings");
    if (s.IsObject()) {
        const JSONValue n = s.Get("notifications");
        if (n.IsObject()) {
            for (const auto& member : n.GetMembers()) p.settings.notifications[member.first] = member.second.GetString();
        }
        p.settings.historyOnAccount = s.Get("history_on_account").GetBoolean(false);
        p.settings.publicProfile = s.Get("public_profile").GetBoolean(true);
        p.settings.marketing = s.Get("marketing").GetBoolean(false);
    }
    return p;
}

Me MeOf(const JSONValue& v) {
    Me me;
    me.profile = ProfileOf(v);
    me.device = DeviceOf(v.Get("device"));
    for (const JSONValue& d : v.Get("devices").GetElements()) me.devices.push_back(DeviceOf(d));
    for (const JSONValue& l : v.Get("licences").GetElements()) me.licences.push_back(AccountLicenceOf(l));
    me.unreadNotices = static_cast<int>(v.Get("unread_notices").GetInteger(0));
    if (v.Get("archive").IsObject()) me.archive = ArchiveOf(v.Get("archive"));
    return me;
}

// ===========================================================================
// The session on disk
// ===========================================================================
std::string AccountSession::Path(const std::string& storeRootUtf8) {
    return UltraCanvas::PathToUtf8(UltraCanvas::PathFromUtf8(storeRootUtf8) / "account.json");
}

AccountSession AccountSession::Load(const std::string& storeRootUtf8) {
    AccountSession s;
    const std::string text = ReadWhole(UltraCanvas::PathFromUtf8(Path(storeRootUtf8)));
    if (text.empty()) return s;
    UltraCanvas::JSONParseResult parse;
    const JSONValue v = UltraCanvas::JSON::Parse(text, &parse);
    if (!parse.success || !v.IsObject()) return s;
    s.url = TrimSlash(v.Get("url").GetString());
    s.token = v.Get("token").GetString();
    s.userId = v.Get("user_id").GetString();
    s.email = v.Get("email").GetString();
    s.displayName = v.Get("display_name").GetString();
    s.deviceId = v.Get("device_id").GetString();
    return s;
}

Result<void> AccountSession::Save(const std::string& storeRootUtf8) const {
    const fs::path path = UltraCanvas::PathFromUtf8(Path(storeRootUtf8));
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) return Result<void>::Failure(ErrorCode::IoError, "Cannot create " + UltraCanvas::PathToUtf8(path.parent_path()), ec.message());
    JSONValue v = JSONValue::MakeObject();
    v.Set("url", JSONValue(TrimSlash(url)));
    v.Set("token", JSONValue(token));
    v.Set("user_id", JSONValue(userId));
    v.Set("email", JSONValue(email));
    v.Set("display_name", JSONValue(displayName));
    v.Set("device_id", JSONValue(deviceId));
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) return Result<void>::Failure(ErrorCode::IoError, "Cannot write " + UltraCanvas::PathToUtf8(path));
        UltraCanvas::JSONSerializeOptions options;
        options.pretty = true;
        const std::string text = UltraCanvas::JSON::Serialize(v, options) + "\n";
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!out) return Result<void>::Failure(ErrorCode::IoError, "Cannot write " + UltraCanvas::PathToUtf8(path));
    }
    // The token signs in as the person: the file is the owner's alone.
    fs::permissions(path, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace, ec);
    return Result<void>::Succeed();
}

void AccountSession::Clear(const std::string& storeRootUtf8) {
    std::error_code ec;
    fs::remove(UltraCanvas::PathFromUtf8(Path(storeRootUtf8)), ec);
}

// ===========================================================================
// The client
// ===========================================================================
AccountClient::AccountClient(std::string serviceUrl, AccountCall call) : url_(TrimSlash(std::move(serviceUrl))), call_(std::move(call)) {}

Result<JSONValue> AccountClient::Ask(const std::string& method, const std::string& path, const std::string& token, const std::string& jsonBody) const {
    using R = Result<JSONValue>;
    if (url_.empty()) return R::Failure(ErrorCode::NotAvailable, Tr("The catalog names no account service"));
    const HttpReply reply = call_(method, url_ + path, token.empty() ? std::string() : "Bearer " + token, jsonBody);
    if (!reply.error.empty() || reply.status == 0) return R::Failure(ErrorCode::NetworkError, Tr("The account service could not be reached"), reply.error);
    UltraCanvas::JSONParseResult parse;
    const JSONValue v = UltraCanvas::JSON::Parse(reply.body, &parse);
    if (reply.status < 200 || reply.status >= 300) {
        const bool said = parse.success && v.IsObject();
        const std::string description = said ? v.Get("error_description").GetString() : std::string();
        const std::string code = said ? v.Get("error").GetString() : std::string();
        return R::Failure(reply.status == 404 ? ErrorCode::NotFound : reply.status == 401 || reply.status == 403 ? ErrorCode::SignatureInvalid : ErrorCode::NetworkError,
                          description.empty() ? TrF("The account service answered HTTP {0}", reply.status) : description,
                          code.empty() ? "HTTP " + std::to_string(reply.status) : code);
    }
    if (!parse.success || !v.IsObject()) return R::Failure(ErrorCode::ParseError, Tr("The account service's answer is not JSON"), parse.errorMessage);
    return R::Succeed(v);
}

Result<Me> AccountClient::MeFrom(const Result<JSONValue>& answer) const {
    if (!answer) return Result<Me>::Failure(answer.error.code, answer.error.message, answer.error.detail);
    return Result<Me>::Succeed(MeOf(answer.Value()));
}

#define ULTRASTORE_FORWARD_FAILURE(R, answer) if (!(answer)) return R::Failure((answer).error.code, (answer).error.message, (answer).error.detail)

Result<std::string> AccountClient::Health() const {
    using R = Result<std::string>;
    if (url_.empty()) return R::Failure(ErrorCode::NotAvailable, Tr("The catalog names no account service"));
    const HttpReply reply = call_("GET", url_ + "/healthz", {}, {});
    if (!reply.error.empty() || reply.status == 0) return R::Failure(ErrorCode::NetworkError, Tr("The account service could not be reached"), reply.error);
    if (reply.status < 200 || reply.status >= 300) return R::Failure(ErrorCode::NetworkError, TrF("The account service answered HTTP {0}", reply.status), reply.body);
    return R::Succeed(reply.body);
}

Result<void> AccountClient::SignUp(const std::string& email, const std::string& password, const std::string& country, int yearOfBirth,
                                   const std::string& language, bool marketing) const {
    JSONValue body = JSONValue::MakeObject();
    body.Set("email", JSONValue(email));
    body.Set("password", JSONValue(password));
    body.Set("country", JSONValue(country));
    body.Set("year_of_birth", JSONValue(static_cast<int64_t>(yearOfBirth)));
    body.Set("language", JSONValue(language.empty() ? std::string("en") : language));
    body.Set("marketing", JSONValue(marketing));
    const auto answer = Ask("POST", "/v1/signup", {}, UltraCanvas::JSON::Serialize(body));
    ULTRASTORE_FORWARD_FAILURE(Result<void>, answer);
    return Result<void>::Succeed();
}

namespace {
SignedIn SignedInOf(const JSONValue& v) {
    SignedIn s;
    s.token = v.Get("token").GetString();
    s.device = DeviceOf(v.Get("device"));
    s.profile = ProfileOf(v.Get("user"));
    return s;
}
} // namespace

Result<SignedIn> AccountClient::VerifySignUp(const std::string& email, const std::string& code, const DeviceInfo& device) const {
    using R = Result<SignedIn>;
    JSONValue body = JSONValue::MakeObject();
    body.Set("email", JSONValue(email));
    body.Set("code", JSONValue(code));
    body.Set("device", DeviceJson(device));
    const auto answer = Ask("POST", "/v1/signup/verify", {}, UltraCanvas::JSON::Serialize(body));
    ULTRASTORE_FORWARD_FAILURE(R, answer);
    SignedIn s = SignedInOf(answer.Value());
    if (s.token.empty()) return R::Failure(ErrorCode::ParseError, Tr("The account service's answer lacks the session token"));
    return R::Succeed(std::move(s));
}

Result<void> AccountClient::ResendCode(const std::string& email) const {
    JSONValue body = JSONValue::MakeObject();
    body.Set("email", JSONValue(email));
    const auto answer = Ask("POST", "/v1/signup/resend", {}, UltraCanvas::JSON::Serialize(body));
    ULTRASTORE_FORWARD_FAILURE(Result<void>, answer);
    return Result<void>::Succeed();
}

Result<SignedIn> AccountClient::SignIn(const std::string& email, const std::string& password, const DeviceInfo& device,
                                       const std::string& totp, const std::string& recoveryCode) const {
    using R = Result<SignedIn>;
    JSONValue body = JSONValue::MakeObject();
    body.Set("email", JSONValue(email));
    body.Set("password", JSONValue(password));
    body.Set("device", DeviceJson(device));
    if (!totp.empty()) body.Set("totp", JSONValue(totp));
    if (!recoveryCode.empty()) body.Set("recovery_code", JSONValue(recoveryCode));
    const auto answer = Ask("POST", "/v1/signin", {}, UltraCanvas::JSON::Serialize(body));
    ULTRASTORE_FORWARD_FAILURE(R, answer);
    SignedIn s = SignedInOf(answer.Value());
    if (s.token.empty()) return R::Failure(ErrorCode::ParseError, Tr("The account service's answer lacks the session token"));
    return R::Succeed(std::move(s));
}

Result<void> AccountClient::ForgotPassword(const std::string& email) const {
    JSONValue body = JSONValue::MakeObject();
    body.Set("email", JSONValue(email));
    const auto answer = Ask("POST", "/v1/password/forgot", {}, UltraCanvas::JSON::Serialize(body));
    ULTRASTORE_FORWARD_FAILURE(Result<void>, answer);
    return Result<void>::Succeed();
}

Result<void> AccountClient::ResetPassword(const std::string& email, const std::string& code, const std::string& newPassword,
                                          const std::string& totp, const std::string& recoveryCode) const {
    JSONValue body = JSONValue::MakeObject();
    body.Set("email", JSONValue(email));
    body.Set("code", JSONValue(code));
    body.Set("new_password", JSONValue(newPassword));
    if (!totp.empty()) body.Set("totp", JSONValue(totp));
    if (!recoveryCode.empty()) body.Set("recovery_code", JSONValue(recoveryCode));
    const auto answer = Ask("POST", "/v1/password/reset", {}, UltraCanvas::JSON::Serialize(body));
    ULTRASTORE_FORWARD_FAILURE(Result<void>, answer);
    return Result<void>::Succeed();
}

Result<Me> AccountClient::GetMe(const std::string& token) const {
    return MeFrom(Ask("GET", "/v1/me", token));
}

Result<Me> AccountClient::UpdateProfile(const std::string& token, const ProfileChange& change) const {
    JSONValue body = JSONValue::MakeObject();
    if (change.displayName) body.Set("display_name", JSONValue(*change.displayName));
    if (change.handle) body.Set("handle", JSONValue(*change.handle));
    if (change.language) body.Set("language", JSONValue(*change.language));
    if (change.country) body.Set("country", JSONValue(*change.country));
    if (change.about) body.Set("about", JSONValue(*change.about));
    if (change.yearOfBirth) body.Set("year_of_birth", JSONValue(static_cast<int64_t>(*change.yearOfBirth)));
    return MeFrom(Ask("PUT", "/v1/me", token, UltraCanvas::JSON::Serialize(body)));
}

Result<Me> AccountClient::UpdateSettings(const std::string& token, const AccountSettings& settings) const {
    JSONValue body = JSONValue::MakeObject();
    JSONValue notifications = JSONValue::MakeObject();
    for (const auto& [key, value] : settings.notifications) notifications.Set(key, JSONValue(value));
    body.Set("notifications", notifications);
    body.Set("history_on_account", JSONValue(settings.historyOnAccount));
    body.Set("public_profile", JSONValue(settings.publicProfile));
    body.Set("marketing", JSONValue(settings.marketing));
    return MeFrom(Ask("PUT", "/v1/me/settings", token, UltraCanvas::JSON::Serialize(body)));
}

Result<void> AccountClient::ChangeEmail(const std::string& token, const std::string& email, const std::string& password) const {
    JSONValue body = JSONValue::MakeObject();
    body.Set("email", JSONValue(email));
    body.Set("password", JSONValue(password));
    const auto answer = Ask("POST", "/v1/me/email", token, UltraCanvas::JSON::Serialize(body));
    ULTRASTORE_FORWARD_FAILURE(Result<void>, answer);
    return Result<void>::Succeed();
}

Result<Me> AccountClient::VerifyEmailChange(const std::string& token, const std::string& code) const {
    JSONValue body = JSONValue::MakeObject();
    body.Set("code", JSONValue(code));
    return MeFrom(Ask("POST", "/v1/me/email/verify", token, UltraCanvas::JSON::Serialize(body)));
}

Result<int> AccountClient::ChangePassword(const std::string& token, const std::string& current, const std::string& fresh) const {
    JSONValue body = JSONValue::MakeObject();
    body.Set("current", JSONValue(current));
    body.Set("new", JSONValue(fresh));
    const auto answer = Ask("POST", "/v1/me/password", token, UltraCanvas::JSON::Serialize(body));
    ULTRASTORE_FORWARD_FAILURE(Result<int>, answer);
    return Result<int>::Succeed(static_cast<int>(answer.Value().Get("signed_out").GetInteger(0)));
}

Result<TwoFactorSetup> AccountClient::TwoFactorSetupStart(const std::string& token) const {
    using R = Result<TwoFactorSetup>;
    const auto answer = Ask("POST", "/v1/me/two-factor/setup", token, "{}");
    ULTRASTORE_FORWARD_FAILURE(R, answer);
    TwoFactorSetup s;
    s.secret = answer.Value().Get("secret").GetString();
    s.otpauthUrl = answer.Value().Get("otpauth_url").GetString();
    return R::Succeed(std::move(s));
}

Result<std::vector<std::string>> AccountClient::TwoFactorEnable(const std::string& token, const std::string& code) const {
    using R = Result<std::vector<std::string>>;
    JSONValue body = JSONValue::MakeObject();
    body.Set("code", JSONValue(code));
    const auto answer = Ask("POST", "/v1/me/two-factor/enable", token, UltraCanvas::JSON::Serialize(body));
    ULTRASTORE_FORWARD_FAILURE(R, answer);
    return R::Succeed(StringsOf(answer.Value().Get("recovery_codes")));
}

Result<void> AccountClient::TwoFactorDisable(const std::string& token, const std::string& code) const {
    JSONValue body = JSONValue::MakeObject();
    body.Set("code", JSONValue(code));
    const auto answer = Ask("POST", "/v1/me/two-factor/disable", token, UltraCanvas::JSON::Serialize(body));
    ULTRASTORE_FORWARD_FAILURE(Result<void>, answer);
    return Result<void>::Succeed();
}

Result<std::vector<std::string>> AccountClient::NewRecoveryCodes(const std::string& token, const std::string& code) const {
    using R = Result<std::vector<std::string>>;
    JSONValue body = JSONValue::MakeObject();
    body.Set("code", JSONValue(code));
    const auto answer = Ask("POST", "/v1/me/two-factor/recovery-codes", token, UltraCanvas::JSON::Serialize(body));
    ULTRASTORE_FORWARD_FAILURE(R, answer);
    return R::Succeed(StringsOf(answer.Value().Get("recovery_codes")));
}

Result<std::vector<AccountDevice>> AccountClient::Devices(const std::string& token) const {
    using R = Result<std::vector<AccountDevice>>;
    const auto answer = Ask("GET", "/v1/me/devices", token);
    ULTRASTORE_FORWARD_FAILURE(R, answer);
    std::vector<AccountDevice> out;
    for (const JSONValue& d : answer.Value().Get("devices").GetElements()) out.push_back(DeviceOf(d));
    return R::Succeed(std::move(out));
}

Result<void> AccountClient::RenameDevice(const std::string& token, const std::string& deviceId, const std::string& name) const {
    JSONValue body = JSONValue::MakeObject();
    body.Set("name", JSONValue(name));
    const auto answer = Ask("PUT", "/v1/me/devices/" + Encode(deviceId), token, UltraCanvas::JSON::Serialize(body));
    ULTRASTORE_FORWARD_FAILURE(Result<void>, answer);
    return Result<void>::Succeed();
}

Result<void> AccountClient::SignOutDevice(const std::string& token, const std::string& deviceId) const {
    const auto answer = Ask("DELETE", "/v1/me/devices/" + Encode(deviceId), token);
    ULTRASTORE_FORWARD_FAILURE(Result<void>, answer);
    return Result<void>::Succeed();
}

Result<void> AccountClient::SignOut(const std::string& token) const {
    const auto answer = Ask("POST", "/v1/me/signout", token, "{}");
    ULTRASTORE_FORWARD_FAILURE(Result<void>, answer);
    return Result<void>::Succeed();
}

Result<int> AccountClient::SignOutOthers(const std::string& token) const {
    const auto answer = Ask("POST", "/v1/me/signout-all", token, "{}");
    ULTRASTORE_FORWARD_FAILURE(Result<int>, answer);
    return Result<int>::Succeed(static_cast<int>(answer.Value().Get("signed_out").GetInteger(0)));
}

Result<std::vector<AccountLicence>> AccountClient::Licences(const std::string& token) const {
    using R = Result<std::vector<AccountLicence>>;
    const auto answer = Ask("GET", "/v1/me/licences", token);
    ULTRASTORE_FORWARD_FAILURE(R, answer);
    std::vector<AccountLicence> out;
    for (const JSONValue& l : answer.Value().Get("licences").GetElements()) out.push_back(AccountLicenceOf(l));
    return R::Succeed(std::move(out));
}

Result<AccountLicence> AccountClient::AttachLicence(const std::string& token, const std::string& licenceToken, int seats) const {
    using R = Result<AccountLicence>;
    JSONValue body = JSONValue::MakeObject();
    body.Set("licence", JSONValue(licenceToken));
    if (seats > 0) body.Set("seats", JSONValue(static_cast<int64_t>(seats)));
    const auto answer = Ask("POST", "/v1/me/licences", token, UltraCanvas::JSON::Serialize(body));
    ULTRASTORE_FORWARD_FAILURE(R, answer);
    return R::Succeed(AccountLicenceOf(answer.Value().Get("licence")));
}

Result<void> AccountClient::ReleaseLicence(const std::string& token, const std::string& licenceId) const {
    const auto answer = Ask("DELETE", "/v1/me/licences/" + Encode(licenceId), token);
    ULTRASTORE_FORWARD_FAILURE(Result<void>, answer);
    return Result<void>::Succeed();
}

Result<AccountLicence> AccountClient::InstallLicence(const std::string& token, const std::string& licenceId) const {
    using R = Result<AccountLicence>;
    const auto answer = Ask("POST", "/v1/me/licences/" + Encode(licenceId) + "/install", token, "{}");
    ULTRASTORE_FORWARD_FAILURE(R, answer);
    return R::Succeed(AccountLicenceOf(answer.Value().Get("licence")));
}

Result<AccountLicence> AccountClient::UninstallLicence(const std::string& token, const std::string& licenceId) const {
    using R = Result<AccountLicence>;
    const auto answer = Ask("POST", "/v1/me/licences/" + Encode(licenceId) + "/uninstall", token, "{}");
    ULTRASTORE_FORWARD_FAILURE(R, answer);
    return R::Succeed(AccountLicenceOf(answer.Value().Get("licence")));
}

Result<std::vector<AccountNotice>> AccountClient::Notices(const std::string& token) const {
    using R = Result<std::vector<AccountNotice>>;
    const auto answer = Ask("GET", "/v1/me/notices", token);
    ULTRASTORE_FORWARD_FAILURE(R, answer);
    std::vector<AccountNotice> out;
    for (const JSONValue& n : answer.Value().Get("notices").GetElements()) out.push_back(NoticeOf(n));
    return R::Succeed(std::move(out));
}

Result<void> AccountClient::MarkNoticeRead(const std::string& token, const std::string& noticeId) const {
    const auto answer = Ask("POST", "/v1/me/notices/" + Encode(noticeId) + "/read", token, "{}");
    ULTRASTORE_FORWARD_FAILURE(Result<void>, answer);
    return Result<void>::Succeed();
}

Result<AccountArchive> AccountClient::RequestArchive(const std::string& token) const {
    using R = Result<AccountArchive>;
    const auto answer = Ask("POST", "/v1/me/archive", token, "{}");
    ULTRASTORE_FORWARD_FAILURE(R, answer);
    return R::Succeed(ArchiveOf(answer.Value().Get("archive")));
}

Result<std::vector<AccountArchive>> AccountClient::Archives(const std::string& token) const {
    using R = Result<std::vector<AccountArchive>>;
    const auto answer = Ask("GET", "/v1/me/archive", token);
    ULTRASTORE_FORWARD_FAILURE(R, answer);
    std::vector<AccountArchive> out;
    for (const JSONValue& a : answer.Value().Get("archives").GetElements()) out.push_back(ArchiveOf(a));
    return R::Succeed(std::move(out));
}

Result<std::string> AccountClient::DownloadArchive(const std::string& token, const std::string& archiveId) const {
    using R = Result<std::string>;
    if (url_.empty()) return R::Failure(ErrorCode::NotAvailable, Tr("The catalog names no account service"));
    const HttpReply reply = call_("GET", url_ + "/v1/me/archive/" + Encode(archiveId) + "/download", "Bearer " + token, {});
    if (!reply.error.empty() || reply.status == 0) return R::Failure(ErrorCode::NetworkError, Tr("The account service could not be reached"), reply.error);
    if (reply.status < 200 || reply.status >= 300) {
        UltraCanvas::JSONParseResult parse;
        const JSONValue v = UltraCanvas::JSON::Parse(reply.body, &parse);
        const std::string description = parse.success && v.IsObject() ? v.Get("error_description").GetString() : std::string();
        return R::Failure(reply.status == 404 ? ErrorCode::NotFound : ErrorCode::NetworkError,
                          description.empty() ? TrF("The account service answered HTTP {0}", reply.status) : description,
                          parse.success && v.IsObject() ? v.Get("error").GetString() : "HTTP " + std::to_string(reply.status));
    }
    return R::Succeed(reply.body);
}

Result<Me> AccountClient::RequestDeletion(const std::string& token, const std::string& password) const {
    JSONValue body = JSONValue::MakeObject();
    body.Set("password", JSONValue(password));
    return MeFrom(Ask("POST", "/v1/me/delete", token, UltraCanvas::JSON::Serialize(body)));
}

Result<Me> AccountClient::CancelDeletion(const std::string& token) const {
    return MeFrom(Ask("POST", "/v1/me/delete/cancel", token, "{}"));
}

#undef ULTRASTORE_FORWARD_FAILURE

// ===========================================================================
// The licences, kept in step
// ===========================================================================
Result<AccountLicenceSync> SyncLicencesWithAccount(SqliteRegistry& registry, const AccountClient& client, const std::string& token,
                                                   const std::string& licenceKeyHex) {
    using R = Result<AccountLicenceSync>;
    AccountLicenceSync sync;
    const auto local = registry.Licences();
    if (!local) return R::Failure(local.error.code, local.error.message, local.error.detail);
    const auto remote = client.Licences(token);
    if (!remote) return R::Failure(remote.error.code, remote.error.message, remote.error.detail);

    // Up: what this machine bought, with a seat for it.
    for (const LicenceRecord& record : local.Value()) {
        const auto attached = client.AttachLicence(token, record.token);
        if (!attached) { sync.failed.emplace_back(record.app, attached.error); continue; }
        const bool known = std::any_of(remote.Value().begin(), remote.Value().end(), [&](const AccountLicence& l) { return l.id == attached.Value().id; });
        if (!known) sync.attached.push_back(record.app);
        const auto seat = client.InstallLicence(token, attached.Value().id);
        if (!seat && seat.error.detail == "no_seat") sync.noSeat.push_back(record.app);
        else if (!seat) sync.failed.emplace_back(record.app, seat.error);
    }
    // Down: what the account has and this machine lacks.
    for (const AccountLicence& l : remote.Value()) {
        if (registry.GetLicence(l.app)) continue;
        if (licenceKeyHex.empty()) continue;
        const auto verified = VerifyLicence(l.token, licenceKeyHex);
        if (!verified) { sync.failed.emplace_back(l.app, verified.error); continue; }
        const auto seat = client.InstallLicence(token, l.id);
        if (!seat) {
            if (seat.error.detail == "no_seat") sync.noSeat.push_back(l.app);
            else sync.failed.emplace_back(l.app, seat.error);
            continue;
        }
        LicenceRecord record;
        record.app = l.app;
        record.licenceId = l.id;
        record.orderId = l.orderId;
        record.token = l.token;
        record.issuedAt = l.issued;
        if (const auto put = registry.PutLicence(record); !put) { sync.failed.emplace_back(l.app, put.error); continue; }
        sync.fetched.push_back(l.app);
    }
    return R::Succeed(std::move(sync));
}

} // namespace UltraStore
