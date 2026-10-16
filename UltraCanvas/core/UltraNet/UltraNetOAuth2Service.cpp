// core/UltraNet/UltraNetOAuth2Service.cpp
// The client of the OAuth2 sign-in service (UltraNetOAuth2Service.h): four
// small HTTP calls on UltraNet_HttpGet / UltraNet_HttpPost, their flat JSON
// answers read by the scanner in UltraNetFlatJson.h. The provider's token
// answer comes back through the service verbatim, so the exchange and the
// refresh parse it with UltraNet_OAuth2ParseTokenResponse as the direct
// flow does.
// Version: 1.0.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraNet/UltraNetOAuth2Service.h"
#include "UltraNet/UltraNetHttp.h"
#include "UltraNet/UltraNetUrl.h"
#include "UltraNetFlatJson.h"

#include <chrono>
#include <cstdlib>
#include <thread>

namespace {

using ultranet_internal::ForEachTopLevelField;

void AppendFormField(std::string& body, const std::string& key, const std::string& value) {
    if (!body.empty()) body += '&';
    body += UltraNet_UrlEncode(key);
    body += '=';
    body += UltraNet_UrlEncode(value);
}

std::string JoinScopes(const std::vector<std::string>& scopes) {
    std::string out;
    for (const auto& s : scopes) {
        if (!out.empty()) out += ' ';
        out += s;
    }
    return out;
}

UltraNetHttpOptions ServiceOptions(int timeoutMs) {
    UltraNetHttpOptions options;
    options.headers.Set("accept", "application/json");
    options.timeoutMs = timeoutMs > 0 ? timeoutMs : 30000;
    // The service is one address; a redirect away from it is not it.
    options.followRedirects = false;
    return options;
}

// The service's {"error": ..., "error_description": ...}, as one message.
std::string ServiceErrorMessage(const std::string& body, int httpStatus, const std::string& what) {
    std::string error, description;
    ForEachTopLevelField(body, [&](const std::string& key, const std::string& value) {
        if (key == "error") error = value;
        else if (key == "error_description") description = value;
    });
    std::string msg = "the sign-in service could not " + what;
    if (!error.empty()) {
        msg += ": " + error;
        if (!description.empty()) msg += " - " + description;
    } else if (httpStatus > 0) {
        msg += " (HTTP " + std::to_string(httpStatus) + ")";
    }
    return msg;
}

// Maps a failed call to the service to a result: the transport's own error
// when nothing came back, else the service's error with a code that says
// what kind of refusal it was.
UltraNetResult ServiceFailure(const UltraNetResult& transport, const UltraNetResponse& response,
                              const std::string& what) {
    const std::string body = response.GetBodyAsString();
    if (!transport && body.empty() && response.statusCode == 0) {
        UltraNetResult r = transport;
        r.message = "the sign-in service did not answer: " + transport.message;
        return r;
    }
    const int status = response.statusCode;
    UltraNetResultCode code = UltraNetResultCode::AuthenticationFailed;
    if (status == 404)      code = UltraNetResultCode::NotFound;
    else if (status == 429) code = UltraNetResultCode::AccessDenied;
    else if (status >= 500) code = UltraNetResultCode::HttpError;
    UltraNetResult r = UltraNetResult::Error(code, ServiceErrorMessage(body, status, what));
    r.httpStatus = status;
    r.url = transport.url;
    return r;
}

bool Succeeded(const UltraNetResult& transport, const UltraNetResponse& response) {
    return transport && response.statusCode >= 200 && response.statusCode < 300;
}

UltraNetResult RequireService(const UltraNetOAuth2ServiceConfig& config) {
    if (config.serviceUrl.empty())
        return UltraNetResult::Error(UltraNetResultCode::InvalidState,
                                     "no sign-in service URL is configured");
    if (config.providerId.empty())
        return UltraNetResult::Error(UltraNetResultCode::InvalidState,
                                     "the provider to sign in to is not named");
    return UltraNetResult::Ok();
}

// A token call through the service: the provider's answer verbatim, parsed
// as the direct flow parses it.
UltraNetResult PostTokenThroughService(const UltraNetOAuth2ServiceConfig& config,
                                       std::string body, UltraNetOAuth2Token& outToken) {
    outToken = {};
    UltraNetResult ok = RequireService(config);
    if (!ok) return ok;
    UltraNetHttpOptions options = ServiceOptions(config.requestTimeoutMs);
    options.headers.Set("content-type", "application/x-www-form-urlencoded");
    UltraNetResponse response;
    UltraNetResult r = UltraNet_HttpPost(UltraNet_OAuth2ServiceUrl(config.serviceUrl, "v1/token"),
                                         std::vector<uint8_t>(body.begin(), body.end()),
                                         response, options);
    const std::string responseBody = response.GetBodyAsString();
    if (!r && responseBody.empty()) return ServiceFailure(r, response, "reach the provider");
    // The provider's own answer, success or error, is what is there - with
    // the service's own refusals (404 unknown provider, 429) in the same
    // error / error_description shape.
    UltraNetResult parse = UltraNet_OAuth2ParseTokenResponse(responseBody, outToken);
    if (parse && !r) return r;
    if (!parse) {
        parse.httpStatus = response.statusCode;
        if (response.statusCode == 404) parse.code = UltraNetResultCode::NotFound;
        else if (response.statusCode == 429) parse.code = UltraNetResultCode::AccessDenied;
        else if (response.statusCode >= 500) parse.code = UltraNetResultCode::HttpError;
    }
    return parse;
}

} // namespace

