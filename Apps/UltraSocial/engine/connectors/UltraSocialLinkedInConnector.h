// Apps/UltraSocial/engine/connectors/UltraSocialLinkedInConnector.h
// LinkedIn member posts over the versioned REST API. The user registers
// their own app (products "Sign In with LinkedIn using OpenID Connect" +
// "Share on LinkedIn", redirect http://127.0.0.1:17997/callback) and
// supplies its client id + client secret — LinkedIn issues confidential
// clients only, and its token endpoint wants the secret in the form body.
// Publishing creates text posts via POST /rest/posts (the new post's URN
// arrives in the x-restli-id response header). Token refresh runs when the
// server granted a refresh token (programmatic refresh is enabled per app).
// Media (the initializeUpload flow) is a later extension. Phase 3.
// Version: 0.1.0 (Phase 3)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "../UltraSocialConnector.h"

namespace UltraSocial {

class LinkedInConnector : public ISocialConnector {
public:
    SocialNetwork Network() const override { return SocialNetwork::LinkedIn; }
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
