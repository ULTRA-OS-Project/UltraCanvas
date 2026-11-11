// Apps/UltraDBAdmin/relay/RelaySignIn.h
// The relay's sign-in with an OpenID Connect provider - a company's
// Keycloak or Entra ID, Google, the ULTRA account once there is one - beside
// the tokens an administrator hands out (RelayConfig.h, [signin]).
//
// The relay is the provider's client, so no provider token ever reaches the
// browser:
//
//   page ──GET /relay/signin?return=<page>──► relay ──302──► provider's sign-in
//   provider ──302 /relay/callback?code&state──► relay ──code + PKCE verifier──► provider's token endpoint
//   relay: the ID token's signature checked against the provider's published
//          keys (RS256, ES256), its issuer, audience, expiry and nonce; the
//          user found by verified email or subject
//   relay ──302 <page>#relay_ticket=<ticket>──► page ── hello {token: ticket} ──► relay
//
// A ticket is random, kept only as its SHA-256, and good for ticket_hours.
// A desktop app returns to a loopback address of its own instead
// (http://127.0.0.1:<port>/...) and sends a state of its own: the ticket then
// comes as ?code=<ticket>&state=<its state>, as a loopback OAuth redirect does
// (UltraNet_OAuth2WaitForCallback).
// Where the browser may go back to is checked: public_url's own origin, the
// configured origins, or a loopback address - nowhere else, so a ticket is
// never handed to another site.
//
// The provider is read from its discovery document
// (<issuer>/.well-known/openid-configuration) at start-up; its keys again
// when a token names one not known yet.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "RelayConfig.h"
#include "RelayWebSocket.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace UltraDBAdminRelay {

// ----- the pieces, on their own for the tests -----

std::string Base64UrlEncode(const std::string& bytes);
bool Base64UrlDecode(const std::string& text, std::string& bytes);

// A provider's published signing keys (a JWKS document): RSA and P-256.
class PublicKeys {
public:
    PublicKeys();
    ~PublicKeys();
    PublicKeys(PublicKeys&&) noexcept;
    PublicKeys& operator=(PublicKeys&&) noexcept;
    // Reads {"keys":[...]}; keys not for signatures, or of another kind, are
    // left out. False when none is left.
    bool Parse(const std::string& jwks, std::string& error);
    size_t Size() const;
    bool Has(const std::string& kid) const;
    // Is `signature` the signature of `data` by key `kid` ("" when the
    // provider has one key and names none) under `alg` (RS256, ES256)?
    bool Verify(const std::string& kid, const std::string& alg, const std::string& data,
                const std::string& signature, std::string& error) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Who an ID token says signed in.
struct Identity {
    std::string subject;
    std::string email;          // in lower case
    bool emailVerified = false;
};

struct TokenExpectations {
    std::string issuer;
    std::string clientId;
    std::string nonce;
    int64_t now = 0;            // seconds since 1970
};

// The key id an ID token's header names ("" when none).
std::string IdTokenKeyId(const std::string& jwt);

// Checks an ID token: its signature by one of `keys`, and that it is from
// the issuer, for this client, not expired, and for this sign-in (nonce).
bool VerifyIdToken(const std::string& jwt, const PublicKeys& keys, const TokenExpectations& expect, Identity& out,
                   std::string& error);

// The user `identity` is: the one whose subject it is, or whose email it is
// when the provider verified that email; "" for nobody.
std::string UserFor(const Identity& identity, const std::map<std::string, UserIdentity>& identities);

// "https://a.example:8443" of a URL.
std::string OriginOf(const std::string& url);

// May the browser be sent back to `returnUrl` with a ticket?
bool ReturnAllowed(const std::string& returnUrl, const std::string& publicUrl, const std::vector<std::string>& origins);

// ----- the sign-in -----

class RelaySignIn {
public:
    // `clientSecret`: from the relay's vault, "" for a public client.
    // `log`: one line for each sign-in, signed in (`ok`) or refused (the audit log).
    RelaySignIn(const RelayConfig& config, std::string clientSecret,
                std::function<void(const std::string& peer, const std::string& line, bool ok)> log = {});

    // Reads the provider's discovery document and keys; false with `error`.
    bool Start(std::string& error);

    // The provider, as discovered (for --check and the tests).
    std::string AuthorizationEndpoint() const;
    std::string TokenEndpoint() const;

    // GET <path>/signin and <path>/callback; false for any other path.
    bool Handle(const HttpRequest& request, HttpReply& reply);

    // The user a ticket stands for, while it is good.
    bool Ticket(const std::string& ticket, std::string& user);

private:
    struct Pending {
        std::string verifier;
        std::string nonce;
        std::string returnUrl;
        std::string clientState;
        std::chrono::steady_clock::time_point created;
    };
    struct Granted {
        std::string user;
        std::chrono::system_clock::time_point expires;
    };

    bool SignIn(const HttpRequest& request, HttpReply& reply);
    bool Callback(const HttpRequest& request, HttpReply& reply);
    bool RefreshKeys(std::string& error);

    RelayConfig config_;
    std::string clientSecret_;
    std::function<void(const std::string&, const std::string&, bool)> log_;
    mutable std::mutex m_;
    std::string authorizationEndpoint_, tokenEndpoint_, jwksUri_;
    PublicKeys keys_;
    std::chrono::steady_clock::time_point keysRead_;
    std::map<std::string, Pending> pending_;            // by state
    std::map<std::string, Granted> tickets_;            // by the ticket's SHA-256
};

} // namespace UltraDBAdminRelay
