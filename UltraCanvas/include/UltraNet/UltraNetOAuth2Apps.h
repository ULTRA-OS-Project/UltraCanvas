// include/UltraNet/UltraNetOAuth2Apps.h
// The OAuth2 *app registrations* of a process — the client id, optional secret
// and redirect URI an application signs in as, per provider — in one registry
// shared by every module and application that runs an OAuth2 flow.
//
// UltraMail (Gmail, Outlook) and UltraCloud (Dropbox, OneDrive, Google Drive)
// each used to carry their own copy of this lookup with different priority
// chains and different environment variable names, so a Google client
// registered for mail was invisible to the cloud dialog two menus away. The
// registry here is the one place; the modules keep thin profiles over it —
// their environment prefix, their INI file, their built-in defaults, their
// redirect URI — the way UltraMail's and UltraSocial's credential vaults became
// profiles of UltraVault::DeviceKeyVault.
//
// Lookup order for UltraNet_OAuth2GetApp(providerId), highest priority first
// (a tier is taken whole — its client id, secret and redirect URI together —
// and a tier that names no client id is skipped):
//
//   1. UltraNet_OAuth2SetApp()                              code, at run time
//   2. the environment: <PREFIX><PROVIDER>_CLIENT_ID, _CLIENT_SECRET,
//      _REDIRECT_URI, for every registered prefix in registration order —
//      "ULTRANET_OAUTH_" is always registered; UltraMail adds "ULTRAMAIL_",
//      UltraCloud "ULTRACLOUD_", so their documented names keep working.
//      <PROVIDER> is the id upper-cased with every non-alphanumeric
//      character replaced by '_'.
//   3. an INI file loaded with UltraNet_OAuth2LoadAppsFile(): one section per
//      provider id, keys client_id / client_secret / redirect_uri.
//   4. UltraNet_OAuth2SetBuiltInApp()                       a shipped build's
//      baked-in client, registered once at start-up by the module that
//      carries it.
//
// When nothing names the provider, its *alias* is tried through the same
// tiers: UltraNet_OAuth2SetAppAlias("googledrive", "google") lets one Google
// registration serve Gmail and Drive, since the consent screen of one client
// can carry both scopes. A registration under the specific id always wins
// over one under the alias, at every tier.
//
// The redirect URI is returned as registered (possibly empty): every consumer
// has its own loopback default and fills it in — Google and Microsoft ignore
// the port of a loopback URI, other providers want the registered one.
//
// Beside the apps, the registry knows the *sign-in service*
// (UltraNetOAuth2Service.h): the web service a provider without a client of
// its own signs in through, which holds the registrations and receives the
// providers' redirects. UltraNet_OAuth2SignInServiceUrl() resolves it through
// the same tiers - UltraNet_OAuth2SetSignInService() > the environment
// (<PREFIX>OAUTH_SERVICE_URL for every registered prefix, so
// ULTRANET_OAUTH_SERVICE_URL, ULTRAMAIL_OAUTH_SERVICE_URL,
// ULTRACLOUD_OAUTH_SERVICE_URL) > "[service]\nurl = ..." in the INI file >
// the address built in (the ULTRANET_OAUTH_SERVICE_URL CMake option, by
// default ULTRA OS's https://oauth-callback.ultraos.eu, or what
// UltraNet_OAuth2SetBuiltInSignInService() set). The value "off" (or "none")
// at any tier turns the service off. A consumer decides for itself whether
// a provider goes through the service: UltraMail and UltraCloud send a
// provider there when no app names it.
//
// Thread-safe; nothing throws. A desktop OAuth client's id and secret are not
// confidential (the flow's safety is PKCE + loopback + the user's consent), so
// this is configuration, not a secret store: tokens go to UltraVault.
// Version: 0.2.0 - the sign-in service is a setting of the registry
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <string>
#include <vector>

// The OAuth client an application signs in as, for one provider.
struct UltraNetOAuth2App {
    std::string clientId;
    std::string clientSecret;   // empty = public client (PKCE only)
    std::string redirectUri;    // empty = the consumer's default loopback URI

