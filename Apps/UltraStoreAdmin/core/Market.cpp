// Apps/UltraStoreAdmin/core/Market.cpp
// Version: 0.1.1 - the HTTP call speaks PUT (the operator's client, Apps/UltraStoreOperator, shares it)
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "Market.h"

#include "Catalog.h"
#include "KeyVault.h"
#include "Verifier.h"

#include "UltraNet/UltraNetCore.h"
#include "UltraNet/UltraNetHttp.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <charconv>
#include <cstring>

namespace UltraStoreAdmin {

using UltraCanvas::JSONValue;

// ===========================================================================
// Canonical JSON
// ===========================================================================
namespace {

void AppendString(std::string& out, const std::string& s) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);   // UTF-8 as it is: the service writes ensure_ascii=False
                }
        }
    }
    out += '"';
}

// The shortest decimal that reads back as the same double, written as
// Python's repr writes it: plain digits for exponents from -4 to 15, else
// d.ddde+XX (the component JSON carries no floats today; this keeps the
// form right if one ever does).
std::string ShortestDouble(double v) {
    // to_chars: the shortest digits that read back as v, locale-free, as d.ddde+XX.
    char buf[40];
    const auto r = std::to_chars(buf, buf + sizeof buf, v, std::chars_format::scientific);
    std::string text(buf, r.ptr);
    const bool negative = !text.empty() && text[0] == '-';
    if (negative) text.erase(0, 1);
    const size_t e = text.find('e');
    int exponent = 0;
    const char* expStart = text.c_str() + e + 1;
    if (*expStart == '+') ++expStart;   // from_chars takes no plus sign
    std::from_chars(expStart, text.c_str() + text.size(), exponent);
    std::string mantissa;
    for (size_t i = 0; i < e; ++i) if (text[i] != '.') mantissa += text[i];
    while (mantissa.size() > 1 && mantissa.back() == '0') mantissa.pop_back();
    std::string out = negative ? "-" : "";
    if (exponent >= -4 && exponent < 16) {
        if (exponent < 0) {
            out += "0." + std::string(static_cast<size_t>(-exponent - 1), '0') + mantissa;
        } else if (static_cast<size_t>(exponent) + 1 >= mantissa.size()) {
            out += mantissa + std::string(static_cast<size_t>(exponent) + 1 - mantissa.size(), '0') + ".0";
        } else {
            out += mantissa.substr(0, static_cast<size_t>(exponent) + 1) + "." + mantissa.substr(static_cast<size_t>(exponent) + 1);
        }
        return out;
    }
    out += mantissa.substr(0, 1);
    if (mantissa.size() > 1) out += "." + mantissa.substr(1);
    std::snprintf(buf, sizeof buf, "e%c%02d", exponent < 0 ? '-' : '+', exponent < 0 ? -exponent : exponent);   // locale-ok: an exponent, digits only
    return out + buf;
}

void Write(std::string& out, const JSONValue& v) {
    if (v.IsNull()) { out += "null"; return; }
    if (v.IsBoolean()) { out += v.GetBoolean() ? "true" : "false"; return; }
    if (v.IsInteger()) { out += std::to_string(v.GetInteger()); return; }
    if (v.IsNumber()) { out += ShortestDouble(v.GetNumber()); return; }
    if (v.IsString()) { AppendString(out, v.GetString()); return; }
    if (v.IsArray()) {
        out += '[';
        bool first = true;
        for (const JSONValue& e : v.GetElements()) {
            if (!first) out += ',';
            first = false;
            Write(out, e);
        }
        out += ']';
        return;
    }
    // An object: the members by key, byte order, as sort_keys gives.
    std::vector<const JSONValue::Member*> members;
    for (const JSONValue::Member& m : v.GetMembers()) members.push_back(&m);
    std::sort(members.begin(), members.end(), [](const JSONValue::Member* a, const JSONValue::Member* b) { return a->first < b->first; });
    out += '{';
    bool first = true;
    for (const JSONValue::Member* m : members) {
        if (!first) out += ',';
        first = false;
        AppendString(out, m->first);
        out += ':';
        Write(out, m->second);
    }
    out += '}';
}

std::string TrimSlash(std::string s) {
    while (!s.empty() && s.back() == '/') s.pop_back();
    return s;
}