// ============================================================================
// Public surface
// ============================================================================
std::string UltraNet_OAuth2ServiceUrl(const std::string& serviceUrl, const std::string& path) {
    std::string base = serviceUrl;
    while (!base.empty() && base.back() == '/') base.pop_back();
    std::string rel = path;
    while (!rel.empty() && rel.front() == '/') rel.erase(rel.begin());
    return base + "/" + rel;
}

UltraNetResult UltraNet_OAuth2ServiceParseSession(const std::string& jsonBody,
                                                  UltraNetOAuth2ServiceSession& outSession) {
    outSession = {};
    std::string error, description;
    const bool wellFormed = ForEachTopLevelField(jsonBody,
        [&](const std::string& key, const std::string& value) {
            if      (key == "session_id")        outSession.sessionId = value;
            else if (key == "authorization_url") outSession.authorizationUrl = value;
            else if (key == "expires_in")        outSession.expiresInSeconds =
                                                     static_cast<int>(std::strtol(value.c_str(), nullptr, 10));
            else if (key == "error")             error = value;
            else if (key == "error_description") description = value;
        });
    if (!error.empty()) {
        std::string msg = "the sign-in service refused to start the sign-in: " + error;
        if (!description.empty()) msg += " - " + description;
        return UltraNetResult::Error(UltraNetResultCode::AuthenticationFailed, msg);
    }
    if (!wellFormed)
        return UltraNetResult::Error(UltraNetResultCode::AuthenticationFailed,
                                     "the sign-in service returned a malformed answer");
    if (!outSession.IsValid())
        return UltraNetResult::Error(UltraNetResultCode::AuthenticationFailed,
                                     "the sign-in service's answer names no session or consent URL");
    return UltraNetResult::Ok();
}

UltraNetResult UltraNet_OAuth2ServiceParseStatus(const std::string& jsonBody,
                                                 UltraNetOAuth2CallbackResult& outResult,
                                                 bool& outPending) {
    outResult = {};
    outPending = false;
    std::string status;
    const bool wellFormed = ForEachTopLevelField(jsonBody,
        [&](const std::string& key, const std::string& value) {
            if      (key == "status")            status = value;
            else if (key == "code")              outResult.code = value;
            else if (key == "error")             outResult.error = value;
            else if (key == "error_description") outResult.errorDescription = value;
        });
    if (!wellFormed)
        return UltraNetResult::Error(UltraNetResultCode::AuthenticationFailed,
                                     "the sign-in service returned a malformed answer");
    if (status == "pending") {
        outResult = {};
        outPending = true;
        return UltraNetResult::Ok();
    }
    if (status == "ready" && !outResult.code.empty()) return UltraNetResult::Ok();
    if (status == "error" && !outResult.error.empty()) return UltraNetResult::Ok();
    if (!outResult.error.empty()) {
        // No status: the service's own refusal (404 unknown_session, 429).
        std::string msg = "the sign-in service lost the sign-in: " + outResult.error;
        if (!outResult.errorDescription.empty()) msg += " - " + outResult.errorDescription;
        outResult = {};
        return UltraNetResult::Error(UltraNetResultCode::NotFound, msg);
    }
    outResult = {};
    return UltraNetResult::Error(UltraNetResultCode::AuthenticationFailed,
                                 "the sign-in service's answer names no status");
}

