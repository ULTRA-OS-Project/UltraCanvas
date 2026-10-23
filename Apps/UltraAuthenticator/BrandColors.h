// Apps/UltraAuthenticator/BrandColors.h
// The colour of a well-known service, for the monogram on its card.
//
// A card cannot show the service's logo: logos are trademarks, and shipping
// several hundred of them is a licence to clear for every one. What a card
// shows instead is the first letter of the name on a circle, and for the
// services people most often protect with a second factor that circle takes
// the service's own colour — GitHub's near-black, Dropbox's blue, AWS's
// orange — so the card is recognised before its name is read. A colour
// identifies the service the account belongs to; it is not a mark the app
// claims as its own.
//
// The table holds the primary colour each company publishes in its brand
// guidelines. A service that is not in it falls back to a colour picked from
// its name (Theme::MonogramColor), which is stable but not the brand's.
//
// Matching ignores case and everything but letters and digits, so "Amazon Web
// Services", "amazon-web-services" and "AmazonWebServices" are one name. A
// key also matches a longer name it starts with ("google" matches "Google
// Workspace"), the longest such key winning, so "Amazon Web Services" is AWS
// and not Amazon. Keys shorter than five characters ("x", "aws", "meta",
// "zoom") match only exactly, or "x" would claim every name beginning with x
// and "meta" would paint MetaMask in Facebook's blue.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once
#ifndef AUTHENTICATOR_BRANDCOLORS_H
#define AUTHENTICATOR_BRANDCOLORS_H

#include "UltraCanvasCommonTypes.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

