// core/UltraNet/UltraNetOAuth2.cpp
// OAuth 2.0 authorization-code + PKCE helper. Self-contained: SHA-256 and
// base64url live in this TU (UltraNet must not call TLS-library crypto —
// the backend differs per platform), the loopback redirect listener rides
// on UltraNet_Tcp*, and the token exchange on UltraNet_HttpPost. The token
// JSON is a flat object, so a focused scanner below extracts the standard
// fields without pulling a JSON engine into the UltraNet target.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#if defined(_WIN32) || defined(_WIN64)
  // rand_s() — must be defined before the first <stdlib.h> inclusion.
  #ifndef _CRT_RAND_S
  #define _CRT_RAND_S
  #endif
#endif

#include "UltraNet/UltraNetOAuth2.h"
#include "UltraNet/UltraNetHttp.h"
#include "UltraNet/UltraNetSocket.h"
#include "UltraNet/UltraNetUrl.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>

namespace {

// ============================================================================
// Random bytes — OS entropy source, std::random_device as last resort.
// ============================================================================
bool RandomBytes(uint8_t* out, std::size_t n) {
#if defined(_WIN32) || defined(_WIN64)
    for (std::size_t i = 0; i < n; i += sizeof(unsigned int)) {
        unsigned int v = 0;
        if (rand_s(&v) != 0) return false;
        std::memcpy(out + i, &v, std::min(sizeof v, n - i));
    }
    return true;
#else
    if (std::FILE* f = std::fopen("/dev/urandom", "rb")) {
        std::size_t got = std::fread(out, 1, n, f);
        std::fclose(f);
        if (got == n) return true;
    }
    return false;
#endif
}

void FillRandom(uint8_t* out, std::size_t n) {
    if (RandomBytes(out, n)) return;
    std::random_device rd;
    for (std::size_t i = 0; i < n; ++i) {
        out[i] = static_cast<uint8_t>(rd());
    }
}

// ============================================================================
// SHA-256 (FIPS 180-4). Only used for the PKCE S256 challenge.
// ============================================================================
struct Sha256 {
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                     0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    uint8_t  block[64]{};
    uint64_t totalBits = 0;
    std::size_t fill = 0;

    static uint32_t Rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

