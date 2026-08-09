// Apps/UltraSocial/engine/UltraSocialTypes.cpp
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraSocialTypes.h"

#include <cctype>

namespace UltraSocial {

std::string ToString(SocialNetwork network) {
    switch (network) {
        case SocialNetwork::Mastodon: return "mastodon";
        case SocialNetwork::Bluesky:  return "bluesky";
        case SocialNetwork::Telegram: return "telegram";
    }
    return "mastodon";
}

SocialNetwork SocialNetworkFromString(const std::string& s) {
    if (s == "bluesky")  return SocialNetwork::Bluesky;
    if (s == "telegram") return SocialNetwork::Telegram;
    return SocialNetwork::Mastodon;
}

std::string MakeAccountId(SocialNetwork network,
                          const std::string& a,
                          const std::string& b) {
    std::string id = ToString(network);
    for (const std::string* part : {&a, &b}) {
        if (part->empty()) continue;
        id += '-';
        for (unsigned char c : *part) {
            if (std::isalnum(c) || c == '.' || c == '_' || c == '-') {
                id += static_cast<char>(std::tolower(c));
            } else {
                id += '-';
            }
        }
    }
    return id;
}

} // namespace UltraSocial
