// Apps/UltraSocial/engine/UltraSocialSettings.cpp
// Version: 0.1.0 - sign-in settings set as standard, editable in Settings
// Last Modified: 2026-11-14
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraSocialSettings.h"

#include "DataFormats/UltraCanvasJSON.h"

// The app registrations a build ships with. Empty unless the build passes
// its own (CMake: ULTRASOCIAL_REDDIT_CLIENT_ID and friends); an empty one is
// asked for once, in Settings or the add-account form.
#ifndef ULTRASOCIAL_REDDIT_CLIENT_ID
#define ULTRASOCIAL_REDDIT_CLIENT_ID ""
#endif
#ifndef ULTRASOCIAL_X_CLIENT_ID
#define ULTRASOCIAL_X_CLIENT_ID ""
#endif
#ifndef ULTRASOCIAL_LINKEDIN_CLIENT_ID
#define ULTRASOCIAL_LINKEDIN_CLIENT_ID ""
#endif
#ifndef ULTRASOCIAL_LINKEDIN_CLIENT_SECRET
#define ULTRASOCIAL_LINKEDIN_CLIENT_SECRET ""
#endif

using UltraCanvas::JSONValue;

namespace UltraSocial {

namespace {

void FillEmpty(std::string& field, const std::string& fallback) {
    if (field.empty()) field = fallback;
}

void FillEmptyFrom(SignInSettings& s, const SignInSettings& d) {
    FillEmpty(s.mastodonInstance,     d.mastodonInstance);
    FillEmpty(s.blueskyServer,        d.blueskyServer);
    FillEmpty(s.redditClientId,       d.redditClientId);
    FillEmpty(s.xClientId,            d.xClientId);
    FillEmpty(s.linkedInClientId,     d.linkedInClientId);
    FillEmpty(s.linkedInClientSecret, d.linkedInClientSecret);
}

} // namespace

SignInSettings DefaultSignInSettings() {
    SignInSettings s;
    s.mastodonInstance     = "mastodon.social";
    s.blueskyServer        = "bsky.social";
    s.redditClientId       = ULTRASOCIAL_REDDIT_CLIENT_ID;
    s.xClientId            = ULTRASOCIAL_X_CLIENT_ID;
    s.linkedInClientId     = ULTRASOCIAL_LINKEDIN_CLIENT_ID;
    s.linkedInClientSecret = ULTRASOCIAL_LINKEDIN_CLIENT_SECRET;
    return s;
}

std::string SignInSettingsToJson(const SignInSettings& s) {
    JSONValue doc = JSONValue::MakeObject()
        .Set("mastodon_instance",      s.mastodonInstance)
        .Set("bluesky_server",         s.blueskyServer)
        .Set("reddit_client_id",       s.redditClientId)
        .Set("x_client_id",            s.xClientId)
        .Set("linkedin_client_id",     s.linkedInClientId)
        .Set("linkedin_client_secret", s.linkedInClientSecret);
    return UltraCanvas::JSON::Serialize(doc);
}

SignInSettings SignInSettingsFromJson(const std::string& json) {
    SignInSettings s;
    if (json.empty()) return s;
    JSONValue doc = UltraCanvas::JSON::Parse(json);
    if (!doc.IsObject()) return s;
    s.mastodonInstance     = doc["mastodon_instance"].GetString();
    s.blueskyServer        = doc["bluesky_server"].GetString();
    s.redditClientId       = doc["reddit_client_id"].GetString();
    s.xClientId            = doc["x_client_id"].GetString();
    s.linkedInClientId     = doc["linkedin_client_id"].GetString();
    s.linkedInClientSecret = doc["linkedin_client_secret"].GetString();
    return s;
}

SignInSettings LoadSignInSettings(const CredentialVault& vault) {
    std::string json;
    SignInSettings s;
    if (vault.Retrieve(kSettingsVaultKey, json)) s = SignInSettingsFromJson(json);
    FillEmptyFrom(s, DefaultSignInSettings());
    return s;
}

bool SaveSignInSettings(CredentialVault& vault, const SignInSettings& settings) {
    return vault.Store(kSettingsVaultKey, SignInSettingsToJson(settings));
}

void ApplySignInSettings(SocialNetwork network, const SignInSettings& s,
                         AuthInput& input) {
    switch (network) {
        case SocialNetwork::Mastodon:
            FillEmpty(input.server, s.mastodonInstance);
            break;
        case SocialNetwork::Bluesky:
            FillEmpty(input.server, s.blueskyServer);
            break;
        case SocialNetwork::Reddit:
            FillEmpty(input.clientId, s.redditClientId);
            break;
        case SocialNetwork::X:
            FillEmpty(input.clientId, s.xClientId);
            break;
        case SocialNetwork::LinkedIn:
            FillEmpty(input.clientId, s.linkedInClientId);
            FillEmpty(input.secret,   s.linkedInClientSecret);
            break;
        default:
            break;
    }
}

bool NeedsClientId(SocialNetwork network, const SignInSettings& s) {
    switch (network) {
        case SocialNetwork::Reddit:   return s.redditClientId.empty();
        case SocialNetwork::X:        return s.xClientId.empty();
        case SocialNetwork::LinkedIn: return s.linkedInClientId.empty() ||
                                             s.linkedInClientSecret.empty();
        default:                      return false;
    }
}

} // namespace UltraSocial