    void Compress(const uint8_t* p) {
        static const uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b,
            0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
            0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7,
            0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
            0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152,
            0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
            0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
            0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819,
            0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
            0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
            0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
            0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (uint32_t(p[i * 4]) << 24) | (uint32_t(p[i * 4 + 1]) << 16) |
                   (uint32_t(p[i * 4 + 2]) << 8) | uint32_t(p[i * 4 + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t s1 = Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t t1 = hh + s1 + ch + k[i] + w[i];
            uint32_t s0 = Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = s0 + maj;
            hh = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }

    void Update(const uint8_t* data, std::size_t n) {
        totalBits += uint64_t(n) * 8;
        while (n > 0) {
            std::size_t take = std::min(n, sizeof(block) - fill);
            std::memcpy(block + fill, data, take);
            fill += take; data += take; n -= take;
            if (fill == sizeof(block)) { Compress(block); fill = 0; }
        }
    }

    void Final(uint8_t out[32]) {
        uint64_t bits = totalBits;
        uint8_t pad = 0x80;
        Update(&pad, 1);
        uint8_t zero = 0;
        while (fill != 56) Update(&zero, 1);
        uint8_t len[8];
        for (int i = 0; i < 8; ++i) len[i] = uint8_t(bits >> (56 - i * 8));
        Update(len, 8);
        for (int i = 0; i < 8; ++i) {
            out[i * 4]     = uint8_t(h[i] >> 24);
            out[i * 4 + 1] = uint8_t(h[i] >> 16);
            out[i * 4 + 2] = uint8_t(h[i] >> 8);
            out[i * 4 + 3] = uint8_t(h[i]);
        }
    }
};

// ============================================================================
// base64url without padding (RFC 4648 §5) — the PKCE / state alphabet.
// ============================================================================
std::string Base64Url(const uint8_t* data, std::size_t n) {
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    out.reserve(((n + 2) / 3) * 4);
    for (std::size_t i = 0; i < n; i += 3) {
        uint32_t v = uint32_t(data[i]) << 16;
        if (i + 1 < n) v |= uint32_t(data[i + 1]) << 8;
        if (i + 2 < n) v |= uint32_t(data[i + 2]);
        out += alphabet[(v >> 18) & 63];
        out += alphabet[(v >> 12) & 63];
        if (i + 1 < n) out += alphabet[(v >> 6) & 63];
        if (i + 2 < n) out += alphabet[v & 63];
    }
    return out;
}

// ============================================================================
// Flat-JSON field scanner for token-endpoint bodies. Walks the top-level
// object, decoding string values (incl. \uXXXX) and skipping nested
// structures, and reports each top-level key/value pair to the visitor.
// ============================================================================
void SkipWs(const std::string& s, std::size_t& i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
}

void AppendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out += char(cp);
    } else if (cp < 0x800) {
        out += char(0xC0 | (cp >> 6));
        out += char(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += char(0xE0 | (cp >> 12));
        out += char(0x80 | ((cp >> 6) & 0x3F));
        out += char(0x80 | (cp & 0x3F));
    } else {
        out += char(0xF0 | (cp >> 18));
        out += char(0x80 | ((cp >> 12) & 0x3F));
        out += char(0x80 | ((cp >> 6) & 0x3F));
        out += char(0x80 | (cp & 0x3F));
    }
}

bool ParseJsonString(const std::string& s, std::size_t& i, std::string& out) {
    if (i >= s.size() || s[i] != '"') return false;
    ++i;
    out.clear();
    while (i < s.size()) {
        char c = s[i];
        if (c == '"') { ++i; return true; }
        if (c == '\\') {
            if (++i >= s.size()) return false;
            char e = s[i++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    auto hex4 = [&](uint32_t& v) {
                        if (i + 4 > s.size()) return false;
                        v = 0;
                        for (int k = 0; k < 4; ++k) {
                            char hc = s[i + k];
                            v <<= 4;
                            if (hc >= '0' && hc <= '9') v |= uint32_t(hc - '0');
                            else if (hc >= 'a' && hc <= 'f') v |= uint32_t(hc - 'a' + 10);
                            else if (hc >= 'A' && hc <= 'F') v |= uint32_t(hc - 'A' + 10);
                            else return false;
                        }
                        i += 4;
                        return true;
                    };
                    uint32_t cp = 0;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < s.size() &&
                        s[i] == '\\' && s[i + 1] == 'u') {
                        i += 2;
                        uint32_t lo = 0;
                        if (!hex4(lo)) return false;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    AppendUtf8(out, cp);
                    break;
                }
                default: return false;
            }
        } else {
            out += c;
            ++i;
        }
    }
    return false;
}

// Skips any JSON value; captures scalars (string decoded, number/bool as
// spelled) into outScalar and reports whether the value was a scalar.
bool SkipJsonValue(const std::string& s, std::size_t& i,
                   std::string& outScalar, bool& outIsScalar);

bool SkipJsonComposite(const std::string& s, std::size_t& i,
                       char open, char close) {
    ++i; // consume opener
    int depth = 1;
    while (i < s.size() && depth > 0) {
        SkipWs(s, i);
        if (i >= s.size()) return false;
        char c = s[i];
        if (c == '"') {
            std::string ignore;
            if (!ParseJsonString(s, i, ignore)) return false;
        } else {
            if (c == open) ++depth;
            else if (c == close) --depth;
            ++i;
        }
    }
    return depth == 0;
}

bool SkipJsonValue(const std::string& s, std::size_t& i,
                   std::string& outScalar, bool& outIsScalar) {
    SkipWs(s, i);
    if (i >= s.size()) return false;
    outIsScalar = false;
    char c = s[i];
    if (c == '"') {
        outIsScalar = true;
        return ParseJsonString(s, i, outScalar);
    }
    if (c == '{') return SkipJsonComposite(s, i, '{', '}');
    if (c == '[') return SkipJsonComposite(s, i, '[', ']');
    std::size_t start = i;
    while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']' &&
           s[i] != ' ' && s[i] != '\t' && s[i] != '\n' && s[i] != '\r') {
        ++i;
    }
    if (i == start) return false;
    outScalar = s.substr(start, i - start);
    outIsScalar = true;
    return true;
}

