// Apps/UltraStore/core/Purchase.cpp
// Buying through the payment service (Purchase.h).
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "Purchase.h"

#include "Verifier.h"

#include "DataFormats/UltraCanvasJSON.h"
#include "UltraNet/UltraNetCore.h"
#include "UltraNet/UltraNetHttp.h"

#include <cctype>
#include <cstdint>

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
    if (!reply.error.empty() || reply.status == 0) return R::Failure(ErrorCode::NetworkError, "The payment service could not be reached", reply.error);
    UltraCanvas::JSONParseResult parse;
    const UltraCanvas::JSONValue v = UltraCanvas::JSON::Parse(reply.body, &parse);
    if (reply.status < 200 || reply.status >= 300) {
        const std::string description = parse.success && v.IsObject() ? v.Get("error_description").GetString() : std::string();
        return R::Failure(reply.status == 404 ? ErrorCode::NotFound : ErrorCode::NetworkError,
                          description.empty() ? what + ": the payment service answered HTTP " + std::to_string(reply.status) : description,
                          "HTTP " + std::to_string(reply.status));
    }
    if (!parse.success || !v.IsObject()) return R::Failure(ErrorCode::ParseError, what + ": the payment service's answer is not JSON", parse.errorMessage);
    return R::Succeed(v);
}

Order OrderOf(const UltraCanvas::JSONValue& v) {
    Order o;
    o.orderId = v.Get("order_id").GetString();
    o.secret = v.Get("secret").GetString();
    o.checkoutUrl = v.Get("checkout_url").GetString();
    o.app = v.Get("app").GetString();
    o.appName = v.Get("app_name").GetString();
    o.amount = v.Get("amount").GetString();
    o.currency = v.Get("currency").GetString();
    o.listPrice = v.Get("list_price").GetString();
    o.code = v.Get("code").GetString();
    o.codeApplied = v.Get("code_applied").GetBoolean(false);
    o.status = v.Get("status").GetString();
    o.paidAt = v.Get("paid_at").GetString();
    o.licenceId = v.Get("licence_id").GetString();
    o.licence = v.Get("licence").GetString();
    o.receiptNo = static_cast<int>(v.Get("receipt_no").GetInteger(0));
    return o;
}

// RFC 4648 base64url without padding.
bool Base64UrlDecode(const std::string& text, std::vector<uint8_t>& out) {
    out.clear();
    uint32_t buffer = 0;
    int bits = 0;
    for (char c : text) {
        int value;
        if (c >= 'A' && c <= 'Z') value = c - 'A';
        else if (c >= 'a' && c <= 'z') value = c - 'a' + 26;
        else if (c >= '0' && c <= '9') value = c - '0' + 52;
        else if (c == '-') value = 62;
        else if (c == '_') value = 63;
        else if (c == '=') break;
        else return false;
        buffer = (buffer << 6) | static_cast<uint32_t>(value);
        bits += 6;
        if (bits >= 8) { bits -= 8; out.push_back(static_cast<uint8_t>((buffer >> bits) & 0xFF)); }
    }
    return true;
}

} // namespace

