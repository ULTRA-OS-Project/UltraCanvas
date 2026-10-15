// include/UltraNet/UltraNetOAuth2Service.h
// The client side of an OAuth2 *sign-in service*: a small web service that
// holds the provider registrations - the client id and the client secret an
// application signs in as - and is the public https address the providers
// redirect to. The desktop application then ships no secret, and a provider
// that refuses loopback redirect URIs (Yahoo) redirects like any other.
// ULTRA OS runs one at https://oauth-callback.ultraos.eu
// (Server/OAuthCallback in the repository is the service).
//
// The flow, against the service at serviceUrl:
//
//   1. POST /v1/sessions   the PKCE challenge, the provider, the login hint
//                          -> a session id and the provider's consent URL
//   2. the application opens the consent URL in the browser; the provider
//      sends the browser to <service>/callback/<provider>, which records
//      the authorization code for the session
//   3. GET /v1/sessions/<id>?wait=N   until the code (or the refusal) is there
//   4. POST /v1/token      the code and the PKCE verifier -> the provider's
//                          token answer, with the secret added by the service
//      ... later, the refresh token -> a new access token, the same way.
//
// PKCE stays end to end: the verifier never leaves the application until
// the exchange, so a code seen by the browser, the service or a log is
// useless without it. UltraNet_OAuth2AuthorizeViaService runs steps 1-4 in
// one blocking call, the twin of UltraNet_OAuth2AuthorizeInteractive.
//
// Every answer of the service is a flat JSON object (the service's README
// says so), parsed by the same scanner as a provider's token answer.
// Version: 1.0.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraNetCore.h"
#include "UltraNetOAuth2.h"

#include <functional>
#include <string>
#include <vector>

// ============================================================================
// Where the service is and what to sign in to.
// ============================================================================
struct UltraNetOAuth2ServiceConfig {
    std::string serviceUrl;            // "https://oauth-callback.ultraos.eu" (a trailing '/' is fine)
    std::string providerId;            // "google", "microsoft", "yahoo": the service's provider ids
    std::string loginHint;             // the account's address, preselected on the consent page; empty = none
    // The scopes to ask for, from the service's allow-list for the provider;
    // empty = the service's default for the provider (the mail scopes).
    std::vector<std::string> scopes;
    std::string clientName;            // names the application in the service's log ("UltraMail/0.10.53"); optional
    int requestTimeoutMs = 30000;      // one call to the service, the poll's wait excluded
    // How long one poll may be held by the service before it answers
    // "pending" (the service caps it at 30 s). isCancelled is asked between
    // polls, so this is also the most a cancelled wait outlives its dialog.
    int pollWaitSeconds = 10;
};

// What step 1 returns.
struct UltraNetOAuth2ServiceSession {
    std::string sessionId;
    std::string authorizationUrl;      // open it in the system browser, never in an embedded view
    int expiresInSeconds = 0;          // how long the service waits for the browser

    bool IsValid() const { return !sessionId.empty() && !authorizationUrl.empty(); }
};

// "<serviceUrl>/<path>" with exactly one '/' between them.
std::string UltraNet_OAuth2ServiceUrl(const std::string& serviceUrl, const std::string& path);

// ============================================================================
// Step 1 - start a sign-in. The PKCE pair is the application's
// (UltraNet_OAuth2GeneratePkce); only its challenge is sent. Fails with
// Unsupported when the service serves no such provider (404), with
// AuthenticationFailed on any other refusal (the service's error and
// error_description in message), and with the transport's code when the
// service cannot be reached.
// ============================================================================
UltraNetResult UltraNet_OAuth2ServiceBegin(
    const UltraNetOAuth2ServiceConfig& config,
    const UltraNetOAuth2Pkce& pkce,
    UltraNetOAuth2ServiceSession& outSession);

// ============================================================================
// Step 3 - wait for the browser. Polls the session until the code or the
// provider's refusal is there (Ok, outResult filled - check HasCode()), the
// session expired at the service (NotFound), timeoutMs passed (Timeout) or
// isCancelled said to stop (Cancelled). Blocking - run off the UI thread.
// ============================================================================
UltraNetResult UltraNet_OAuth2ServiceWaitForCode(
    const UltraNetOAuth2ServiceConfig& config,
    const std::string& sessionId,
    UltraNetOAuth2CallbackResult& outResult,
    int timeoutMs = 300000,
    const std::function<bool()>& isCancelled = {});

// ============================================================================
// Step 4 - exchange and refresh, through the service. The answer is the
// provider's own token response, parsed as UltraNet_OAuth2ExchangeCode and
// UltraNet_OAuth2Refresh parse it; a refresh keeps the refresh token that
// worked when the provider issued no new one.
// ============================================================================
UltraNetResult UltraNet_OAuth2ServiceExchangeCode(
    const UltraNetOAuth2ServiceConfig& config,
    const std::string& code,
    const std::string& codeVerifier,
    UltraNetOAuth2Token& outToken);

UltraNetResult UltraNet_OAuth2ServiceRefresh(
    const UltraNetOAuth2ServiceConfig& config,
    const std::string& refreshToken,
    UltraNetOAuth2Token& outToken);

// ============================================================================
// Which providers the service at serviceUrl signs in to ("google",
// "microsoft", ...), from GET /v1/providers. One short request; for a
// settings page or a diagnostic, not for every sign-in.
// ============================================================================
UltraNetResult UltraNet_OAuth2ServiceProviders(
    const std::string& serviceUrl,
    std::vector<std::string>& outProviderIds,
    int timeoutMs = 10000);

// ============================================================================
// Steps 1-4 in one blocking call: generate PKCE, start the session, hand the
// consent URL to onOpenUrl, wait for the code, exchange it. Up to timeoutMs
// for the browser; isCancelled ends the wait early. Run on a worker thread,
// never the UI thread. A refusal on the consent page ends as
// AuthenticationFailed with the provider's error.
// ============================================================================
UltraNetResult UltraNet_OAuth2AuthorizeViaService(
    const UltraNetOAuth2ServiceConfig& config,
    const std::function<void(const std::string& consentUrl)>& onOpenUrl,
    UltraNetOAuth2Token& outToken,
    int timeoutMs = 300000,
    const std::function<bool()>& isCancelled = {});

// ============================================================================
// The parsers behind the steps, for tests and for a caller that talks to the
// service through another transport.
// ============================================================================
// {"session_id": ..., "authorization_url": ..., "expires_in": N} or the
// service's {"error": ..., "error_description": ...}.
UltraNetResult UltraNet_OAuth2ServiceParseSession(
    const std::string& jsonBody,
    UltraNetOAuth2ServiceSession& outSession);

// {"status": "pending"} -> Ok, outPending = true;
// {"status": "ready", "code": ...} and {"status": "error", "error": ...,
// "error_description": ...} -> Ok, outPending = false, outResult filled;
// anything else -> an error.
UltraNetResult UltraNet_OAuth2ServiceParseStatus(
    const std::string& jsonBody,
    UltraNetOAuth2CallbackResult& outResult,
    bool& outPending);