UltraNetResult UltraNet_OAuth2ServiceBegin(const UltraNetOAuth2ServiceConfig& config,
                                           const UltraNetOAuth2Pkce& pkce,
                                           UltraNetOAuth2ServiceSession& outSession) {
    outSession = {};
    UltraNetResult ok = RequireService(config);
    if (!ok) return ok;
    if (!pkce.IsValid())
        return UltraNetResult::Error(UltraNetResultCode::InvalidState,
                                     "a PKCE challenge is required (UltraNet_OAuth2GeneratePkce)");
    std::string body;
    AppendFormField(body, "provider", config.providerId);
    AppendFormField(body, "code_challenge", pkce.codeChallenge);
    AppendFormField(body, "code_challenge_method", pkce.method);
    if (!config.loginHint.empty()) AppendFormField(body, "login_hint", config.loginHint);
    if (!config.scopes.empty())    AppendFormField(body, "scope", JoinScopes(config.scopes));
    if (!config.clientName.empty()) AppendFormField(body, "client", config.clientName);

    UltraNetHttpOptions options = ServiceOptions(config.requestTimeoutMs);
    options.headers.Set("content-type", "application/x-www-form-urlencoded");
    UltraNetResponse response;
    UltraNetResult r = UltraNet_HttpPost(UltraNet_OAuth2ServiceUrl(config.serviceUrl, "v1/sessions"),
                                         std::vector<uint8_t>(body.begin(), body.end()),
                                         response, options);
    if (!Succeeded(r, response)) {
        UltraNetResult f = ServiceFailure(r, response, "start the sign-in");
        if (response.statusCode == 404) f.code = UltraNetResultCode::Unsupported;
        return f;
    }
    UltraNetResult parse = UltraNet_OAuth2ServiceParseSession(response.GetBodyAsString(), outSession);
    if (!parse) parse.httpStatus = response.statusCode;
    return parse;
}