bool ForEachTopLevelField(
    const std::string& json,
    const std::function<void(const std::string& key, const std::string& value)>& visit) {
    std::size_t i = 0;
    SkipWs(json, i);
    if (i >= json.size() || json[i] != '{') return false;
    ++i;
    SkipWs(json, i);
    if (i < json.size() && json[i] == '}') return true;
    while (i < json.size()) {
        SkipWs(json, i);
        std::string key;
        if (!ParseJsonString(json, i, key)) return false;
        SkipWs(json, i);
        if (i >= json.size() || json[i] != ':') return false;
        ++i;
        std::string value;
        bool isScalar = false;
        if (!SkipJsonValue(json, i, value, isScalar)) return false;
        if (isScalar && value != "null") visit(key, value);
        SkipWs(json, i);
        if (i >= json.size()) return false;
        if (json[i] == ',') { ++i; continue; }
        if (json[i] == '}') return true;
        return false;
    }
    return false;
}

// ============================================================================
// Small shared helpers for the flow steps.
// ============================================================================
std::string JoinScopes(const std::vector<std::string>& scopes) {
    std::string out;
    for (const auto& s : scopes) {
        if (!out.empty()) out += ' ';
        out += s;
    }
    return out;
}

void AppendParam(std::string& url, bool& first,
                 const std::string& key, const std::string& value) {
    url += first ? '?' : '&';
    first = false;
    url += UltraNet_UrlEncode(key);
    url += '=';
    url += UltraNet_UrlEncode(value);
}

// application/x-www-form-urlencoded body builder (ordered).
void AppendFormField(std::string& body,
                     const std::string& key, const std::string& value) {
    if (!body.empty()) body += '&';
    body += UltraNet_UrlEncode(key);
    body += '=';
    body += UltraNet_UrlEncode(value);
}

// Query-string decode: '+' means space in a query component, then
// percent-decoding.
std::string DecodeQueryComponent(std::string s) {
    std::replace(s.begin(), s.end(), '+', ' ');
    return UltraNet_UrlDecode(s);
}

void ParseQueryParams(const std::string& query,
                      std::map<std::string, std::string>& out) {
    std::size_t i = 0;
    while (i < query.size()) {
        std::size_t amp = query.find('&', i);
        if (amp == std::string::npos) amp = query.size();
        std::string pair = query.substr(i, amp - i);
        std::size_t eq = pair.find('=');
        if (eq == std::string::npos) {
            if (!pair.empty()) out[DecodeQueryComponent(pair)] = "";
        } else {
            out[DecodeQueryComponent(pair.substr(0, eq))] =
                DecodeQueryComponent(pair.substr(eq + 1));
        }
        i = amp + 1;
    }
}

bool IsLoopbackHost(const std::string& host) {
    return host == "127.0.0.1" || host == "localhost" || host == "::1" ||
           host == "[::1]";
}

constexpr const char* kDefaultResponseHtml =
    "<!doctype html><html><head><meta charset=\"utf-8\">"
    "<title>Authorization complete</title></head>"
    "<body style=\"font-family:sans-serif;text-align:center;margin-top:15%\">"
    "<h2>Authorization complete</h2>"
    "<p>You can close this window and return to the application.</p>"
    "</body></html>";

void SendHttpResponse(UltraNetHandle conn, const std::string& status,
                      const std::string& html) {
    std::string resp = "HTTP/1.1 " + status + "\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Content-Length: " + std::to_string(html.size()) + "\r\n"
        "Connection: close\r\n\r\n" + html;
    int sent = 0;
    UltraNet_TcpSend(conn, std::vector<uint8_t>(resp.begin(), resp.end()), sent);
}

