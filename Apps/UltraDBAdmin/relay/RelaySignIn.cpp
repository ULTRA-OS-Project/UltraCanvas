// Apps/UltraDBAdmin/relay/RelaySignIn.cpp
// Version: 0.1.1 - a P-256 key read from its DER form (OpenSSL 3.0.2)
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "RelaySignIn.h"

#include <UltraDatabase/UltraDatabase.h>
#include <UltraNet/UltraNetHttp.h>
#include <UltraNet/UltraNetOAuth2.h>

#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/param_build.h>
#include <openssl/rand.h>
#include <openssl/x509.h>

#include <yyjson.h>

#include <algorithm>
#include <cctype>
#include <ctime>
#include <sstream>

namespace UltraDBAdminRelay {
namespace {

// ===== JSON, through yyjson =====

struct Doc {
    yyjson_doc* doc = nullptr;
    explicit Doc(const std::string& text) : doc(yyjson_read(text.data(), text.size(), 0)) {}
    ~Doc() { yyjson_doc_free(doc); }
    Doc(const Doc&) = delete;
    Doc& operator=(const Doc&) = delete;
    yyjson_val* Root() const { return doc ? yyjson_doc_get_root(doc) : nullptr; }
};

std::string Str(yyjson_val* obj, const char* key) {
    yyjson_val* v = obj ? yyjson_obj_get(obj, key) : nullptr;
    return v && yyjson_is_str(v) ? std::string(yyjson_get_str(v)) : std::string();
}

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string HtmlEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else if (c == '&') out += "&amp;";
        else if (c == '"') out += "&quot;";
        else out += c;
    }
    return out;
}

std::string PercentEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += static_cast<char>(c);
        else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

// A page the browser shows when the sign-in cannot go on.
void Page(HttpReply& reply, int status, const char* reason, const std::string& what) {
    reply.status = status;
    reply.reason = reason;
    reply.headers["Cache-Control"] = "no-store";
    reply.body = "<!doctype html><meta charset=\"utf-8\"><title>UltraDBAdmin relay</title>"
                 "<body style=\"font-family:sans-serif;margin:3em\"><h1>Not signed in</h1><p>" +
                 HtmlEscape(what) + "</p></body>";
}

std::string RandomToken(size_t bytes) {
    std::string raw(bytes, '\0');
    if (RAND_bytes(reinterpret_cast<unsigned char*>(raw.data()), static_cast<int>(bytes)) != 1) return std::string();
    return Base64UrlEncode(raw);
}

UltraNetHttpOptions FetchOptions() {
    UltraNetHttpOptions options = UltraNetHttpOptions::Default();
    options.timeoutMs = 15000;
    options.connectTimeoutMs = 10000;
    return options;
}

bool Fetch(const std::string& url, std::string& body, std::string& error) {
    UltraNetResponse response;
    const UltraNetResult r = UltraNet_HttpGet(url, response, FetchOptions());
    if (!r || !response.IsSuccess() || !response.IsComplete()) {
        error = "cannot read " + url + ": " +
                (!r ? r.message : "HTTP " + std::to_string(response.statusCode) + " " + response.transferError);
        return false;
    }
    body = response.GetBodyAsString();
    return true;
}

bool Loopback(const std::string& url) {
    return url.rfind("http://", 0) == 0 && IsLoopback(UrlHost(url));
}

// ===== keys =====

struct KeyFree {
    void operator()(EVP_PKEY* k) const { EVP_PKEY_free(k); }
};
using Key = std::unique_ptr<EVP_PKEY, KeyFree>;