namespace UltraCanvas {
namespace Authenticator {

struct BrandColor {
    const char* key;      // normalised: lower-case letters and digits only
    uint32_t    rgb;      // 0xRRGGBB
};

// Grouped loosely by kind; order does not matter (the longest match wins).
inline constexpr BrandColor kBrandColors[] = {
    // --- developer and cloud ---
    {"github",            0x181717},
    {"gitlab",            0xFC6D26},
    {"bitbucket",         0x0052CC},
    {"atlassian",         0x0052CC},
    {"jira",              0x0052CC},
    {"trello",            0x0052CC},
    {"amazonwebservices", 0xFF9900},
    {"aws",               0xFF9900},
    {"azure",             0x0078D4},
    {"cloudflare",        0xF38020},
    {"digitalocean",      0x0080FF},
    {"heroku",            0x430098},
    {"vercel",            0x000000},
    {"netlify",           0x00C7B7},
    {"docker",            0x2496ED},
    {"npm",               0xCB3837},
    {"hetzner",           0xD50C2D},
    {"backblaze",         0xE21E29},
    {"sentry",            0x362D59},
    {"twilio",            0xF22F46},
    {"sendgrid",          0x1A82E2},
    {"okta",              0x007DC1},
    {"ibm",               0x0F62FE},
    {"oracle",            0xC74634},
    {"salesforce",        0x00A1E0},
    {"hubspot",           0xFF7A59},
    {"figma",             0xF24E1E},
    {"notion",            0x000000},
    {"slack",             0x4A154B},
    {"zoom",              0x0B5CFF},
    {"wordpress",         0x21759B},
    {"namecheap",         0xDE3723},
    {"nextcloud",         0x0082C9},
    // --- accounts, mail and storage ---
    {"google",            0x4285F4},
    {"gmail",             0xEA4335},
    {"microsoft",         0x00A4EF},
    {"outlook",           0x0078D4},
    {"apple",             0x000000},
    {"amazon",            0xFF9900},
    {"yahoo",             0x6001D2},
    {"proton",            0x6D4AFF},
    {"protonmail",        0x6D4AFF},
    {"dropbox",           0x0061FF},
    {"box",               0x0061D5},
    {"evernote",          0x00A82D},
    {"adobe",             0xFA0F00},
    {"zoho",              0xE42527},
    {"mailchimp",         0xFFE01B},
    // --- password managers ---
    {"bitwarden",         0x175DDC},
    {"1password",         0x0572EC},
    {"lastpass",          0xD32D27},
    // --- social ---
    {"facebook",          0x0866FF},
    {"meta",              0x0866FF},
    {"instagram",         0xE4405F},
    {"whatsapp",          0x25D366},
    {"twitter",           0x000000},
    {"x",                 0x000000},
    {"linkedin",          0x0A66C2},
    {"reddit",            0xFF4500},
    {"discord",           0x5865F2},
    {"telegram",          0x0088CC},
    {"mastodon",          0x6364FF},
    {"tumblr",            0x36465D},
    {"pinterest",         0xE60023},
    {"snapchat",          0xFFFC00},
    {"tiktok",            0x000000},
    {"twitch",            0x9146FF},
    {"patreon",           0xFF424D},
    {"spotify",           0x1DB954},
    // --- games ---
    {"steam",             0x1B2838},
    {"epicgames",         0x313131},
    {"nintendo",          0xE60012},
    {"playstation",       0x003791},
    {"xbox",              0x107C10},
    {"battlenet",         0x148EFF},
    {"blizzard",          0x148EFF},
    // --- money ---
    {"paypal",            0x003087},
    {"stripe",            0x635BFF},
    {"wise",              0x9FE870},
    {"coinbase",          0x0052FF},
    {"binance",           0xF0B90B},
    {"kraken",            0x5741D9},
    {"gemini",            0x00DCFA},
    {"cryptocom",         0x002D74},
    {"shopify",           0x95BF47},
    {"ebay",              0xE53238},
    {"airbnb",            0xFF5A5F},
    {"uber",              0x000000},
};

// Lower-case ASCII letters and digits only; everything else is dropped.
inline std::string NormaliseBrandName(const std::string& name) {
    std::string out;
    out.reserve(name.size());
    for (unsigned char c : name) {
        if (c >= 'A' && c <= 'Z') out.push_back(static_cast<char>(c - 'A' + 'a'));
        else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out.push_back(static_cast<char>(c));
    }
    return out;
}

// The brand colour for `name`, or false when it is not a known service.
inline bool FindBrandColor(const std::string& name, Color& out) {
    const std::string normalised = NormaliseBrandName(name);
    if (normalised.empty()) return false;
    const BrandColor* best = nullptr;
    size_t bestLength = 0;
    for (const BrandColor& brand : kBrandColors) {
        const std::string key(brand.key);
        const bool exact  = normalised == key;
        const bool prefix = key.size() >= 5 && normalised.size() > key.size() &&
                            normalised.compare(0, key.size(), key) == 0;
        if ((exact || prefix) && key.size() > bestLength) {
            best = &brand;
            bestLength = key.size();
        }
    }
    if (!best) return false;
    out = Color(static_cast<uint8_t>((best->rgb >> 16) & 0xFF),
                static_cast<uint8_t>((best->rgb >> 8) & 0xFF),
                static_cast<uint8_t>(best->rgb & 0xFF), 255);
    return true;
}

// White or near-black for a letter on `background`, whichever reads better.
// Several brand colours are light (Snapchat's yellow, Wise's green, AWS's
// orange), and white on them is unreadable; WCAG's relative luminance
// decides, with 3:1 — the bar for large text, which the monogram letter is —
// as the point at which white is kept.
inline Color MonogramInkFor(const Color& background) {
    auto channel = [](uint8_t v) {
        const double c = v / 255.0;
        return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    const double luminance = 0.2126 * channel(background.r) +
                             0.7152 * channel(background.g) +
                             0.0722 * channel(background.b);
    const double whiteContrast = 1.05 / (luminance + 0.05);
    return whiteContrast >= 3.0 ? Color(255, 255, 255, 255) : Color(24, 28, 35, 255);
}

} // namespace Authenticator
} // namespace UltraCanvas

#endif // AUTHENTICATOR_BRANDCOLORS_H