// Serves the loopback listener until the provider's redirect arrives on
// `path` or the deadline passes. Stray requests (favicon, reloads) get a
// 404 and the wait continues.
UltraNetResult RunCallbackServer(UltraNetHandle listener,
                                 const std::string& path,
                                 int timeoutMs,
                                 const std::string& responseHtml,
                                 UltraNetOAuth2CallbackResult& out) {
    using Clock = std::chrono::steady_clock;
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    const std::string& html =
        responseHtml.empty() ? kDefaultResponseHtml : responseHtml;

    for (;;) {
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                             deadline - Clock::now()).count();
        if (remaining <= 0) {
            return UltraNetResult::Error(UltraNetResultCode::Timeout,
                "no authorization redirect received before the timeout");
        }
        UltraNetEndpoint remote;
        UltraNetHandle conn = UltraNet_TcpAccept(listener, remote,
                                                 static_cast<int>(remaining));
        if (conn == UltraNetInvalidHandle) continue; // timed out or spurious

        // Read the request head. Only the request line matters; GET
        // redirects carry no body.
        std::string head;
        while (head.find("\r\n\r\n") == std::string::npos &&
               head.size() < 16384) {
            std::vector<uint8_t> chunk;
            auto r = UltraNet_TcpReceive(conn, chunk, 4096, 5000);
            if (!r || chunk.empty()) break;
            head.append(chunk.begin(), chunk.end());
        }

        std::size_t sp1 = head.find(' ');
        std::size_t sp2 = sp1 == std::string::npos
                              ? std::string::npos : head.find(' ', sp1 + 1);
        if (sp2 == std::string::npos) {
            SendHttpResponse(conn, "400 Bad Request", "<html>bad request</html>");
            UltraNet_SocketClose(conn);
            continue;
        }
        std::string target = head.substr(sp1 + 1, sp2 - sp1 - 1);
        std::size_t qpos = target.find('?');
        std::string reqPath = qpos == std::string::npos
                                  ? target : target.substr(0, qpos);
        if (reqPath != path) {
            SendHttpResponse(conn, "404 Not Found", "<html>not found</html>");
            UltraNet_SocketClose(conn);
            continue;
        }

        std::map<std::string, std::string> params;
        if (qpos != std::string::npos) {
            ParseQueryParams(target.substr(qpos + 1), params);
        }
        auto get = [&](const char* k) {
            auto it = params.find(k);
            return it == params.end() ? std::string() : it->second;
        };
        UltraNetOAuth2CallbackResult cb;
        cb.code             = get("code");
        cb.state            = get("state");
        cb.error            = get("error");
        cb.errorDescription = get("error_description");

        if (cb.code.empty() && cb.error.empty()) {
            // Right path, no OAuth params — a reload or a probe. Keep waiting.
            SendHttpResponse(conn, "400 Bad Request",
                             "<html>missing authorization response</html>");
            UltraNet_SocketClose(conn);
            continue;
        }

        SendHttpResponse(conn, "200 OK", html);
        UltraNet_SocketClose(conn);
        out = cb;
        return UltraNetResult::Ok();
    }
}

// Validates + splits a loopback redirect URI. Port may legitimately be 0
// (ephemeral, resolved by the caller before building the consent URL).
UltraNetResult SplitLoopbackRedirect(const std::string& redirectUri,
                                     std::string& outBindAddress,
                                     bool& outIpv6,
                                     int& outPort,
                                     std::string& outPath) {
    UltraNetUrlComponents c;
    auto r = UltraNet_ParseUrl(redirectUri, c);
    if (!r) return r;
    if (c.scheme != "http" || !IsLoopbackHost(c.host)) {
        return UltraNetResult::Error(UltraNetResultCode::InvalidUrl,
            "redirect URI must be http:// on a loopback host "
            "(127.0.0.1, localhost or [::1]): " + redirectUri);
    }
    outIpv6        = (c.host == "::1" || c.host == "[::1]");
    outBindAddress = outIpv6 ? "::1" : "127.0.0.1";
    outPort        = c.port >= 0 ? c.port : 80;
    outPath        = c.path.empty() ? "/" : c.path;
    return UltraNetResult::Ok();
}

