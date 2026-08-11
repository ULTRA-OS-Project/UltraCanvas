// Apps/UltraSocial/engine/connectors/UltraSocialMastodonConnector.h
// Mastodon (and API-compatible Fediverse servers). Sign-in registers its
// own OAuth client on the instance (POST /api/v1/apps — no pre-registered
// keys) and runs the UltraNetOAuth2 interactive flow; a pasted access token
// in AuthInput.secret skips the browser entirely. Publishing uploads media
// via /api/v2/media and posts through /api/v1/statuses with an
// Idempotency-Key.
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "../UltraSocialConnector.h"

namespace UltraSocial {

class MastodonConnector : public ISocialConnector {
public:
    SocialNetwork Network() const override { return SocialNetwork::Mastodon; }
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