HttpCall UltraNetHttpCall() {
    return [](const std::string& method, const std::string& url, const std::string& jsonBody) {
        HttpReply out;
        if (!UltraNet_IsInitialized()) UltraNet_Initialize();
        UltraNetHttpRequest request;
        request.url = url;
        request.method = method == "POST" ? UltraNetHttpMethod::Post : UltraNetHttpMethod::Get;
        request.options.httpsOnlyRedirects = true;
        request.options.maxReceiveSize = int64_t(1) << 20;
        if (method == "POST") {
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

Result<Order> StartCheckout(const std::string& paymentUrl, const std::string& appId, const std::string& code, const HttpCall& http) {
    using R = Result<Order>;
    if (paymentUrl.empty()) return R::Failure(ErrorCode::NotAvailable, "The catalog names no payment service");
    UltraCanvas::JSONValue body = UltraCanvas::JSONValue::MakeObject();
    body.Set("app", appId);
    if (!code.empty()) body.Set("code", code);
    const auto answer = Answer(http("POST", TrimSlash(paymentUrl) + "/v1/checkout", UltraCanvas::JSON::Serialize(body)), "checkout");
    if (!answer) return R::Failure(answer.error.code, answer.error.message, answer.error.detail);
    Order order = OrderOf(answer.Value());
    if (order.orderId.empty() || order.secret.empty() || order.checkoutUrl.empty()) {
        return R::Failure(ErrorCode::ParseError, "The payment service's order lacks its id, secret or checkout URL");
    }
    return R::Succeed(std::move(order));
}

Result<Order> OrderStatus(const std::string& paymentUrl, const std::string& orderId, const std::string& secret, const HttpCall& http) {
    using R = Result<Order>;
    const auto answer = Answer(http("GET", TrimSlash(paymentUrl) + "/v1/orders/" + Encode(orderId) + "?secret=" + Encode(secret), ""), "order");
    if (!answer) return R::Failure(answer.error.code, answer.error.message, answer.error.detail);
    Order order = OrderOf(answer.Value());
    order.secret = secret;
    return R::Succeed(std::move(order));
}

Result<std::string> SignedDownloadUrl(const std::string& paymentUrl, const std::string& orderId, const std::string& secret,
                                      const std::string& file, const HttpCall& http) {
    using R = Result<std::string>;
    const auto answer = Answer(http("POST", TrimSlash(paymentUrl) + "/v1/orders/" + Encode(orderId) + "/download?secret=" + Encode(secret) +
                                    "&file=" + Encode(file), "{}"), "download");
    if (!answer) return R::Failure(answer.error.code, answer.error.message, answer.error.detail);
    const std::string url = answer.Value().Get("url").GetString();
    if (url.empty()) return R::Failure(ErrorCode::ParseError, "The payment service gave no download URL");
    return R::Succeed(url);
}

std::string RestrictedFileOf(const std::string& packageUrl) {
    const size_t at = packageUrl.find("/restricted/");
    if (at == std::string::npos) return {};
    std::string file = packageUrl.substr(at + 1);
    if (const size_t q = file.find('?'); q != std::string::npos) file = file.substr(0, q);
    return file;
}

std::function<std::string(const Package&, const Component&)>
LicensedUrlSigner(SqliteRegistry& registry, std::string paymentUrl, HttpCall http) {
    SqliteRegistry* reg = &registry;
    return [reg, paymentUrl, http](const Package& package, const Component& app) -> std::string {
        if (paymentUrl.empty()) return {};
        const auto licence = reg->GetLicence(app.id);
        if (!licence) return {};
        const auto url = SignedDownloadUrl(paymentUrl, licence.Value().orderId, licence.Value().orderSecret, RestrictedFileOf(package.url), http);
        return url ? url.Value() : std::string();
    };
}

Result<Licence> VerifyLicence(const std::string& token, const std::string& publicKeyHex) {
    using R = Result<Licence>;
    const std::string prefix = "ULIC1.";
    if (token.rfind(prefix, 0) != 0) return R::Failure(ErrorCode::ParseError, "Not a licence");
    const size_t dot = token.find('.', prefix.size());
    if (dot == std::string::npos) return R::Failure(ErrorCode::ParseError, "Not a licence");
    std::vector<uint8_t> payload, signature, key;
    if (!Base64UrlDecode(token.substr(prefix.size(), dot - prefix.size()), payload) || !Base64UrlDecode(token.substr(dot + 1), signature)) {
        return R::Failure(ErrorCode::ParseError, "The licence is not base64url");
    }
    if (!FromHex(publicKeyHex, key) || key.size() != 32) return R::Failure(ErrorCode::InvalidArgument, "The licence key is not a 32-byte hex public key");
    const std::string bytes(payload.begin(), payload.end());
    if (const auto ok = VerifyCatalogSignature(bytes, signature, {key}); !ok) {
        return R::Failure(ErrorCode::SignatureInvalid, "The licence is not signed by the store's licence key", ok.error.detail);
    }
    UltraCanvas::JSONParseResult parse;
    const UltraCanvas::JSONValue v = UltraCanvas::JSON::Parse(bytes, &parse);
    if (!parse.success || !v.IsObject()) return R::Failure(ErrorCode::ParseError, "The licence's payload is not JSON");
    Licence l;
    l.licenceId = v.Get("licence").GetString();
    l.app = v.Get("app").GetString();
    l.orderId = v.Get("order").GetString();
    l.issued = v.Get("issued").GetString();
    l.updates = v.Get("updates").GetString();
    l.updatesUntil = v.Get("updates_until").GetString();
    if (l.licenceId.empty() || l.app.empty()) return R::Failure(ErrorCode::ParseError, "The licence names no app");
    return R::Succeed(std::move(l));
}

LicenceRecord LicenceRecordOf(const Order& order, const Licence& licence) {
    LicenceRecord r;
    r.app = licence.app;
    r.licenceId = licence.licenceId;
    r.orderId = order.orderId;
    r.orderSecret = order.secret;
    r.token = order.licence;
    r.issuedAt = licence.issued;
    r.amount = order.amount;
    r.currency = order.currency;
    r.receiptNo = order.receiptNo;
    return r;
}

} // namespace UltraStore
