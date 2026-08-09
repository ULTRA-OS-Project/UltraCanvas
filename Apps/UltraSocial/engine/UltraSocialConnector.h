// Apps/UltraSocial/engine/UltraSocialConnector.h
// The per-network connector interface: one implementation per social
// network, each a REST-over-HTTPS client on UltraNet (auth flows through
// UltraNet/UltraNetOAuth2.h where the network uses OAuth). Connectors are
// stateless — account identity and the credential blob travel through every
// call, so one connector instance serves any number of accounts.
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraSocialTypes.h"

#include <UltraNet/UltraNetCore.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraSocial {

// What the account wizard collects before Authenticate. Which fields matter
// depends on the network:
//   Mastodon  server (instance URL); secret optionally a pasted access token
//             (skips the browser flow); otherwise onOpenUrl drives OAuth.
//   Bluesky   server (PDS, default https://bsky.social), identifier (handle
//             or email), secret (app password).
//   Telegram  secret (bot token), identifier (@channelname or chat id).
struct AuthInput {
    std::string server;
    std::string identifier;
    std::string secret;
    // Shows the OAuth consent URL (e.g. UltraCanvas::OpenURL). Only used by
    // OAuth networks and only when no token was pasted.
    std::function<void(const std::string& url)> onOpenUrl;
};

class ISocialConnector {
public:
    virtual ~ISocialConnector() = default;

    virtual SocialNetwork Network() const = 0;
    virtual SocialCapabilities Capabilities() const = 0;

    // Signs the account in and fills outAccount plus the opaque credential
    // blob (a JSON string) the caller stores in the CredentialVault under
    // outAccount.accountId. Blocking — run off the UI thread.
    virtual UltraNetResult Authenticate(const AuthInput& input,
                                        Account& outAccount,
                                        std::string& outCredentialJson) = 0;

    // Non-network checks of an adapted post against this network's rules.
    // Empty result = publishable.
    virtual std::vector<std::string> ValidateDraft(
        const AdaptedPost& post) const = 0;

    // Publishes one adapted post. credentialJson is in-out: a connector that
    // refreshes an expired token rewrites the blob, and the caller persists
    // it back to the vault. Blocking — run off the UI thread.
    virtual UltraNetResult PublishPost(const Account& account,
                                       std::string& credentialJson,
                                       const AdaptedPost& post,
                                       PostResult& outResult) = 0;
};

// Factory over the built-in connectors.
std::shared_ptr<ISocialConnector> CreateConnector(SocialNetwork network);

} // namespace UltraSocial