Key RsaKey(const std::string& n, const std::string& e) {
    BIGNUM* bn = BN_bin2bn(reinterpret_cast<const unsigned char*>(n.data()), static_cast<int>(n.size()), nullptr);
    BIGNUM* be = BN_bin2bn(reinterpret_cast<const unsigned char*>(e.data()), static_cast<int>(e.size()), nullptr);
    OSSL_PARAM_BLD* build = OSSL_PARAM_BLD_new();
    EVP_PKEY* key = nullptr;
    if (bn && be && build && OSSL_PARAM_BLD_push_BN(build, OSSL_PKEY_PARAM_RSA_N, bn) &&
        OSSL_PARAM_BLD_push_BN(build, OSSL_PKEY_PARAM_RSA_E, be)) {
        OSSL_PARAM* params = OSSL_PARAM_BLD_to_param(build);
        EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_from_name(nullptr, "RSA", nullptr);
        if (params && ctx && EVP_PKEY_fromdata_init(ctx) == 1)
            EVP_PKEY_fromdata(ctx, &key, EVP_PKEY_PUBLIC_KEY, params);
        EVP_PKEY_CTX_free(ctx);
        OSSL_PARAM_free(params);
    }
    OSSL_PARAM_BLD_free(build);
    BN_free(bn);
    BN_free(be);
    return Key(key);
}

Key P256Key(const std::string& x, const std::string& y) {
    if (x.size() != 32 || y.size() != 32) return nullptr;
    // The key as a SubjectPublicKeyInfo (RFC 5480): the fixed DER prefix for
    // an id-ecPublicKey on prime256v1, then the uncompressed point. Read with
    // d2i_PUBKEY, which every OpenSSL reads alike (OSSL_PARAM_BLD's EC import
    // fails on 3.0.2's).
    static const unsigned char prefix[] = { 0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2a, 0x86, 0x48, 0xce, 0x3d,
                                            0x02, 0x01, 0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01,
                                            0x07, 0x03, 0x42, 0x00, 0x04 };
    std::string der(reinterpret_cast<const char*>(prefix), sizeof prefix);
    der += x;
    der += y;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(der.data());
    return Key(d2i_PUBKEY(nullptr, &p, static_cast<long>(der.size())));
}

// JWS's ES256 signature is r || s, 32 bytes each; OpenSSL verifies DER.
std::string EcdsaDer(const std::string& raw) {
    if (raw.size() != 64) return std::string();
    ECDSA_SIG* sig = ECDSA_SIG_new();
    BIGNUM* r = BN_bin2bn(reinterpret_cast<const unsigned char*>(raw.data()), 32, nullptr);
    BIGNUM* s = BN_bin2bn(reinterpret_cast<const unsigned char*>(raw.data()) + 32, 32, nullptr);
    std::string der;
    if (sig && r && s && ECDSA_SIG_set0(sig, r, s) == 1) {
        r = s = nullptr;                    // the signature owns them now
        unsigned char* out = nullptr;
        const int size = i2d_ECDSA_SIG(sig, &out);
        if (size > 0) der.assign(reinterpret_cast<char*>(out), static_cast<size_t>(size));
        OPENSSL_free(out);
    }
    BN_free(r);
    BN_free(s);
    ECDSA_SIG_free(sig);
    return der;
}

} // namespace

// ===== base64url =====

std::string Base64UrlEncode(const std::string& bytes) {
    static const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    size_t i = 0;
    for (; i + 2 < bytes.size(); i += 3) {
        const uint32_t v = (static_cast<uint8_t>(bytes[i]) << 16) | (static_cast<uint8_t>(bytes[i + 1]) << 8) |
                           static_cast<uint8_t>(bytes[i + 2]);
        out += alphabet[(v >> 18) & 63];
        out += alphabet[(v >> 12) & 63];
        out += alphabet[(v >> 6) & 63];
        out += alphabet[v & 63];
    }
    if (i + 1 == bytes.size()) {
        const uint32_t v = static_cast<uint8_t>(bytes[i]) << 16;
        out += alphabet[(v >> 18) & 63];
        out += alphabet[(v >> 12) & 63];
    } else if (i + 2 == bytes.size()) {
        const uint32_t v = (static_cast<uint8_t>(bytes[i]) << 16) | (static_cast<uint8_t>(bytes[i + 1]) << 8);
        out += alphabet[(v >> 18) & 63];
        out += alphabet[(v >> 12) & 63];
        out += alphabet[(v >> 6) & 63];
    }
    return out;
}

