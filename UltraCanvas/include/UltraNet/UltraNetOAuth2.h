// include/UltraNet/UltraNetOAuth2.h
// OAuth 2.0 authorization-code flow with PKCE (RFC 6749 + RFC 7636) for
// native/desktop apps: build the consent URL, catch the redirect on a
// loopback listener (RFC 8252), exchange the code, refresh tokens. The
// building blocks are exposed individually; UltraNet_OAuth2AuthorizeInteractive
// runs the whole dance in one blocking call.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraNetCore.h"

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

// ============================================================================
// PKCE (RFC 7636). GeneratePkce produces a fresh high-entropy code_verifier
// and its S256 code_challenge. One pair per authorization attempt — never
// reuse a verifier.
// ============================================================================
struct UltraNetOAuth2Pkce {
    std::string codeVerifier;   // 43–128 chars from the unreserved alphabet
    std::string codeChallenge;  // base64url(SHA-256(codeVerifier)), no padding
    std::string method = "S256";

    bool IsValid() const { return !codeVerifier.empty() && !codeChallenge.empty(); }
};

UltraNetOAuth2Pkce UltraNet_OAuth2GeneratePkce();

// S256 challenge for an existing verifier: base64url(SHA-256(codeVerifier)).
// For callers that persist a verifier across a restart mid-flow.
std::string UltraNet_OAuth2ChallengeFromVerifier(const std::string& codeVerifier);

// Random URL-safe state token for CSRF protection of the redirect.
std::string UltraNet_OAuth2GenerateState(int entropyBytes = 32);

// ============================================================================
// Provider + client description. A desktop app is a *public* client: leave
// clientSecret empty and keep usePkce on (the default). Fill clientSecret
// only for confidential-client providers that insist on one.
// ============================================================================
struct UltraNetOAuth2Config {
    std::string authorizationEndpoint;  // e.g. "https://host/oauth/authorize"
    std::string tokenEndpoint;          // e.g. "https://host/oauth/token"
    std::string clientId;
    std::string clientSecret;           // empty = public client
    std::vector<std::string> scopes;    // joined with ' ' into the scope param
    // Must match the provider-side registration. For the interactive flow use
    // a loopback URI, e.g. "http://127.0.0.1:8571/callback"; port 0 picks an
    // ephemeral port at listen time (for providers that allow any loopback
    // port, per RFC 8252 §7.3).
    std::string redirectUri;
    // Extra query params for the consent URL (e.g. {"access_type","offline"}).
    std::map<std::string, std::string> extraAuthParams;
    // Extra form fields for the token endpoint.
    std::map<std::string, std::string> extraTokenParams;
    bool usePkce = true;
    // A non-empty clientSecret is sent as HTTP Basic by default (RFC 6749
    // §2.3.1); set true for providers that only accept it in the form body.
    bool secretInBody = false;
};

// ============================================================================
// Token-endpoint response. expiresInSeconds is the server-reported lifetime
// (0 if the server didn't say) — the caller tracks the wall-clock deadline.
// ============================================================================
struct UltraNetOAuth2Token {
    std::string accessToken;
    std::string tokenType;              // usually "Bearer"
    std::string refreshToken;           // empty if the server issued none
    std::string scope;                  // granted scope, if reported
    std::string idToken;                // OpenID Connect id_token, if present
    int64_t expiresInSeconds = 0;
    std::string raw;                    // full response body for extra fields

    bool IsValid() const { return !accessToken.empty(); }
};

// ============================================================================
// Step 1 — consent URL. Appends response_type/client_id/redirect_uri/scope/
// state and the PKCE challenge (when pkce.IsValid()) to authorizationEndpoint.
// Open it in the system browser (e.g. UltraCanvas::OpenURL); never embed the
// provider's login page in an in-app view.
// ============================================================================
std::string UltraNet_OAuth2BuildAuthUrl(
    const UltraNetOAuth2Config& config,
    const UltraNetOAuth2Pkce& pkce,
    const std::string& state);

// ============================================================================
// Step 2 — redirect capture. Serves one HTTP request on the loopback
// interface and returns the query parameters the provider redirected with.
// Blocking — run off the UI thread. The redirect URI must be
// http://127.0.0.1:<port>/<path> (or localhost / [::1]); the listener binds
// to the loopback interface only. responseHtml is shown in the browser tab
// afterwards; empty = a built-in "you can close this window" page.
// ============================================================================
struct UltraNetOAuth2CallbackResult {
    std::string code;
    std::string state;
    std::string error;              // OAuth error code, e.g. "access_denied"
    std::string errorDescription;

    bool HasCode() const { return !code.empty() && error.empty(); }
};

UltraNetResult UltraNet_OAuth2WaitForCallback(
    const std::string& redirectUri,
    UltraNetOAuth2CallbackResult& outResult,
    int timeoutMs = 300000,
    const std::string& responseHtml = std::string());

// ============================================================================
// Step 3 — token exchange / refresh. codeVerifier must be the pkce.codeVerifier
// that produced the challenge in the consent URL (empty when PKCE unused).
// On an HTTP-level failure the result carries the server's error /
// error_description in message and the status in httpStatus.
// ============================================================================
UltraNetResult UltraNet_OAuth2ExchangeCode(
    const UltraNetOAuth2Config& config,
    const std::string& code,
    const std::string& codeVerifier,
    UltraNetOAuth2Token& outToken);

UltraNetResult UltraNet_OAuth2Refresh(
    const UltraNetOAuth2Config& config,
    const std::string& refreshToken,
    UltraNetOAuth2Token& outToken);

// Parses a token-endpoint JSON body (success or error shape) into outToken.
// Exposed for callers talking to nonstandard endpoints and for tests.
UltraNetResult UltraNet_OAuth2ParseTokenResponse(
    const std::string& jsonBody,
    UltraNetOAuth2Token& outToken);

// ============================================================================
// One-call orchestration: listen on the loopback redirect (resolving port 0
// to a real ephemeral port first), generate state + PKCE, hand the consent
// URL to onOpenUrl, await the redirect, verify state, exchange the code.
// Blocking for up to timeoutMs — run on a worker thread, never the UI thread.
// ============================================================================
UltraNetResult UltraNet_OAuth2AuthorizeInteractive(
    const UltraNetOAuth2Config& config,
    const std::function<void(const std::string& consentUrl)>& onOpenUrl,
    UltraNetOAuth2Token& outToken,
    int timeoutMs = 300000);