    bool IsConfigured() const { return !clientId.empty(); }
};

// ---- Registrations ----------------------------------------------------------

// Tier 1: register (or replace) the client for a provider from code. An app
// without a client id is stored but never returned — it does not hide the
// lower tiers.
void UltraNet_OAuth2SetApp(const std::string& providerId, const UltraNetOAuth2App& app);

// Tier 4: the client baked into this build for a provider. A module that
// carries compile-time defaults registers them once; unconfigured apps are
// ignored so a build without credentials registers nothing.
void UltraNet_OAuth2SetBuiltInApp(const std::string& providerId, const UltraNetOAuth2App& app);

// Tier 2: add an environment variable prefix to consult, e.g. "ULTRAMAIL_".
// "ULTRANET_OAUTH_" is always first; a prefix added twice is kept once, in
// its first position.
void UltraNet_OAuth2AddAppEnvPrefix(const std::string& prefix);
std::vector<std::string> UltraNet_OAuth2AppEnvPrefixes();

// The environment variable name a tier-2 lookup reads for a provider under a
// prefix and suffix ("_CLIENT_ID", "_CLIENT_SECRET", "_REDIRECT_URI"), for
// messages that tell the user what to set.
std::string UltraNet_OAuth2AppEnvName(const std::string& prefix, const std::string& providerId,
                                      const std::string& suffix);

// Declare that `providerId` may fall back to `fallbackId`'s registration when
// it has none of its own. Chains are followed (a → b → c); a cycle stops at
// the first repeated id. An empty fallback removes the alias.
void UltraNet_OAuth2SetAppAlias(const std::string& providerId, const std::string& fallbackId);

// Tier 3: parse INI text ("[google]\nclient_id = ...\nclient_secret = ...\n
// redirect_uri = ...") into the file tier. Section names are provider ids,
// case-insensitive; '#' and ';' start comments; a section without a client id
// is ignored. "[service]\nurl = ..." sets the file tier of the sign-in
// service. Returns the number of providers registered. Later loads replace
// a provider's earlier file entry.
int UltraNet_OAuth2ParseAppsIni(const std::string& text);
// Read and parse a file. A missing or unreadable file is not an error: 0.
int UltraNet_OAuth2LoadAppsFile(const std::string& path);

// ---- Lookup -----------------------------------------------------------------

// The client to sign in as, through the tiers and then the alias chain.
// Unconfigured (empty clientId) when nothing names the provider.
UltraNetOAuth2App UltraNet_OAuth2GetApp(const std::string& providerId);
bool UltraNet_OAuth2HasApp(const std::string& providerId);

// Forget every Set() registration and every file entry, the sign-in
// service's included. The environment, the built-in defaults, the prefixes
// and the aliases stay — they describe the build and the process, not a
// session. For tests.
void UltraNet_OAuth2ClearApps();

// ---- The sign-in service ----------------------------------------------------

// Where the sign-in service is, through the tiers in the header comment: the
// address without a trailing '/', or "" when there is none or it is off.
std::string UltraNet_OAuth2SignInServiceUrl();
// Tier 1: name the service from code (a settings page, tests). "" drops the
// override; "off" turns the service off whatever the other tiers say.
void UltraNet_OAuth2SetSignInService(const std::string& url);
// Tier 4: replace the address built into this UltraNet (the
// ULTRANET_OAUTH_SERVICE_URL CMake option) for the process - a module or
// application that carries its own default. "" = no built-in service.
void UltraNet_OAuth2SetBuiltInSignInService(const std::string& url);
// The environment variable name a tier-2 lookup reads under a prefix:
// "ULTRANET_OAUTH_SERVICE_URL" for the always-registered prefix,
// "ULTRAMAIL_OAUTH_SERVICE_URL" for "ULTRAMAIL_".
std::string UltraNet_OAuth2SignInServiceEnvName(const std::string& prefix);