bool Base64UrlDecode(const std::string& text, std::string& bytes) {
    bytes.clear();
    uint32_t v = 0;
    int bits = 0;
    for (char c : text) {
        int d;
        if (c >= 'A' && c <= 'Z') d = c - 'A';
        else if (c >= 'a' && c <= 'z') d = c - 'a' + 26;
        else if (c >= '0' && c <= '9') d = c - '0' + 52;
        else if (c == '-' || c == '+') d = 62;
        else if (c == '_' || c == '/') d = 63;
        else if (c == '=') break;
        else return false;
        v = (v << 6) | static_cast<uint32_t>(d);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            bytes += static_cast<char>((v >> bits) & 0xFF);
        }
    }
    return true;
}

// ===== PublicKeys =====

struct PublicKeys::Impl {
    struct One {
        std::string kid;
        std::string kty;        // RSA, EC
        Key key;
    };
    std::vector<One> keys;
};

PublicKeys::PublicKeys() : impl_(std::make_unique<Impl>()) {}
PublicKeys::~PublicKeys() = default;
PublicKeys::PublicKeys(PublicKeys&&) noexcept = default;
PublicKeys& PublicKeys::operator=(PublicKeys&&) noexcept = default;

bool PublicKeys::Parse(const std::string& jwks, std::string& error) {
    impl_->keys.clear();
    Doc doc(jwks);
    yyjson_val* list = doc.Root() ? yyjson_obj_get(doc.Root(), "keys") : nullptr;
    if (!list || !yyjson_is_arr(list)) {
        error = "not a key set (JWKS): no \"keys\"";
        return false;
    }
    size_t index, max;
    yyjson_val* jwk;
    yyjson_arr_foreach(list, index, max, jwk) {
        const std::string use = Str(jwk, "use");
        if (!use.empty() && use != "sig") continue;
        Impl::One one;
        one.kid = Str(jwk, "kid");
        one.kty = Str(jwk, "kty");
        std::string a, b;
        if (one.kty == "RSA" && Base64UrlDecode(Str(jwk, "n"), a) && Base64UrlDecode(Str(jwk, "e"), b) &&
            a.size() >= 256)            // 2048 bits at least
            one.key = RsaKey(a, b);
        else if (one.kty == "EC" && Str(jwk, "crv") == "P-256" && Base64UrlDecode(Str(jwk, "x"), a) &&
                 Base64UrlDecode(Str(jwk, "y"), b))
            one.key = P256Key(a, b);
        if (one.key) impl_->keys.push_back(std::move(one));
    }
    if (impl_->keys.empty()) {
        error = "the key set holds no RSA (2048 bits or more) or P-256 signing key";
        return false;
    }
    return true;
}

size_t PublicKeys::Size() const { return impl_->keys.size(); }

bool PublicKeys::Has(const std::string& kid) const {
    for (const auto& one : impl_->keys)
        if (one.kid == kid) return true;
    return kid.empty() && impl_->keys.size() == 1;
}

bool PublicKeys::Verify(const std::string& kid, const std::string& alg, const std::string& data,
                        const std::string& signature, std::string& error) const {
    const std::string kty = alg == "RS256" ? "RSA" : alg == "ES256" ? "EC" : "";
    if (kty.empty()) {
        error = "a token signed with " + (alg.empty() ? std::string("nothing") : alg) + " is not accepted (RS256, ES256)";
        return false;
    }
    const Impl::One* found = nullptr;
    for (const auto& one : impl_->keys)
        if (one.kty == kty && (one.kid == kid || (kid.empty() && impl_->keys.size() == 1))) found = &one;
    if (!found) {
        error = "the token is signed with a key the provider does not publish (" + kid + ")";
        return false;
    }
    const std::string sig = alg == "ES256" ? EcdsaDer(signature) : signature;
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    const bool ok = ctx && !sig.empty() &&
                    EVP_DigestVerifyInit(ctx, nullptr, EVP_sha256(), nullptr, found->key.get()) == 1 &&
                    EVP_DigestVerify(ctx, reinterpret_cast<const unsigned char*>(sig.data()), sig.size(),
                                     reinterpret_cast<const unsigned char*>(data.data()), data.size()) == 1;
    EVP_MD_CTX_free(ctx);
    if (!ok) error = "the token's signature is not the provider's";
    return ok;
}