UltraNetResult PostTokenRequest(const UltraNetOAuth2Config& config,
                                std::string body,
                                UltraNetOAuth2Token& outToken) {
    if (config.tokenEndpoint.empty()) {
        return UltraNetResult::Error(UltraNetResultCode::InvalidUrl,
                                     "tokenEndpoint is empty");
    }
    for (const auto& [k, v] : config.extraTokenParams) {
        AppendFormField(body, k, v);
    }
    if (!config.clientSecret.empty() && config.secretInBody) {
        AppendFormField(body, "client_secret", config.clientSecret);
    }

    UltraNetHttpOptions options;
    options.headers.Set("content-type", "application/x-www-form-urlencoded");
    options.headers.Set("accept", "application/json");
    if (!config.clientSecret.empty() && !config.secretInBody) {
        options.authType = UltraNetAuthType::Basic;
        options.credentials.type     = UltraNetAuthType::Basic;
        options.credentials.username = config.clientId;
        options.credentials.password = config.clientSecret;
    }

    UltraNetResponse response;
    auto r = UltraNet_HttpPost(config.tokenEndpoint,
                               std::vector<uint8_t>(body.begin(), body.end()),
                               response, options);
    const std::string responseBody = response.GetBodyAsString();
    if (!r && responseBody.empty()) return r;  // pure transport failure

    // Parse even a 4xx body — the server's error / error_description beats
    // a bare "HTTP 400" as a diagnostic.
    auto parse = UltraNet_OAuth2ParseTokenResponse(responseBody, outToken);
    if (parse && !r) return r;  // token in an error response — trust transport
    if (!parse) parse.httpStatus = response.statusCode;
    return parse;
}

} // namespace

// ============================================================================
// Public surface
// ============================================================================
UltraNetOAuth2Pkce UltraNet_OAuth2GeneratePkce() {
    // 32 random bytes -> 43 base64url chars, the RFC 7636 minimum length
    // at full entropy.
    uint8_t entropy[32];
    FillRandom(entropy, sizeof entropy);

    UltraNetOAuth2Pkce pkce;
    pkce.codeVerifier  = Base64Url(entropy, sizeof entropy);
    pkce.codeChallenge = UltraNet_OAuth2ChallengeFromVerifier(pkce.codeVerifier);
    return pkce;
}

std::string UltraNet_OAuth2ChallengeFromVerifier(const std::string& codeVerifier) {
    Sha256 sha;
    sha.Update(reinterpret_cast<const uint8_t*>(codeVerifier.data()),
               codeVerifier.size());
    uint8_t digest[32];
    sha.Final(digest);
    return Base64Url(digest, sizeof digest);
}

std::string UltraNet_OAuth2GenerateState(int entropyBytes) {
    if (entropyBytes < 16) entropyBytes = 16;
    if (entropyBytes > 128) entropyBytes = 128;
    std::vector<uint8_t> entropy(static_cast<std::size_t>(entropyBytes));
    FillRandom(entropy.data(), entropy.size());
    return Base64Url(entropy.data(), entropy.size());
}

std::string UltraNet_OAuth2BuildAuthUrl(const UltraNetOAuth2Config& config,
                                        const UltraNetOAuth2Pkce& pkce,
                                        const std::string& state) {
    std::string url = config.authorizationEndpoint;
    bool first = url.find('?') == std::string::npos;

    AppendParam(url, first, "response_type", "code");
    AppendParam(url, first, "client_id", config.clientId);
    AppendParam(url, first, "redirect_uri", config.redirectUri);
    if (!config.scopes.empty()) {
        AppendParam(url, first, "scope", JoinScopes(config.scopes));
    }
    if (!state.empty()) {
        AppendParam(url, first, "state", state);
    }
    if (pkce.IsValid()) {
        AppendParam(url, first, "code_challenge", pkce.codeChallenge);
        AppendParam(url, first, "code_challenge_method", pkce.method);
    }
    for (const auto& [k, v] : config.extraAuthParams) {
        AppendParam(url, first, k, v);
    }
    return url;
}

