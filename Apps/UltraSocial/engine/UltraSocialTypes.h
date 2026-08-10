// Apps/UltraSocial/engine/UltraSocialTypes.h
// Core data types for the UltraSocial engine: networks, accounts, drafts,
// per-network adapted posts, capabilities, and publish results. Design in
// Docs/UltraSocial/Concept.md.
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace UltraSocial {

// Supported networks (Docs/UltraSocial/Concept.md §1 — Tier 1 in phase 1,
// Tier 2 in phase 2). Persisted as the ToString slug, never the int value.
enum class SocialNetwork {
    Mastodon = 0,   // and API-compatible Fediverse servers
    Bluesky,
    Telegram,
    Reddit,
    X
};

std::string   ToString(SocialNetwork network);
SocialNetwork SocialNetworkFromString(const std::string& s);

// Stable account slug "<network>-<a>-<b>": lowercased, with anything
// outside [a-z0-9._-] replaced by '-'. Used as the vault/store key.
std::string MakeAccountId(SocialNetwork network,
                          const std::string& a,
                          const std::string& b);

// A connected account as the store knows it (no secrets here — the
// credential blob lives in the CredentialVault under accountId).
struct Account {
    std::string   accountId;    // stable slug, e.g. "mastodon-mastodon.social-erika"
    SocialNetwork network = SocialNetwork::Mastodon;
    std::string   displayName;  // "Erika Example"
    std::string   handle;       // "@erika@mastodon.social", "erika.bsky.social", "@channel"
    std::string   server;       // instance / PDS / API base URL
};

// One attached media file. Bytes are read from filePath at publish time.
struct MediaAttachment {
    std::string filePath;
    std::string mimeType;   // empty = guessed from the file extension
    std::string altText;
};

// What the user wrote once, before per-network adaptation.
struct PostDraft {
    std::string text;
    std::vector<MediaAttachment> media;
    std::string visibility = "public";  // Mastodon: public/unlisted/private
};

// A network-ready variant of the draft, produced by the Composer for one
// network and validated against that network's capabilities.
struct AdaptedPost {
    SocialNetwork network = SocialNetwork::Mastodon;
    std::string text;
    std::vector<MediaAttachment> media;
    std::string visibility = "public";
};

// What a network can take — drives adaptation, validation, and the live
// counters in the compose UI. Values for the built-in connectors follow
// Docs/UltraSocial/Concept.md §1.
struct SocialCapabilities {
    int     maxTextChars = 0;       // 0 = unlimited (counted in code points)
    int     maxMediaCaptionChars = 0; // 0 = same as maxTextChars
    int     maxImages = 0;          // 0 = media not supported on this network
    int64_t maxImageBytes = 0;      // 0 = unlimited
    int     maxAltTextChars = 0;    // 0 = alt text unsupported
    bool    supportsVisibility = false;
};

// Outcome of one successful publish.
struct PostResult {
    std::string accountId;
    SocialNetwork network = SocialNetwork::Mastodon;
    std::string postId;   // network-native id (status id / at:// uri / message id)
    std::string url;      // human-viewable permalink (empty if not derivable)
};

} // namespace UltraSocial