std::string NowStamp() {
    return std::to_string(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
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

} // namespace

std::string CanonicalJson(const JSONValue& value) {
    std::string out;
    Write(out, value);
    return out;
}

std::string SignRequest(const std::vector<uint8_t>& seed, const std::string& merchantId, const std::string& method,
                        const std::string& path, const std::string& body, const std::string& timestamp) {
    const std::string stamp = timestamp.empty() ? NowStamp() : timestamp;
    std::string upper = method;
    std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    const std::string signed_ = upper + " " + path + "\n" + stamp + "\n" + body;
    const auto signature = UltraStore::SignCatalog(signed_, seed);
    if (!signature) return {};
    return "Merchant " + merchantId + ":" + stamp + ":" + UltraStore::ToHex(signature.Value());
}

// ===========================================================================
// The HTTP call
// ===========================================================================
MarketCall UltraNetMarketCall() {
    return [](const std::string& method, const std::string& url, const std::string& authorization, const std::string& jsonBody) {
        MarketReply out;
        if (!UltraNet_IsInitialized()) UltraNet_Initialize();
        UltraNetHttpRequest request;
        request.url = url;
        request.method = method == "POST" ? UltraNetHttpMethod::Post : method == "PUT" ? UltraNetHttpMethod::Put : UltraNetHttpMethod::Get;
        request.options.httpsOnlyRedirects = true;
        request.options.maxReceiveSize = int64_t(8) << 20;
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

Result<std::vector<uint8_t>> OpenKeySeed(const AdminDatabase& db, int64_t keyId, const std::string& passphrase) {
    using R = Result<std::vector<uint8_t>>;
    if (keyId == 0) {
        const std::string named = db.Setting("market.key");
        if (!named.empty()) keyId = std::strtoll(named.c_str(), nullptr, 10);   // locale-ok: digits only
    }
    if (keyId == 0) {
        const auto keys = db.Keys();
        if (!keys) return R::Failure(keys.error.code, keys.error.message, keys.error.detail);
        for (const KeyRecord& k : keys.Value()) if (k.Active()) { keyId = k.id; break; }
        if (keyId == 0) return R::Failure(ErrorCode::NotFound, "No signing key: `key new <name>` makes one");
    }
    const auto key = db.GetKey(keyId);
    if (!key) return R::Failure(key.error.code, key.error.message, key.error.detail);
    if (!key.Value().Active()) return R::Failure(ErrorCode::InvalidArgument, "Key " + std::to_string(keyId) + " (" + key.Value().name + ") is retired");
    const auto sealedBytes = db.SealedSeed(keyId);
    if (!sealedBytes) return R::Failure(sealedBytes.error.code, sealedBytes.error.message, sealedBytes.error.detail);
    SealedSeed sealed;
    if (!SealedSeed::Parse(sealedBytes.Value(), sealed)) return R::Failure(ErrorCode::ParseError, "The key's sealed seed is malformed");
    return OpenSeed(sealed, passphrase);
}

MarketSettings MarketSettings::From(const AdminDatabase& db) {
    MarketSettings s;
    s.url = TrimSlash(db.Setting("market.url"));
    s.merchantId = db.Setting("market.merchant_id");
    return s;
}

// ===========================================================================
// The client
// ===========================================================================
MarketClient::MarketClient(MarketSettings settings, std::vector<uint8_t> seed, MarketCall call)
    : settings_(std::move(settings)), seed_(std::move(seed)), call_(std::move(call)) {
    settings_.url = TrimSlash(settings_.url);
}

Result<JSONValue> MarketClient::Ask(const std::string& method, const std::string& path, const std::string& jsonBody) const {
    using R = Result<JSONValue>;
    if (settings_.url.empty()) return R::Failure(ErrorCode::InvalidArgument, "No marketplace service URL (the setting market.url)");
    if (settings_.merchantId.empty()) return R::Failure(ErrorCode::InvalidArgument, "No merchant id (the setting market.merchant_id, from the operator)");
    if (seed_.size() != 32) return R::Failure(ErrorCode::InvalidArgument, "The merchant's key is needed to sign the request");
    const std::string authorization = SignRequest(seed_, settings_.merchantId, method, path, jsonBody);
    if (authorization.empty()) return R::Failure(ErrorCode::CryptoUnavailable, "The request could not be signed");
    const MarketReply reply = call_(method, settings_.url + path, authorization, jsonBody);
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

namespace {

Standing StandingOf(const JSONValue& v) {
    Standing s;
    s.id = v.Get("id").GetString();
    s.name = v.Get("name").GetString();
    s.standing = v.Get("standing").GetString();
    s.strikes = static_cast<int>(v.Get("strikes").GetInteger(0));
    s.termsInForce = static_cast<int>(v.Get("terms").Get("in_force").GetInteger(0));
    s.termsAccepted = static_cast<int>(v.Get("terms").Get("accepted").GetInteger(0));
    return s;
}

} // namespace

Result<Standing> MarketClient::GetStanding() const {
    const auto v = Ask("GET", "/v1/merchants/me");
    if (!v) return Result<Standing>::Failure(v.error.code, v.error.message, v.error.detail);
    return Result<Standing>::Succeed(StandingOf(v.Value()));
}

JSONValue SubmissionPackages(const UltraStore::Component& component) {
    JSONValue packages = JSONValue::MakeArray();
    for (const UltraStore::Release& release : component.releases) {
        for (const UltraStore::Package& p : release.packages) {
            JSONValue o = JSONValue::MakeObject();
            // The path under the packages server when the URL has one
            // (packages/<sha>.<format>, restricted/<sha>.<format>), else the URL.
            const size_t at = std::min(p.url.find("/packages/"), p.url.find("/restricted/"));
            if (at != std::string::npos) o.Set("file", p.url.substr(at + 1));
            else o.Set("url", p.url);
            o.Set("sha256", p.sha256);
            o.Set("size", static_cast<int64_t>(p.size));
            o.Set("version", release.version);
            packages.Append(std::move(o));
        }
    }
    return packages;
}

Result<SubmissionReceipt> MarketClient::Submit(const UltraStore::Component& component, const std::string& answers) const {
    using R = Result<SubmissionReceipt>;
    if (seed_.size() != 32) return R::Failure(ErrorCode::InvalidArgument, "The merchant's key is needed to sign the submission");
    UltraCanvas::JSONParseResult parse;
    const JSONValue componentJson = UltraCanvas::JSON::Parse(UltraStore::SerializeComponent(component), &parse);
    if (!parse.success || !componentJson.IsObject()) return R::Failure(ErrorCode::ParseError, "The component could not be serialized", parse.errorMessage);
    const auto signature = UltraStore::SignCatalog(CanonicalJson(componentJson), seed_);
    if (!signature) return R::Failure(signature.error.code, signature.error.message, signature.error.detail);
    JSONValue body = JSONValue::MakeObject();
    body.Set("component", componentJson);
    body.Set("signature", UltraStore::ToHex(signature.Value()));
    body.Set("packages", SubmissionPackages(component));
    if (!answers.empty()) body.Set("answers", answers);
    const auto v = Ask("POST", "/v1/submissions", UltraCanvas::JSON::Serialize(body));
    if (!v) return R::Failure(v.error.code, v.error.message, v.error.detail);
    SubmissionReceipt receipt;
    receipt.id = v.Value().Get("id").GetString();
    receipt.app = v.Value().Get("app").GetString();
    receipt.version = v.Value().Get("version").GetString();
    receipt.state = v.Value().Get("state").GetString();
    for (const JSONValue::Member& m : v.Value().Get("checks").GetMembers()) receipt.checks.emplace_back(m.first, m.second.GetString());
    return R::Succeed(std::move(receipt));
}

Result<JSONValue> MarketClient::Submissions() const {
    return Ask("GET", "/v1/merchants/me/submissions");
}

Result<void> MarketClient::Withdraw(const std::string& submissionId) const {
    const auto v = Ask("POST", "/v1/submissions/" + Encode(submissionId) + "/withdraw", "{}");
    if (!v) return Result<void>::Failure(v.error.code, v.error.message, v.error.detail);
    return Result<void>::Succeed();
}

Result<std::vector<Notice>> MarketClient::Notices() const {
    using R = Result<std::vector<Notice>>;
    const auto v = Ask("GET", "/v1/merchants/me/notices");
    if (!v) return R::Failure(v.error.code, v.error.message, v.error.detail);
    std::vector<Notice> out;
    for (const JSONValue& n : v.Value().Get("notices").GetElements()) {
        Notice notice;
        notice.id = n.Get("id").GetString();
        notice.kind = n.Get("kind").GetString();
        notice.subject = n.Get("subject").GetString();
        notice.text = n.Get("text").GetString();
        notice.clause = n.Get("clause").GetString();
        notice.createdAt = n.Get("created_at").GetString();
        notice.deadline = n.Get("deadline").GetString();
        notice.acknowledgedAt = n.Get("acknowledged_at").GetString();
        out.push_back(std::move(notice));
    }
    return R::Succeed(std::move(out));
}

Result<std::string> MarketClient::Acknowledge(const std::string& noticeId) const {
    const auto v = Ask("POST", "/v1/notices/" + Encode(noticeId) + "/ack", "{}");
    if (!v) return Result<std::string>::Failure(v.error.code, v.error.message, v.error.detail);
    return Result<std::string>::Succeed(v.Value().Get("acknowledged_at").GetString());
}

Result<Terms> MarketClient::CurrentTerms() const {
    using R = Result<Terms>;
    const auto v = Ask("GET", "/v1/terms/current");
    if (!v) return R::Failure(v.error.code, v.error.message, v.error.detail);
    Terms t;
    t.version = static_cast<int>(v.Value().Get("version").GetInteger(0));
    t.text = v.Value().Get("text").GetString();
    t.changes = v.Value().Get("changes").GetString();
    t.inForceFrom = v.Value().Get("in_force_from").GetString();
    return R::Succeed(std::move(t));
}

Result<Standing> MarketClient::AcceptTerms(int version, const std::string& acceptedBy) const {
    JSONValue body = JSONValue::MakeObject();
    body.Set("version", static_cast<int64_t>(version));
    body.Set("accepted_by", acceptedBy);
    const auto v = Ask("POST", "/v1/terms/accept", UltraCanvas::JSON::Serialize(body));
    if (!v) return Result<Standing>::Failure(v.error.code, v.error.message, v.error.detail);
    return GetStanding();
}

Result<JSONValue> MarketClient::Reports() const {
    return Ask("GET", "/v1/merchants/me/reports");
}

} // namespace UltraStoreAdmin
