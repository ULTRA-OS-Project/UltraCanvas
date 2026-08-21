// Apps/UltraSocial/engine/connectors/UltraSocialRedditConnector.h
// Reddit over OAuth2 as an "installed app": the user registers their own
// app (redirect http://127.0.0.1:17995/callback) and supplies its client
// id — Reddit issues no secret to installed apps and does not offer PKCE,
// so the token exchange authenticates with HTTP Basic `clientid:` (empty
// password). Publishing submits self posts via /api/submit: the draft's
// first line becomes the title, the rest the body. Media is a later
// extension (Reddit's upload flow is lease-based). Phase 2.
// Version: 0.1.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "../UltraSocialConnector.h"

namespace UltraSocial {

class RedditConnector : public ISocialConnector {
public:
    SocialNetwork Network() const override { return SocialNetwork::Reddit; }
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
