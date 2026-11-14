// Apps/UltraSocial/engine/connectors/UltraSocialDiscordConnector.h
// Discord channels through a channel webhook: the user creates one under
// the channel's Settings -> Integrations -> Webhooks and pastes its URL - no
// sign-in, no app registration. Signing in fetches the webhook (GET on its
// URL), which proves it exists and names it; posting is POST
// <url>?wait=true with {"content": ...}, or multipart with payload_json and
// files[n] when images are attached (up to 10). The webhook posts under its
// own name and avatar, not the user's - Discord allows no posting as a user.
// Version: 0.1.0 - Discord via a channel webhook
// Last Modified: 2026-11-14
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "../UltraSocialConnector.h"

namespace UltraSocial {

class DiscordConnector : public ISocialConnector {
public:
    SocialNetwork Network() const override { return SocialNetwork::Discord; }
    SocialCapabilities Capabilities() const override;

    // `input.secret` is the webhook URL (https://discord.com/api/webhooks/<id>/<token>).
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