UltraNetResult UltraNet_OAuth2WaitForCallback(
    const std::string& redirectUri,
    UltraNetOAuth2CallbackResult& outResult,
    int timeoutMs,
    const std::string& responseHtml) {
    outResult = {};
    std::string bindAddress, path;
    bool ipv6 = false;
    int port = 0;
    auto r = SplitLoopbackRedirect(redirectUri, bindAddress, ipv6, port, path);
    if (!r) return r;
    if (port == 0) {
        return UltraNetResult::Error(UltraNetResultCode::InvalidUrl,
            "redirect URI needs an explicit port here; port 0 is only "
            "resolved by UltraNet_OAuth2AuthorizeInteractive");
    }

    UltraNetSocketOptions options;
    options.bindAddress  = bindAddress;
    options.useIpv6      = ipv6;
    options.reuseAddress = true;
    UltraNetHandle listener = UltraNet_TcpListen(port, options);
    if (listener == UltraNetInvalidHandle) {
        return UltraNetResult::Error(UltraNetResultCode::ConnectionRefused,
            "cannot listen on " + bindAddress + ":" + std::to_string(port) +
            " (port in use?)");
    }
    auto result = RunCallbackServer(listener, path, timeoutMs, responseHtml,
                                    outResult);
    UltraNet_SocketClose(listener);
    return result;
}

UltraNetResult UltraNet_OAuth2ExchangeCode(const UltraNetOAuth2Config& config,
                                           const std::string& code,
                                           const std::string& codeVerifier,
                                           UltraNetOAuth2Token& outToken) {
    outToken = {};
    if (code.empty()) {
        return UltraNetResult::Error(UltraNetResultCode::InvalidState,
                                     "authorization code is empty");
    }
    std::string body;
    AppendFormField(body, "grant_type", "authorization_code");
    AppendFormField(body, "code", code);
    AppendFormField(body, "redirect_uri", config.redirectUri);
    AppendFormField(body, "client_id", config.clientId);
    if (!codeVerifier.empty()) {
        AppendFormField(body, "code_verifier", codeVerifier);
    }
    return PostTokenRequest(config, std::move(body), outToken);
}

UltraNetResult UltraNet_OAuth2Refresh(const UltraNetOAuth2Config& config,
                                      const std::string& refreshToken,
                                      UltraNetOAuth2Token& outToken) {
    outToken = {};
    if (refreshToken.empty()) {
        return UltraNetResult::Error(UltraNetResultCode::InvalidState,
                                     "refresh token is empty");
    }
    std::string body;
    AppendFormField(body, "grant_type", "refresh_token");
    AppendFormField(body, "refresh_token", refreshToken);
    AppendFormField(body, "client_id", config.clientId);
    auto r = PostTokenRequest(config, std::move(body), outToken);
    // Providers that don't rotate refresh tokens omit the field in the
    // response; keep the one that worked so callers can store outToken as-is.
    if (r && outToken.refreshToken.empty()) {
        outToken.refreshToken = refreshToken;
    }
    return r;
}

