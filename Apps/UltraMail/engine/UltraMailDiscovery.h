// Apps/UltraMail/engine/UltraMailDiscovery.h
// Account auto-discovery: given just an email address, work out the incoming
// (IMAP or JMAP) and outgoing (SMTP) server settings. Tries, in order, a
// built-in provider preset table, then the domain's JMAP server (DNS SRV
// `_jmap._tcp`, then the `/.well-known/jmap` session resource - RFC 8620
// §2.2), then a Mozilla-style autoconfig / ISPDB lookup over HTTP (UltraNet).
// The preset, SRV-parsing, probe-judging and XML-parsing steps are pure and
// testable; the network steps are orchestrated in Discover().
// Version: 0.8.0 - the JMAP step: SRV _jmap._tcp and the /.well-known/jmap
//                  probe (DiscoverJmap, ParseSrvRecord, BestSrvTarget,
//                  LooksLikeJmapServer, JmapServer)
// Version: 0.7.0 - IncomingServerUrl: jmap:// for a JMAP incoming server
//                  (replaces ImapServerUrl); a protocol change is another
//                  mailbox to IncomingMailboxChanged
// Version: 0.6.0 - ICloudSetupGuide / OffersICloudSetupGuide: the account
//                  wizard's "How to set up an iCloud mail account" info
// Version: 0.5.0 - IncomingMailboxChanged: new settings that reach another mailbox
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
    std::string        source;       // "presets" | "jmap" | "autoconfig" | "manual" | "" (none)
    std::string        displayName;  // provider display name, if known
    MailServerSettings imap;         // incoming (IMAP or JMAP, by imap.protocol)
    MailServerSettings smtp;         // outgoing (submission); may be empty for JMAP
    // Whether mail is sent through the JMAP server: a JMAP incoming server
    // and no outgoing (SMTP) one (the same rule as Account::SendsThroughJmap).
    bool SendsThroughJmap() const {
        return imap.protocol == MailProtocol::Jmap && !smtp.Valid();
    }
};

class AutoDiscovery {
public:
    // Built-in table of common providers keyed by the address domain. Pure.
    static DiscoveryResult FromPresets(const std::string& email);

    // Parse a Mozilla autoconfig / Thunderbird ISPDB XML document, resolving
    // %EMAILADDRESS% / %EMAILLOCALPART% in usernames against `email`. Pure.
    static DiscoveryResult ParseAutoconfig(const std::string& xml,
                                           const std::string& email);

    // Full pipeline: presets first, then the domain's JMAP server
    // (DiscoverJmap), then autoconfig over HTTP (UltraNet): the domain's own
    // autoconfig document, its .well-known copy, then the Thunderbird ISPDB.
    // Returns the first hit (found == false if nothing resolved). Blocking on
    // the network — run off the UI thread.
    DiscoveryResult Discover(const std::string& email);

    // The JMAP step alone (RFC 8620 §2.2): the SRV record _jmap._tcp.<domain>
    // names the server, else the domain itself is asked; the server is taken
    // when GET https://<host>/.well-known/jmap answers with a session
    // resource, or asks for a sign-in first (401), as Fastmail and Stalwart
    // do. The result (source "jmap") names the incoming JMAP server - the
    // host the answer came from, after redirects - with the full address as
    // username and no outgoing server: mail is sent through JMAP. Blocking
    // on the network — run off the UI thread.
    DiscoveryResult DiscoverJmap(const std::string& email);

    // The settings of a JMAP server at host:port for `email`, as DiscoverJmap
    // returns them. Pure.
    static DiscoveryResult JmapServer(const std::string& email, const std::string& host,
                                      int port);

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

    // The plug-in URL of the incoming server: "imap(s)://host:port/" for an
    // IMAP server (the scheme follows EffectiveSecurity, as ApplyConnection
    // does), "jmap://host:port/" for a JMAP one (the JMAP plug-in resolves
    // the session at https://host:port/.well-known/jmap). "" when the
    // settings name no server.
    static std::string IncomingServerUrl(const MailServerSettings& incoming);
    // Build an "smtp(s)://host:port/" URL for the discovered outgoing server.
    static std::string SmtpServerUrl(const MailServerSettings& smtp);
};

// Helpers (also used by tests).
std::string EmailDomain(const std::string& email);

// One target of a DNS SRV answer, as UltraNet_DnsResolve lists it:
// "priority weight port host" (RFC 2782).
struct SrvTarget {
    int         priority = 0;
    int         weight = 0;
    int         port = 0;
    std::string host;     // without the trailing dot of a fully qualified name
};
// Parse one SRV answer line; false for anything else. Pure.
bool ParseSrvRecord(const std::string& record, SrvTarget& out);
// The target to try first: the lowest priority, and among those the highest
// weight. False with no usable target - no answer, or the one answer whose
// host is "." (the domain says it has no such service). Pure.
bool BestSrvTarget(const std::vector<std::string>& records, SrvTarget& out);
// Whether an answer to GET https://<host>/.well-known/jmap came from a JMAP
// server: a success with a JSON session resource naming an apiUrl, or a
// 401 - the server wants a sign-in before it shows the session (Fastmail,
// Stalwart). A 404, an HTML page or a redirect left unfollowed is not. Pure.
bool LooksLikeJmapServer(int statusCode, const std::string& body);
std::string EmailLocalPart(const std::string& email);

// A deliberately permissive sanity check for an address the user typed: exactly
// one '@', a non-empty local part, and a domain with a dot and no whitespace.
// It exists to catch typos before an account is created — not to validate
// RFC 5322, which the mail server does authoritatively.
bool LooksLikeEmailAddress(const std::string& email);

// The guide the account wizard shows under its sign-in fields ("How to set up
// an iCloud mail account" - Show info), as Markdown: the app-specific
// password Apple requires and where it is made, what to type here, the
// servers for an own domain on iCloud+, and what to check when the sign-in
// still fails. The server names, ports and security are the iCloud preset's,
// so the guide cannot drift from what UltraMail fills in.
std::string ICloudSetupGuide();

// Whether the wizard offers that guide for the address typed so far: an
// iCloud address (icloud.com, me.com, mac.com), an address at a domain no
// preset knows (an own domain on iCloud+ looks like that), and nothing typed
// yet - not Gmail, Outlook or another known provider.
bool OffersICloudSetupGuide(const std::string& email);

// Whether a server name typed on the manual settings page can be one at all,
// before a sign-in is tried with it: empty when it can, else why not, as a
// sentence the page shows ("A server name has no @ - did you mean
// mail.example.com?"). Catches an address typed for a name, a URL scheme, a
// port after a colon, spaces, characters no host name holds and empty or
// over-long parts. Permissive otherwise: one-word LAN names, IPv4 and
// bracketed IPv6 literals and international (UTF-8) names pass, and whether the
// name exists is for the sign-in check to find out.
std::string ServerNameProblem(const std::string& host);

// Whether new incoming-server settings reach a different mailbox than `before`
// did: another host, another user name (both compared without regard to
// case or surrounding spaces) or another protocol - the same mailbox read
// over JMAP instead of IMAP numbers its messages differently, so what was
// held is of no use. A change of port or security on the same host, user and
// protocol is the same mailbox. False when `before` names no host - there was
// no mailbox to differ from. The mail held for an account came from `before`,
// so when this is true it is dropped and fetched anew. Pure.
bool IncomingMailboxChanged(const MailServerSettings& before,
                            const MailServerSettings& after);

} // namespace UltraMail
