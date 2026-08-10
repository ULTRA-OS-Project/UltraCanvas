// Apps/UltraSocial/engine/connectors/UltraSocialXConnector.h
// X (Twitter) over API v2. Sign-in is the standard OAuth2 code + PKCE flow
// for a public client (the user registers their own app with redirect
// http://127.0.0.1:17996/callback and supplies its client id) through
// UltraNet_OAuth2AuthorizeInteractive; refresh tokens rotate on every
// refresh and the rewritten credential blob flows back to the vault.
// Publishing posts text tweets via POST /2/tweets; media (the chunked
// upload endpoint) is a later extension. Mind the free tier's monthly
// write cap (Docs/UltraSocial/Concept.md §1). Phase 2.
// Version: 0.1.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "../UltraSocialConnector.h"

namespace UltraSocial {

class XConnector : public ISocialConnector {
public:
    SocialNetwork Network() const override { return SocialNetwork::X; }
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
