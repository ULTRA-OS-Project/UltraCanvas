// Apps/UltraAccount/core/UltraAccountClient.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraAccountClient.h"

#include "DataFormats/UltraCanvasJSON.h"
#include "UltraCanvasDataFolder.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include "UltraNet/UltraNetCore.h"
#include "UltraNet/UltraNetHttp.h"

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

namespace UltraAccount {

namespace {

std::string TrimSlash(std::string s) {
    while (!s.empty() && s.back() == '/') s.pop_back();
    return s;
}

// A path segment: the ids the service hands out are letters, digits and
// dashes, but one read from a vault file is untrusted.
std::string Segment(const std::string& text) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : text) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += static_cast<char>(c);
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
}

std::string Serialize(const JSONValue& v) {
    return UltraCanvas::JSON::Serialize(v, UltraCanvas::JSONSerializeOptions());
}

JSONValue Object(std::initializer_list<std::pair<const char*, std::string>> fields) {
    JSONValue v = JSONValue::MakeObject();
    for (const auto& field : fields) v.Set(field.first, JSONValue(field.second));
    return v;
}

JSONValue DeviceJson(const DeviceInfo& device) {
    return Object({{"name", device.name}, {"os", device.os}, {"client", device.client}});
}

std::optional<JSONValue> ParseObject(const std::string& body) {
    UltraCanvas::JSONParseResult parse;
    JSONValue v = UltraCanvas::JSON::Parse(body, &parse);
    if (!parse.success || !v.IsObject()) return std::nullopt;
    return v;
}

Error ParseError() {
    return Error{"parse", Tr("The account service's answer could not be read"), 0};
}

Plan PlanOf(const JSONValue& v) {
    Plan p;
    if (!v.IsObject()) return p;
    p.plan = v.Get("plan").GetString();
    if (p.plan.empty()) p.plan = "free";
    p.paid = v.Get("paid").GetBoolean(false);
    p.validUntil = v.Get("valid_until").GetString();
    p.source = v.Get("source").GetString();
    p.sms = v.Get("features").Get("sms").GetBoolean(false);
    return p;
}

Profile ProfileOf(const JSONValue& v) {
    Profile p;
    p.id = v.Get("id").GetString();
    p.email = v.Get("email").GetString();
    p.displayName = v.Get("display_name").GetString();
    p.phone = v.Get("phone").GetString();
    p.state = v.Get("state").GetString();
    p.createdAt = v.Get("created_at").GetString();
    p.emailVerified = v.Get("email_verified").GetBoolean(false);
    p.phoneVerified = v.Get("phone_verified").GetBoolean(false);
    p.twoFactor = v.Get("two_factor").GetBoolean(false);
    return p;
}

Me MeOf(const JSONValue& v) {
    Me me;
    me.profile = ProfileOf(v);
    me.plan = PlanOf(v.Get("subscription"));
    me.recoveryKeys = static_cast<int>(v.Get("recovery_keys").GetInteger(0));
    me.devices = v.Get("devices").IsArray() ? static_cast<int>(v.Get("devices").GetElements().size()) : 0;
    me.unreadNotices = static_cast<int>(v.Get("unread_notices").GetInteger(0));
    me.deviceId = v.Get("device").Get("id").GetString();
    return me;
}

RecoveryKey RecoveryKeyOf(const JSONValue& v) {
    RecoveryKey k;
    k.id = v.Get("id").GetString();
    k.app = v.Get("app").GetString();
    k.label = v.Get("label").GetString();
    k.createdAt = v.Get("created_at").GetString();
    k.releasedAt = v.Get("released_at").GetString();
    k.releases = static_cast<int>(v.Get("releases").GetInteger(0));
    return k;
}

std::vector<Channel> ChannelsOf(const JSONValue& v) {
    std::vector<Channel> out;
    if (!v.IsArray()) return out;
    for (const JSONValue& c : v.GetElements()) out.push_back(Channel{c.Get("channel").GetString(), c.Get("to").GetString()});
    return out;
}

std::string ReadWhole(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

template <typename T, typename F>
Result<T> Map(const Result<std::string>& body, F&& read) {
    if (!body) return Result<T>::Fail(body.error);
    std::optional<JSONValue> v = ParseObject(body.Value());
    if (!v) return Result<T>::Fail(ParseError());
    return Result<T>::Succeed(read(*v));
}

Result<Done> Nothing(const Result<std::string>& body) {
    if (!body) return Result<Done>::Fail(body.error);
    return Result<Done>::Succeed(Done{});
}

} // namespace