// ===== the ID token =====

std::string IdTokenKeyId(const std::string& jwt) {
    std::string header;
    if (!Base64UrlDecode(jwt.substr(0, jwt.find('.')), header)) return std::string();
    Doc doc(header);
    return Str(doc.Root(), "kid");
}

bool VerifyIdToken(const std::string& jwt, const PublicKeys& keys, const TokenExpectations& expect, Identity& out,
                   std::string& error) {
    out = Identity();
    const size_t first = jwt.find('.');
    const size_t second = first == std::string::npos ? std::string::npos : jwt.find('.', first + 1);
    if (second == std::string::npos || jwt.find('.', second + 1) != std::string::npos) {
        error = "not an ID token";
        return false;
    }
    std::string headerText, claimsText, signature;
    if (!Base64UrlDecode(jwt.substr(0, first), headerText) ||
        !Base64UrlDecode(jwt.substr(first + 1, second - first - 1), claimsText) ||
        !Base64UrlDecode(jwt.substr(second + 1), signature)) {
        error = "the ID token is not base64url";
        return false;
    }
    Doc header(headerText), claims(claimsText);
    if (!header.Root() || !claims.Root() || !yyjson_is_obj(claims.Root())) {
        error = "the ID token is not JSON";
        return false;
    }
    if (!keys.Verify(Str(header.Root(), "kid"), Str(header.Root(), "alg"), jwt.substr(0, second), signature, error))
        return false;
    yyjson_val* c = claims.Root();
    if (Str(c, "iss") != expect.issuer) {
        error = "the ID token is from " + Str(c, "iss") + ", not " + expect.issuer;
        return false;
    }
    bool forUs = false;
    size_t audiences = 0;
    yyjson_val* aud = yyjson_obj_get(c, "aud");
    if (aud && yyjson_is_str(aud)) {
        forUs = yyjson_get_str(aud) == expect.clientId;
        audiences = 1;
    } else if (aud && yyjson_is_arr(aud)) {
        size_t index, max;
        yyjson_val* one;
        yyjson_arr_foreach(aud, index, max, one) {
            ++audiences;
            forUs = forUs || (yyjson_is_str(one) && yyjson_get_str(one) == expect.clientId);
        }
    }
    const std::string azp = Str(c, "azp");
    if (!forUs || (audiences > 1 && !azp.empty() && azp != expect.clientId)) {
        error = "the ID token is for another application";
        return false;
    }
    yyjson_val* exp = yyjson_obj_get(c, "exp");
    if (!exp || !yyjson_is_num(exp) || static_cast<int64_t>(yyjson_get_num(exp)) + 60 < expect.now) {
        error = "the ID token has expired";
        return false;
    }
    yyjson_val* nbf = yyjson_obj_get(c, "nbf");
    if (nbf && yyjson_is_num(nbf) && static_cast<int64_t>(yyjson_get_num(nbf)) > expect.now + 60) {
        error = "the ID token is not valid yet";
        return false;
    }
    if (expect.nonce.empty() || Str(c, "nonce") != expect.nonce) {
        error = "the ID token is not for this sign-in (nonce)";
        return false;
    }
    out.subject = Str(c, "sub");
    if (out.subject.empty()) {
        error = "the ID token names nobody (sub)";
        return false;
    }
    out.email = Lower(Str(c, "email"));
    yyjson_val* verified = yyjson_obj_get(c, "email_verified");
    out.emailVerified = verified && ((yyjson_is_bool(verified) && yyjson_get_bool(verified)) ||
                                     (yyjson_is_str(verified) && std::string(yyjson_get_str(verified)) == "true"));
    return true;
}

