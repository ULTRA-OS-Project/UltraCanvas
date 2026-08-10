// Apps/UltraSocial/engine/UltraSocialConnector.cpp
// Factory over the built-in connectors.
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraSocialConnector.h"

#include "connectors/UltraSocialBlueskyConnector.h"
#include "connectors/UltraSocialMastodonConnector.h"
#include "connectors/UltraSocialRedditConnector.h"
#include "connectors/UltraSocialTelegramConnector.h"
#include "connectors/UltraSocialXConnector.h"

namespace UltraSocial {

std::shared_ptr<ISocialConnector> CreateConnector(SocialNetwork network) {
    switch (network) {
        case SocialNetwork::Mastodon:
            return std::make_shared<MastodonConnector>();
        case SocialNetwork::Bluesky:
            return std::make_shared<BlueskyConnector>();
        case SocialNetwork::Telegram:
            return std::make_shared<TelegramConnector>();
        case SocialNetwork::Reddit:
            return std::make_shared<RedditConnector>();
        case SocialNetwork::X:
            return std::make_shared<XConnector>();
    }
    return nullptr;
}

} // namespace UltraSocial
