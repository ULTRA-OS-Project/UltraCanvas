// UltraCloud/include/UltraCloud/UltraCloudTypes.h
// Core data types of the UltraCloud module: results, accounts, credentials,
// remote entries and share links. Provider-independent; every provider and
// every app-facing call speaks in these.
// Version: 0.2.0
// Last Modified: 2026-09-04
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace UltraCloud {

enum class ResultCode {
    Ok = 0,
    NotFound,        // no such account, provider, or remote entry
    AuthFailed,      // the provider rejected the credentials
    Unsupported,     // the provider (or this account) cannot do that
    Network,         // could not reach the server
    Server,          // the server answered with an error (see httpStatus)
    InvalidArgument, // a bad path, URL or option
    IoError,         // local filesystem failure
    Unknown
};

struct Result {
    ResultCode  code = ResultCode::Ok;
    std::string message;
    int         httpStatus = 0;   // when a server was involved

    bool IsOk() const { return code == ResultCode::Ok; }
    explicit operator bool() const { return IsOk(); }

    static Result Ok() { return Result{}; }
    static Result Error(ResultCode code, const std::string& message, int httpStatus = 0) {
        return Result{code, message, httpStatus};
    }
};

// A configured cloud account as the account store knows it. No secrets here:
// passwords and tokens live in the ISecretStore, keyed by accountId.
struct Account {
    std::string accountId;      // stable slug, e.g. "nextcloud-erika-cloud-example-com"
    std::string providerId;     // "nextcloud", "webdav", "dropbox", ...
    std::string displayName;    // "Erika's Nextcloud"
    std::string serverUrl;      // base URL for self-hosted providers, e.g. https://cloud.example.com
    std::string username;
    std::string publicBaseUrl;  // WebDAV only: public URL that mirrors the DAV root (empty = no links)
    std::string remoteFolder;   // where UploadAndShare puts files, e.g. "/Shared from UltraMail"
    bool        isDefault = false;
};

// What a provider needs to sign in. Resolved from the secret store per call.
struct Credentials {
    std::string username;       // usually Account::username; some providers differ
    std::string password;       // password or app password (Basic auth)
    std::string token;          // OAuth2 access token (Bearer auth)
    std::string refreshToken;   // OAuth2 refresh token (empty = none issued)
    int64_t     tokenExpiresAt = 0;   // epoch seconds the access token dies; 0 = unknown

    bool HasToken() const { return !token.empty(); }
    bool TokenExpired(int64_t now) const { return HasToken() && tokenExpiresAt > 0 && now >= tokenExpiresAt; }
};

// One file or folder on the provider.
struct Entry {
    std::string name;
    std::string path;         // provider-relative, always starts with '/'
    bool        isDirectory = false;
    int64_t     size = 0;
    std::string modified;     // as reported (RFC 1123 for WebDAV); empty if unknown
};

struct ShareLinkOptions {
    bool        readOnly  = true;
    std::string password;     // empty = no password
    int64_t     expiresAt = 0;// epoch seconds, 0 = never
    std::string label;        // shown by providers that support it
};

struct ShareLink {
    std::string url;
    std::string id;           // provider's share id (for revoking later)
    int64_t     expiresAt = 0;
};

// What a provider can do; the UI greys out what it cannot.
struct ProviderCapabilities {
    bool browse = true;
    bool upload = true;
    bool shareLinks = false;
    bool passwordProtectedLinks = false;
    bool expiringLinks = false;
    bool needsServerUrl = false;   // self-hosted: the user enters a URL
    bool needsOAuth = false;       // sign-in through the browser
};

} // namespace UltraCloud