// ===========================================================================
// The HTTP call, the machine
// ===========================================================================
HttpCall UltraNetCall() {
    return [](const std::string& method, const std::string& url, const std::string& authorization, const std::string& jsonBody) {
        HttpReply out;
        if (!UltraNet_IsInitialized()) UltraNet_Initialize();
        UltraNetHttpRequest request;
        request.url = url;
        request.method = method == "POST" ? UltraNetHttpMethod::Post : method == "PUT" ? UltraNetHttpMethod::Put
                       : method == "DELETE" ? UltraNetHttpMethod::Delete : UltraNetHttpMethod::Get;
        request.options.httpsOnlyRedirects = true;
        request.options.maxReceiveSize = int64_t(1) << 20;
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

std::string CurrentOs() {
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#elif defined(__linux__)
    return "linux";
#else
    return "unix";
#endif
}

// ===========================================================================
// The session on disk
// ===========================================================================
std::string Session::Path() {
    return UltraCanvas::PathToUtf8(UltraCanvas::UltraCanvasDataFolder(kDataFolderName) / "session.json");
}

Session Session::Load(const std::string& path) {
    Session s;
    std::optional<JSONValue> v = ParseObject(ReadWhole(UltraCanvas::PathFromUtf8(path)));
    if (!v) return s;
    s.url = TrimSlash(v->Get("url").GetString());
    s.token = v->Get("token").GetString();
    s.userId = v->Get("user_id").GetString();
    s.email = v->Get("email").GetString();
    s.deviceId = v->Get("device_id").GetString();
    return s;
}

bool Session::Save(const std::string& path) const {
    const fs::path file = UltraCanvas::PathFromUtf8(path);
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    const JSONValue v = Object({{"url", TrimSlash(url)}, {"token", token}, {"user_id", userId}, {"email", email}, {"device_id", deviceId}});
    const fs::path temporary = UltraCanvas::PathFromUtf8(path + ".tmp");
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        // Owner-only before the token is in it.
        fs::permissions(temporary, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace, ec);
        const std::string text = Serialize(v) + "\n";
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!out) return false;
    }
    fs::rename(temporary, file, ec);
    return !ec;
}

void Session::Clear(const std::string& path) {
    std::error_code ec;
    fs::remove(UltraCanvas::PathFromUtf8(path), ec);
}

// ===========================================================================
// The client
// ===========================================================================
Client::Client(std::string serviceUrl, HttpCall call) : url_(TrimSlash(std::move(serviceUrl))), call_(std::move(call)) {}

Result<std::string> Client::Ask(const std::string& method, const std::string& path, const std::string& token,
                                const std::string& jsonBody) const {
    using R = Result<std::string>;
    if (url_.empty()) return R::Fail(Error{"network", Tr("No account service is set"), 0});
    if (url_.rfind("https://", 0) != 0 && url_.rfind("http://127.0.0.1", 0) != 0 && url_.rfind("http://localhost", 0) != 0) {
        // The password and the session token travel in these requests.
        return R::Fail(Error{"network", Tr("The account service must be an https:// address"), 0});
    }
    const HttpReply reply = call_(method, url_ + path, token.empty() ? std::string() : "Bearer " + token, jsonBody);
    if (!reply.error.empty() || reply.status == 0) {
        return R::Fail(Error{"network", Tr("The account service could not be reached"), reply.status});
    }
    if (reply.status < 200 || reply.status >= 300) {
        std::optional<JSONValue> v = ParseObject(reply.body);
        Error e{v ? v->Get("error").GetString() : std::string(), v ? v->Get("error_description").GetString() : std::string(), reply.status};
        if (e.code.empty()) e.code = "http_" + std::to_string(reply.status);
        if (e.message.empty()) e.message = TrF("The account service answered HTTP {0}", reply.status);
        return R::Fail(e);
    }
    return R::Succeed(reply.body);
}

Result<Done> Client::SignUp(const std::string& email, const std::string& password, const std::string& country,
                            const std::string& language) const {
    return Nothing(Ask("POST", "/v1/signup", {}, Serialize(Object({{"email", email}, {"password", password}, {"country", country},
                                                                    {"language", language}}))));
}

static Session SessionOf(const std::string& url, const JSONValue& v) {
    Session s;
    s.url = url;
    s.token = v.Get("token").GetString();
    s.userId = v.Get("user").Get("id").GetString();
    s.email = v.Get("user").Get("email").GetString();
    s.deviceId = v.Get("device").Get("id").GetString();
    return s;
}

Result<Session> Client::VerifySignUp(const std::string& email, const std::string& code, const DeviceInfo& device) const {
    JSONValue body = Object({{"email", email}, {"code", code}});
    body.Set("device", DeviceJson(device));
    return Map<Session>(Ask("POST", "/v1/signup/verify", {}, Serialize(body)), [this](const JSONValue& v) { return SessionOf(url_, v); });
}

Result<Done> Client::ResendCode(const std::string& email) const {
    return Nothing(Ask("POST", "/v1/signup/resend", {}, Serialize(Object({{"email", email}}))));
}