UltraNetResult UltraNet_OAuth2ServiceWaitForCode(const UltraNetOAuth2ServiceConfig& config,
                                                 const std::string& sessionId,
                                                 UltraNetOAuth2CallbackResult& outResult,
                                                 int timeoutMs,
                                                 const std::function<bool()>& isCancelled) {
    outResult = {};
    UltraNetResult ok = RequireService(config);
    if (!ok) return ok;
    if (sessionId.empty())
        return UltraNetResult::Error(UltraNetResultCode::InvalidState, "no sign-in session to wait for");

    using Clock = std::chrono::steady_clock;
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    int wait = config.pollWaitSeconds;
    if (wait < 0) wait = 0;
    if (wait > 30) wait = 30;
    const std::string url = UltraNet_OAuth2ServiceUrl(config.serviceUrl, "v1/sessions/" + sessionId)
                            + "?wait=" + std::to_string(wait);
    for (;;) {
        if (isCancelled && isCancelled())
            return UltraNetResult::Error(UltraNetResultCode::Cancelled,
                                         "the sign-in was cancelled before the browser answered");
        if (Clock::now() >= deadline)
            return UltraNetResult::Error(UltraNetResultCode::Timeout,
                                         "no authorization answer arrived before the timeout");

        UltraNetHttpOptions options = ServiceOptions(config.requestTimeoutMs);
        options.timeoutMs += wait * 1000;
        UltraNetResponse response;
        UltraNetResult r = UltraNet_HttpGet(url, response, options);
        if (!Succeeded(r, response)) return ServiceFailure(r, response, "report the sign-in");

        bool pending = false;
        UltraNetResult parse = UltraNet_OAuth2ServiceParseStatus(response.GetBodyAsString(),
                                                                 outResult, pending);
        if (!parse) return parse;
        if (!pending) return UltraNetResult::Ok();
        // "pending" after a held poll: ask again at once. After an unheld
        // one (wait 0), pause so a service answering instantly is not polled
        // in a busy loop.
        if (wait == 0) {
            for (int i = 0; i < 10 && !(isCancelled && isCancelled()); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

UltraNetResult UltraNet_OAuth2ServiceExchangeCode(const UltraNetOAuth2ServiceConfig& config,
                                                  const std::string& code,
                                                  const std::string& codeVerifier,
                                                  UltraNetOAuth2Token& outToken) {
    outToken = {};
    if (code.empty())
        return UltraNetResult::Error(UltraNetResultCode::InvalidState, "authorization code is empty");
    if (codeVerifier.empty())
        return UltraNetResult::Error(UltraNetResultCode::InvalidState,
                                     "the PKCE verifier is required for an exchange through the service");
    std::string body;
    AppendFormField(body, "provider", config.providerId);
    AppendFormField(body, "grant_type", "authorization_code");
    AppendFormField(body, "code", code);
    AppendFormField(body, "code_verifier", codeVerifier);
    if (!config.scopes.empty()) AppendFormField(body, "scope", JoinScopes(config.scopes));
    return PostTokenThroughService(config, std::move(body), outToken);
}

UltraNetResult UltraNet_OAuth2ServiceRefresh(const UltraNetOAuth2ServiceConfig& config,
                                             const std::string& refreshToken,
                                             UltraNetOAuth2Token& outToken) {
    outToken = {};
    if (refreshToken.empty())
        return UltraNetResult::Error(UltraNetResultCode::InvalidState, "refresh token is empty");
    std::string body;
    AppendFormField(body, "provider", config.providerId);
    AppendFormField(body, "grant_type", "refresh_token");
    AppendFormField(body, "refresh_token", refreshToken);
    if (!config.scopes.empty()) AppendFormField(body, "scope", JoinScopes(config.scopes));
    UltraNetResult r = PostTokenThroughService(config, std::move(body), outToken);
    // Providers that don't rotate refresh tokens omit the field; keep the
    // one that worked so callers can store outToken as-is.
    if (r && outToken.refreshToken.empty()) outToken.refreshToken = refreshToken;
    return r;
}

UltraNetResult UltraNet_OAuth2ServiceProviders(const std::string& serviceUrl,
                                               std::vector<std::string>& outProviderIds,
                                               int timeoutMs) {
    outProviderIds.clear();
    if (serviceUrl.empty())
        return UltraNetResult::Error(UltraNetResultCode::InvalidState,
                                     "no sign-in service URL is configured");
    UltraNetResponse response;
    UltraNetResult r = UltraNet_HttpGet(UltraNet_OAuth2ServiceUrl(serviceUrl, "v1/providers"),
                                        response, ServiceOptions(timeoutMs));
    if (!Succeeded(r, response)) return ServiceFailure(r, response, "list its providers");
    std::string list;
    const bool wellFormed = ForEachTopLevelField(response.GetBodyAsString(),
        [&](const std::string& key, const std::string& value) {
            if (key == "providers") list = value;
        });
    if (!wellFormed)
        return UltraNetResult::Error(UltraNetResultCode::AuthenticationFailed,
                                     "the sign-in service returned a malformed answer");
    std::size_t i = 0;
    while (i < list.size()) {
        std::size_t sp = list.find(' ', i);
        if (sp == std::string::npos) sp = list.size();
        if (sp > i) outProviderIds.push_back(list.substr(i, sp - i));
        i = sp + 1;
    }
    return UltraNetResult::Ok();
}

UltraNetResult UltraNet_OAuth2AuthorizeViaService(
    const UltraNetOAuth2ServiceConfig& config,
    const std::function<void(const std::string&)>& onOpenUrl,
    UltraNetOAuth2Token& outToken,
    int timeoutMs,
    const std::function<bool()>& isCancelled) {
    outToken = {};
    UltraNetResult ok = RequireService(config);
    if (!ok) return ok;
    if (!onOpenUrl)
        return UltraNetResult::Error(UltraNetResultCode::InvalidState,
                                     "an onOpenUrl callback is required to show the consent page");

    const UltraNetOAuth2Pkce pkce = UltraNet_OAuth2GeneratePkce();
    UltraNetOAuth2ServiceSession session;
    UltraNetResult begun = UltraNet_OAuth2ServiceBegin(config, pkce, session);
    if (!begun) return begun;

    onOpenUrl(session.authorizationUrl);

    UltraNetOAuth2CallbackResult cb;
    UltraNetResult waited = UltraNet_OAuth2ServiceWaitForCode(config, session.sessionId, cb,
                                                              timeoutMs, isCancelled);
    if (!waited) return waited;
    if (!cb.error.empty()) {
        std::string msg = "authorization denied: " + cb.error;
        if (!cb.errorDescription.empty()) msg += " - " + cb.errorDescription;
        return UltraNetResult::Error(UltraNetResultCode::AuthenticationFailed, msg);
    }
    return UltraNet_OAuth2ServiceExchangeCode(config, cb.code, pkce.codeVerifier, outToken);
}
