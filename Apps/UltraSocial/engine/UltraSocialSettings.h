// Apps/UltraSocial/engine/UltraSocialSettings.h
// The sign-in settings every account of a network shares: the default
// Mastodon instance and Bluesky server, and the app registrations Reddit, X
// and LinkedIn sign in through (client id, and LinkedIn's client secret).
// They used to be typed into the add-account dialog for every account; now
// they come set as standard - built in, overridable at build time with
// -DULTRASOCIAL_<NETWORK>_CLIENT_ID=... - and are edited in the Settings
// dialog behind the gear on the start page. Saved in the credential vault
// (LinkedIn's client secret is a secret), under kSettingsVaultKey.
// Version: 0.1.0 - sign-in settings set as standard, editable in Settings
// Last Modified: 2026-11-14
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraSocialConnector.h"
#include "UltraSocialCredentialVault.h"
#include "UltraSocialTypes.h"

#include <string>

namespace UltraSocial {

// The vault entry the settings are kept under. Account ids are
// "<network>-..." slugs, so this name cannot collide with one.
inline constexpr const char* kSettingsVaultKey = "settings.signin";

struct SignInSettings {
    std::string mastodonInstance;   // used when the add-account form is left empty
    std::string blueskyServer;
    std::string redditClientId;
    std::string xClientId;
    std::string linkedInClientId;
    std::string linkedInClientSecret;

    bool operator==(const SignInSettings&) const = default;
};

// The standard values: what a fresh install signs in with.
SignInSettings DefaultSignInSettings();

// The saved settings, or the standard ones where nothing was saved (a field
// saved empty falls back to its standard value too).
SignInSettings LoadSignInSettings(const CredentialVault& vault);
bool SaveSignInSettings(CredentialVault& vault, const SignInSettings& settings);

// JSON round trip (the vault entry's format).
std::string SignInSettingsToJson(const SignInSettings& settings);
SignInSettings SignInSettingsFromJson(const std::string& json);

// Fill what the add-account form left empty from the settings: the server
// for Mastodon / Bluesky, the client id for Reddit / X / LinkedIn and the
// client secret for LinkedIn. Fields the user filled in are kept.
void ApplySignInSettings(SocialNetwork network, const SignInSettings& settings,
                         AuthInput& input);

// Whether the network needs an app registration (client id) and the
// settings have none yet - the add-account form then asks for it.
bool NeedsClientId(SocialNetwork network, const SignInSettings& settings);

} // namespace UltraSocial
