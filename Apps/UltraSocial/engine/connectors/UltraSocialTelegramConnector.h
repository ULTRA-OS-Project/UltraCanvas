// Apps/UltraSocial/engine/connectors/UltraSocialTelegramConnector.h
// Telegram channels through the Bot API: the user pastes a bot token (from
// @BotFather) and names a channel the bot administers. Text goes out via
// sendMessage, a single photo with caption via sendPhoto (multipart).
// Phase 1 caps media at one image; sendMediaGroup albums are a later
// extension.
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "../UltraSocialConnector.h"

namespace UltraSocial {

class TelegramConnector : public ISocialConnector {
public:
    SocialNetwork Network() const override { return SocialNetwork::Telegram; }
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
