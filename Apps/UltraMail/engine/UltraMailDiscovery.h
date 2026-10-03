// Apps/UltraMail/engine/UltraMailDiscovery.h
// Account auto-discovery: given just an email address, work out the incoming
// (IMAP) and outgoing (SMTP) server settings. Tries, in order, a built-in
// provider preset table, then a Mozilla-style autoconfig / ISPDB lookup over
// HTTP (UltraNet). The preset and XML-parsing steps are pure and testable; the
// network step is orchestrated in Discover().
// Version: 0.4.0 - ServerNameProblem: a typed server name checked before it is tried
// Version: 0.3.0 - settings resolved per account (stored, else presets);
//                  a starting point for the manual settings page
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMailTypes.h"   // MailSecurity, MailServerSettings, Account

#include <string>

namespace UltraMail {

struct DiscoveryResult {
    bool               found = false;
    std::string        source;       // "presets" | "autoconfig" | "manual" | "" (none)
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

    // Full pipeline: presets first, then autoconfig over HTTP (UltraNet):
    // the domain's own autoconfig document, its .well-known copy, then the
    // Thunderbird ISPDB. Returns the first hit (found == false if nothing
    // resolved). Blocking on the network — run off the UI thread.
    DiscoveryResult Discover(const std::string& email);

    // The settings an account syncs and sends with: the ones stored on the
    // account (discovered or entered by hand) when complete, else the provider
    // table for its address. Pure.
    static DiscoveryResult ForAccount(const Account& account);
    // Store a discovery result on an account.
    static void ApplyTo(Account& account, const DiscoveryResult& discovery);
    // A starting point for the manual settings page when nothing was found:
    // imap.<domain>:993 (TLS) and smtp.<domain>:587 (STARTTLS), the full
    // address as username. found is false — nothing here was verified. Pure.
    static DiscoveryResult GuessForDomain(const std::string& email);

    // Build an "imap(s)://host:port/" URL for the discovered incoming server.
    static std::string ImapServerUrl(const MailServerSettings& imap);
    // Build an "smtp(s)://host:port/" URL for the discovered outgoing server.
    static std::string SmtpServerUrl(const MailServerSettings& smtp);
};

// Helpers (also used by tests).
std::string EmailDomain(const std::string& email);
std::string EmailLocalPart(const std::string& email);

// A deliberately permissive sanity check for an address the user typed: exactly
// one '@', a non-empty local part, and a domain with a dot and no whitespace.
// It exists to catch typos before an account is created — not to validate
// RFC 5322, which the mail server does authoritatively.
bool LooksLikeEmailAddress(const std::string& email);

// Whether a server name typed on the manual settings page can be one at all,
// before a sign-in is tried with it: empty when it can, else why not, as a
// sentence the page shows ("A server name has no @ - did you mean
// mail.example.com?"). Catches an address typed for a name, a URL scheme, a
// port after a colon, spaces, characters no host name holds and empty or
// over-long parts. Permissive otherwise: one-word LAN names, IPv4 and
// bracketed IPv6 literals and international (UTF-8) names pass, and whether the
// name exists is for the sign-in check to find out.
std::string ServerNameProblem(const std::string& host);

} // namespace UltraMail