std::string UserFor(const Identity& identity, const std::map<std::string, UserIdentity>& identities) {
    for (const auto& [user, known] : identities)
        if (!known.subject.empty() && known.subject == identity.subject) return user;
    if (!identity.emailVerified || identity.email.empty()) return std::string();
    for (const auto& [user, known] : identities)
        if (!known.email.empty() && known.email == identity.email) return user;
    return std::string();
}

std::string OriginOf(const std::string& url) {
    const size_t scheme = url.find("://");
    if (scheme == std::string::npos) return std::string();
    const size_t end = url.find_first_of("/?#", scheme + 3);
    return Lower(url.substr(0, end));
}

bool ReturnAllowed(const std::string& returnUrl, const std::string& publicUrl, const std::vector<std::string>& origins) {
    if (returnUrl.find_first_of(" \r\n\t\"<>\\") != std::string::npos) return false;
    if (Loopback(returnUrl)) return true;                   // this computer: a desktop app's listener
    const std::string origin = OriginOf(returnUrl);
    if (origin.empty()) return false;
    if (origin == OriginOf(publicUrl)) return true;
    for (const std::string& allowed : origins)
        if (origin == Lower(allowed)) return true;
    return false;
}

// ===== RelaySignIn =====

RelaySignIn::RelaySignIn(const RelayConfig& config, std::string clientSecret,
                         std::function<void(const std::string&, const std::string&, bool)> log)
    : config_(config), clientSecret_(std::move(clientSecret)), log_(std::move(log)) {}

bool RelaySignIn::Start(std::string& error) {
    std::string issuer = config_.signIn.issuer;
    while (!issuer.empty() && issuer.back() == '/') issuer.pop_back();
    std::string body;
    if (!Fetch(issuer + "/.well-known/openid-configuration", body, error)) return false;
    Doc doc(body);
    if (!doc.Root()) {
        error = "the provider's discovery document is not JSON";
        return false;
    }
    if (Str(doc.Root(), "issuer") != config_.signIn.issuer) {
        error = "the provider calls itself " + Str(doc.Root(), "issuer") + ", not " + config_.signIn.issuer +
                " - issuer must be written as the provider writes it";
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(m_);
        authorizationEndpoint_ = Str(doc.Root(), "authorization_endpoint");
        tokenEndpoint_ = Str(doc.Root(), "token_endpoint");
        jwksUri_ = Str(doc.Root(), "jwks_uri");
    }
    if (authorizationEndpoint_.empty() || tokenEndpoint_.empty() || jwksUri_.empty()) {
        error = "the provider's discovery document names no authorization, token or key endpoint";
        return false;
    }
    return RefreshKeys(error);
}

bool RelaySignIn::RefreshKeys(std::string& error) {
    std::string uri;
    {
        std::lock_guard<std::mutex> lock(m_);
        uri = jwksUri_;
    }
    std::string body;
    if (!Fetch(uri, body, error)) return false;
    PublicKeys keys;
    if (!keys.Parse(body, error)) return false;
    std::lock_guard<std::mutex> lock(m_);
    keys_ = std::move(keys);
    keysRead_ = std::chrono::steady_clock::now();
    return true;
}

std::string RelaySignIn::AuthorizationEndpoint() const {
    std::lock_guard<std::mutex> lock(m_);
    return authorizationEndpoint_;
}

std::string RelaySignIn::TokenEndpoint() const {
    std::lock_guard<std::mutex> lock(m_);
    return tokenEndpoint_;
}

