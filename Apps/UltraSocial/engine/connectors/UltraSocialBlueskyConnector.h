// Apps/UltraSocial/engine/connectors/UltraSocialBlueskyConnector.h
// Bluesky over the AT Protocol XRPC API. Sign-in creates a session from an
// app password (Settings → App Passwords on the account); publishing uploads
// image blobs and creates an app.bsky.feed.post record. Expired access
// tokens are refreshed transparently with the session's refresh token and
// the rewritten credential blob handed back to the caller for the vault.
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "../UltraSocialConnector.h"

namespace UltraSocial {

class BlueskyConnector : public ISocialConnector {
public:
    SocialNetwork Network() const override { return SocialNetwork::Bluesky; }
    SocialCapabilities Capabilities() const override;

    UltraNetResult Authenticate(const AuthInput& input,
                                Account& outAccount,
                                std::string& outCredentialJson) override;

    std::vector<std::string> ValidateDraft(const AdaptedPost& post) const override;

    UltraNetResult PublishPost(const Account& account,
                               std::string& credentialJson,
                               const AdaptedPost& post,
                               PostResult& outResult) override;
};

} // namespace UltraSocial
