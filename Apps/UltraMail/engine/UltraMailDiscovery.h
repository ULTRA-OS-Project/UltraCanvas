// Apps/UltraMail/engine/UltraMailDiscovery.h
// Account auto-discovery: given just an email address, work out the incoming
// (IMAP) and outgoing (SMTP) server settings. Tries, in order, a built-in
// provider preset table, then a Mozilla-style autoconfig / ISPDB lookup over
// HTTP (UltraNet). The preset and XML-parsing steps are pure and testable; the
// network step is orchestrated in Discover().
// Version: 0.1.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <string>

namespace UltraMail {

// Connection security for a mail server.
enum class MailSecurity {
    None = 0,     // plaintext (discouraged)
    StartTls,     // upgrade on the plaintext port
    SslTls        // implicit TLS from connect
};

struct MailServerSettings {
    std::string  host;
    int          port = 0;
    MailSecurity security = MailSecurity::SslTls;
    std::string  username;     // resolved (full address or local-part)
    bool         oauth = false; // provider expects OAuth2/XOAUTH2

    bool Valid() const { return !host.empty() && port > 0; }
};

struct DiscoveryResult {
    bool               found = false;
    std::string        source;       // "presets" | "autoconfig" | "" (none)
    std::string        displayName;  // provider display name, if known
    MailServerSettings imap;         // incoming
    MailServerSettings smtp;         // outgoing (submission)
};

class AutoDiscovery {
public:
    // Built-in table of common providers keyed by the address domain. Pure.
    static DiscoveryResult FromPresets(const std::string& email);

    // Parse a Mozilla autoconfig / Thunderbird ISPDB XML document, resolving
    // %EMAILADDRESS% / %EMAILLOCALPART% in usernames against `email`. Pure.
    static DiscoveryResult ParseAutoconfig(const std::string& xml,
                                           const std::string& email);

    // Full pipeline: presets first, then autoconfig over HTTP (UltraNet).
    // Returns the first hit (found == false if nothing resolved).
    DiscoveryResult Discover(const std::string& email);

    // Build an "imap(s)://host:port/" URL for the discovered incoming server.
    static std::string ImapServerUrl(const MailServerSettings& imap);
    // Build an "smtp(s)://host:port/" URL for the discovered outgoing server.
    static std::string SmtpServerUrl(const MailServerSettings& smtp);
};

// Helpers (also used by tests).
std::string EmailDomain(const std::string& email);
std::string EmailLocalPart(const std::string& email);

} // namespace UltraMail