bool RelaySignIn::Handle(const HttpRequest& request, HttpReply& reply) {
    if (request.path == config_.path + "/signin") return SignIn(request, reply);
    if (request.path == config_.path + "/callback") return Callback(request, reply);
    return false;
}

bool RelaySignIn::SignIn(const HttpRequest& request, HttpReply& reply) {
    const auto returnIt = request.query.find("return");
    const std::string returnUrl = returnIt == request.query.end() ? std::string() : returnIt->second;
    if (!ReturnAllowed(returnUrl, config_.publicUrl, config_.origins)) {
        Page(reply, 400, "Bad Request",
             "The sign-in may only return to " + OriginOf(config_.publicUrl) +
                 ", the configured origins or this computer, not to " + OriginOf(returnUrl) + ".");
        return true;
    }
    Pending pending;
    const UltraNetOAuth2Pkce pkce = UltraNet_OAuth2GeneratePkce();
    pending.verifier = pkce.codeVerifier;
    pending.nonce = UltraNet_OAuth2GenerateState();
    pending.returnUrl = returnUrl.substr(0, returnUrl.find('#'));
    const auto state = request.query.find("state");
    if (state != request.query.end()) pending.clientState = state->second.substr(0, 256);
    pending.created = std::chrono::steady_clock::now();
    const std::string key = UltraNet_OAuth2GenerateState();

    UltraNetOAuth2Config oauth;
    {
        std::lock_guard<std::mutex> lock(m_);
        oauth.authorizationEndpoint = authorizationEndpoint_;
        // Sign-ins never finished go after ten minutes; a flood of them is capped.
        for (auto it = pending_.begin(); it != pending_.end();)
            it = pending.created - it->second.created > std::chrono::minutes(10) ? pending_.erase(it) : std::next(it);
        if (pending_.size() >= 1000) {
            Page(reply, 503, "Service Unavailable", "Too many sign-ins are under way. Try again in a few minutes.");
            return true;
        }
        pending_[key] = pending;
    }
    oauth.clientId = config_.signIn.clientId;
    std::istringstream scopes(config_.signIn.scopes);
    for (std::string scope; scopes >> scope;) oauth.scopes.push_back(scope);
    oauth.redirectUri = config_.publicUrl + "/callback";
    oauth.extraAuthParams["nonce"] = pending.nonce;
    reply.status = 302;
    reply.reason = "Found";
    reply.headers["Location"] = UltraNet_OAuth2BuildAuthUrl(oauth, pkce, key);
    reply.headers["Cache-Control"] = "no-store";
    reply.body.clear();
    return true;
}