Result<Session> Client::SignIn(const std::string& email, const std::string& password, const DeviceInfo& device,
                               const std::string& totp) const {
    JSONValue body = Object({{"email", email}, {"password", password}, {"totp", totp}});
    body.Set("device", DeviceJson(device));
    return Map<Session>(Ask("POST", "/v1/signin", {}, Serialize(body)), [this](const JSONValue& v) { return SessionOf(url_, v); });
}

Result<Done> Client::ForgotPassword(const std::string& email) const {
    return Nothing(Ask("POST", "/v1/password/forgot", {}, Serialize(Object({{"email", email}}))));
}

Result<Done> Client::ResetPassword(const std::string& email, const std::string& code, const std::string& newPassword,
                                   const std::string& totp) const {
    return Nothing(Ask("POST", "/v1/password/reset", {},
                       Serialize(Object({{"email", email}, {"code", code}, {"new_password", newPassword}, {"totp", totp}}))));
}

Result<Me> Client::GetMe(const std::string& token) const {
    return Map<Me>(Ask("GET", "/v1/me", token, {}), MeOf);
}

Result<Done> Client::SignOut(const std::string& token) const {
    return Nothing(Ask("POST", "/v1/me/signout", token, "{}"));
}

Result<Plan> Client::GetPlan(const std::string& token) const {
    return Map<Plan>(Ask("GET", "/v1/me/subscription", token, {}), PlanOf);
}

Result<Plan> Client::Subscribe(const std::string& token, const std::string& licence) const {
    return Map<Plan>(Ask("POST", "/v1/me/subscription", token, Serialize(Object({{"licence", licence}}))), PlanOf);
}

Result<std::string> Client::AddPhone(const std::string& token, const std::string& phone, const std::string& password) const {
    return Map<std::string>(Ask("POST", "/v1/me/phone", token, Serialize(Object({{"phone", phone}, {"password", password}}))),
                            [](const JSONValue& v) { return v.Get("phone").GetString(); });
}

Result<Me> Client::VerifyPhone(const std::string& token, const std::string& code) const {
    return Map<Me>(Ask("POST", "/v1/me/phone/verify", token, Serialize(Object({{"code", code}}))), MeOf);
}

Result<Me> Client::RemovePhone(const std::string& token, const std::string& password) const {
    return Map<Me>(Ask("POST", "/v1/me/phone/remove", token, Serialize(Object({{"password", password}}))), MeOf);
}

Result<std::vector<RecoveryKey>> Client::RecoveryKeys(const std::string& token) const {
    return Map<std::vector<RecoveryKey>>(Ask("GET", "/v1/me/recovery-keys", token, {}), [](const JSONValue& v) {
        std::vector<RecoveryKey> out;
        if (v.Get("recovery_keys").IsArray()) {
            for (const JSONValue& k : v.Get("recovery_keys").GetElements()) out.push_back(RecoveryKeyOf(k));
        }
        return out;
    });
}

Result<RecoveryKey> Client::StoreRecoveryKey(const std::string& token, const std::string& app, const std::string& label,
                                             const std::string& secretHex) const {
    return Map<RecoveryKey>(Ask("POST", "/v1/me/recovery-keys", token,
                                Serialize(Object({{"app", app}, {"label", label}, {"secret", secretHex}}))),
                            [](const JSONValue& v) { return RecoveryKeyOf(v.Get("recovery_key")); });
}

Result<Done> Client::DeleteRecoveryKey(const std::string& token, const std::string& id) const {
    return Nothing(Ask("DELETE", "/v1/me/recovery-keys/" + Segment(id), token, {}));
}

Result<RecoveryKeyInfo> Client::GetRecoveryKeyInfo(const std::string& id) const {
    return Map<RecoveryKeyInfo>(Ask("GET", "/v1/recovery-keys/" + Segment(id), {}, {}), [](const JSONValue& v) {
        RecoveryKeyInfo info;
        info.id = v.Get("id").GetString();
        info.app = v.Get("app").GetString();
        info.label = v.Get("label").GetString();
        info.channels = ChannelsOf(v.Get("channels"));
        return info;
    });
}

Result<std::string> Client::SendRecoveryCode(const std::string& id, const std::string& channel) const {
    return Map<std::string>(Ask("POST", "/v1/recovery-keys/" + Segment(id) + "/code", {}, Serialize(Object({{"channel", channel}}))),
                            [](const JSONValue& v) { return v.Get("to").GetString(); });
}

Result<std::string> Client::ReleaseRecoveryKey(const std::string& id, const std::string& code) const {
    return Map<std::string>(Ask("POST", "/v1/recovery-keys/" + Segment(id) + "/release", {}, Serialize(Object({{"code", code}}))),
                            [](const JSONValue& v) { return v.Get("secret").GetString(); });
}

std::string Describe(const Error& error) {
    if (!error.message.empty()) return error.message;
    if (error.code == "network") return Tr("The account service could not be reached");
    return Tr("The account service refused the request");
}

} // namespace UltraAccount
