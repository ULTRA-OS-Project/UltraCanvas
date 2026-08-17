// Apps/UltraSocial/engine/connectors/UltraSocialFacebookConnector.h
// Facebook Pages over the Meta Graph API. Personal profiles have no
// posting API at all (Docs/UltraSocial/Concept.md §1) — this connector
// posts to a Page the user manages. Sign-in is a pasted long-lived Page
// access token + the Page id (from the user's Meta app; dev mode covers
// the user's own Pages, public distribution needs Meta's app review —
// which is exactly why this network is Tier 3). Text goes to
// /{page-id}/feed, a single photo with caption to /{page-id}/photos
// (multipart `source`, so no public URL is needed). Phase 3.
// Version: 0.1.0 (Phase 3)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "../UltraSocialConnector.h"

namespace UltraSocial {

class FacebookConnector : public ISocialConnector {
public:
    SocialNetwork Network() const override { return SocialNetwork::Facebook; }
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