bool RelaySignIn::Callback(const HttpRequest& request, HttpReply& reply) {
    auto param = [&request](const char* name) {
        const auto it = request.query.find(name);
        return it == request.query.end() ? std::string() : it->second;
    };
    Pending pending;
    {
        std::lock_guard<std::mutex> lock(m_);
        const auto it = pending_.find(param("state"));
        if (it == pending_.end() || std::chrono::steady_clock::now() - it->second.created > std::chrono::minutes(10)) {
            if (it != pending_.end()) pending_.erase(it);
            Page(reply, 400, "Bad Request", "This sign-in is not known here, or it was finished already. Start it again.");
            return true;
        }
        pending = it->second;
        pending_.erase(it);                 // one use
    }
    auto refuse = [&](int status, const char* reason, const std::string& why) {
        if (log_) log_(request.peer, "-- sign-in with " + config_.signIn.label + " refused: " + why, false);
        Page(reply, status, reason, why);
        return true;
    };
    if (!param("error").empty())
        return refuse(403, "Forbidden", config_.signIn.label + " did not sign you in: " +
                                            (param("error_description").empty() ? param("error") : param("error_description")));

    UltraNetOAuth2Config oauth;
    {
        std::lock_guard<std::mutex> lock(m_);
        oauth.tokenEndpoint = tokenEndpoint_;
    }
    oauth.clientId = config_.signIn.clientId;
    oauth.clientSecret = clientSecret_;
    oauth.redirectUri = config_.publicUrl + "/callback";
    UltraNetOAuth2Token token;
    const UltraNetResult exchanged = UltraNet_OAuth2ExchangeCode(oauth, param("code"), pending.verifier, token);
    if (!exchanged) return refuse(502, "Bad Gateway", config_.signIn.label + " did not answer the sign-in: " + exchanged.message);
    if (token.idToken.empty())
        return refuse(502, "Bad Gateway", config_.signIn.label + " sent no ID token - is openid among the scopes?");

    TokenExpectations expect;
    expect.issuer = config_.signIn.issuer;
    expect.clientId = config_.signIn.clientId;
    expect.nonce = pending.nonce;
    expect.now = static_cast<int64_t>(std::time(nullptr));
    // A key not seen yet: the provider may have rolled its keys - read them
    // again, once a minute at most.
    const std::string kid = IdTokenKeyId(token.idToken);
    bool stale = false;
    {
        std::lock_guard<std::mutex> lock(m_);
        stale = !keys_.Has(kid) && std::chrono::steady_clock::now() - keysRead_ > std::chrono::minutes(1);
    }
    std::string error;
    if (stale && !RefreshKeys(error)) return refuse(502, "Bad Gateway", error);
    Identity identity;
    bool verified = false;
    {
        std::lock_guard<std::mutex> lock(m_);
        verified = VerifyIdToken(token.idToken, keys_, expect, identity, error);
    }
    if (!verified) return refuse(403, "Forbidden", error);
    const std::string who = identity.email.empty() ? identity.subject : identity.email;
    const std::string user = UserFor(identity, config_.identities);
    if (user.empty())
        return refuse(403, "Forbidden",
                      who + (identity.email.empty() || identity.emailVerified ? std::string()
                                                                                : " (an email the provider has not verified)") +
                          " has no access to this relay.");

    const std::string ticket = RandomToken(32);
    if (ticket.empty()) return refuse(500, "Internal Server Error", "no random bytes for a ticket");
    {
        std::lock_guard<std::mutex> lock(m_);
        const auto now = std::chrono::system_clock::now();
        for (auto it = tickets_.begin(); it != tickets_.end();)
            it = it->second.expires < now ? tickets_.erase(it) : std::next(it);
        tickets_[UltraDb_RelayTokenHash(ticket)] = Granted{ user, now + std::chrono::hours(config_.signIn.ticketHours) };
    }
    if (log_) log_(request.peer, "-- signed in with " + config_.signIn.label + " as " + who + ": user " + user, true);
    reply.status = 302;
    reply.reason = "Found";
    reply.headers["Cache-Control"] = "no-store";
    reply.headers["Referrer-Policy"] = "no-referrer";
    if (Loopback(pending.returnUrl) && !pending.clientState.empty()) {
        // A desktop app's loopback listener, which sent a state of its own,
        // reads the query, as an OAuth redirect.
        reply.headers["Location"] = pending.returnUrl + (pending.returnUrl.find('?') == std::string::npos ? "?" : "&") +
                                    "code=" + ticket + "&state=" + PercentEncode(pending.clientState);
    } else {
        // A page reads the fragment, which no server and no log sees.
        reply.headers["Location"] = pending.returnUrl + "#relay_ticket=" + ticket;
    }
    reply.body.clear();
    return true;
}

bool RelaySignIn::Ticket(const std::string& ticket, std::string& user) {
    const std::string hash = UltraDb_RelayTokenHash(ticket);
    std::lock_guard<std::mutex> lock(m_);
    const auto it = tickets_.find(hash);
    if (it == tickets_.end()) return false;
    if (it->second.expires < std::chrono::system_clock::now()) {
        tickets_.erase(it);
        return false;
    }
    user = it->second.user;
    return true;
}

} // namespace UltraDBAdminRelay