UltraNetResult UltraNet_OAuth2ParseTokenResponse(const std::string& jsonBody,
                                                 UltraNetOAuth2Token& outToken) {
    outToken = {};
    outToken.raw = jsonBody;

    std::string error, errorDescription;
    bool wellFormed = ForEachTopLevelField(jsonBody,
        [&](const std::string& key, const std::string& value) {
            if      (key == "access_token")      outToken.accessToken = value;
            else if (key == "token_type")        outToken.tokenType = value;
            else if (key == "refresh_token")     outToken.refreshToken = value;
            else if (key == "scope")             outToken.scope = value;
            else if (key == "id_token")          outToken.idToken = value;
            else if (key == "expires_in")
                outToken.expiresInSeconds = std::strtoll(value.c_str(), nullptr, 10);
            else if (key == "error")             error = value;
            else if (key == "error_description") errorDescription = value;
        });

    if (!error.empty()) {
        std::string msg = "token endpoint error: " + error;
        if (!errorDescription.empty()) msg += " — " + errorDescription;
        return UltraNetResult::Error(UltraNetResultCode::AuthenticationFailed, msg);
    }
    if (!wellFormed) {
        return UltraNetResult::Error(UltraNetResultCode::AuthenticationFailed,
            "token endpoint returned a malformed response");
    }
    if (!outToken.IsValid()) {
        return UltraNetResult::Error(UltraNetResultCode::AuthenticationFailed,
            "token endpoint response carries no access_token");
    }
    return UltraNetResult::Ok();
}

UltraNetResult UltraNet_OAuth2AuthorizeInteractive(
    const UltraNetOAuth2Config& config,
    const std::function<void(const std::string&)>& onOpenUrl,
    UltraNetOAuth2Token& outToken,
    int timeoutMs) {
    outToken = {};
    if (config.authorizationEndpoint.empty() || config.clientId.empty()) {
        return UltraNetResult::Error(UltraNetResultCode::InvalidState,
            "authorizationEndpoint and clientId are required");
    }
    if (!onOpenUrl) {
        return UltraNetResult::Error(UltraNetResultCode::InvalidState,
            "an onOpenUrl callback is required to show the consent page");
    }

    std::string bindAddress, path;
    bool ipv6 = false;
    int port = 0;
    auto r = SplitLoopbackRedirect(config.redirectUri, bindAddress, ipv6,
                                   port, path);
    if (!r) return r;

    // Listen before building the consent URL so a port-0 redirect URI can be
    // resolved to the ephemeral port actually bound.
    UltraNetSocketOptions socketOptions;
    socketOptions.bindAddress  = bindAddress;
    socketOptions.useIpv6      = ipv6;
    socketOptions.reuseAddress = true;
    UltraNetHandle listener = UltraNet_TcpListen(port, socketOptions);
    if (listener == UltraNetInvalidHandle) {
        return UltraNetResult::Error(UltraNetResultCode::ConnectionRefused,
            "cannot listen on " + bindAddress + ":" + std::to_string(port) +
            " (port in use?)");
    }

    UltraNetOAuth2Config effective = config;
    if (port == 0) {
        UltraNetEndpoint local;
        auto ep = UltraNet_SocketLocalEndpoint(listener, local);
        if (!ep) {
            UltraNet_SocketClose(listener);
            return ep;
        }
        const std::string host = ipv6 ? "[::1]" : "127.0.0.1";
        effective.redirectUri =
            "http://" + host + ":" + std::to_string(local.port) + path;
    }

    UltraNetOAuth2Pkce pkce;
    if (effective.usePkce) pkce = UltraNet_OAuth2GeneratePkce();
    const std::string state = UltraNet_OAuth2GenerateState();

    onOpenUrl(UltraNet_OAuth2BuildAuthUrl(effective, pkce, state));

    UltraNetOAuth2CallbackResult cb;
    auto wait = RunCallbackServer(listener, path, timeoutMs, std::string(), cb);
    UltraNet_SocketClose(listener);
    if (!wait) return wait;

    if (!cb.error.empty()) {
        std::string msg = "authorization denied: " + cb.error;
        if (!cb.errorDescription.empty()) msg += " — " + cb.errorDescription;
        return UltraNetResult::Error(UltraNetResultCode::AuthenticationFailed, msg);
    }
    if (cb.state != state) {
        return UltraNetResult::Error(UltraNetResultCode::AuthenticationFailed,
            "state mismatch in authorization redirect (possible CSRF)");
    }
    return UltraNet_OAuth2ExchangeCode(effective, cb.code, pkce.codeVerifier,
                                       outToken);
}
