// Apps/UltraMail/engine/UltraMailDiscovery.cpp
// Version: 0.5.0 - ICloudSetupGuide, OffersICloudSetupGuide
// Version: 0.4.0 - IncomingMailboxChanged
// Version: 0.3.0 - ServerNameProblem
// Version: 0.2.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailDiscovery.h"

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetHttp.h>
#include <UltraCanvasUtils.h>
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace UltraMail {

std::string EmailDomain(const std::string& email) {
    auto at = email.find('@');
    if (at == std::string::npos) return "";
    std::string d = email.substr(at + 1);
    std::transform(d.begin(), d.end(), d.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return d;
}

std::string EmailLocalPart(const std::string& email) {
    auto at = email.find('@');
    return at == std::string::npos ? email : email.substr(0, at);
}

std::string ICloudSetupGuide() {
    // The servers as the preset fills them in.
    const DiscoveryResult icloud = AutoDiscovery::FromPresets("someone@icloud.com");
    auto security = [](MailSecurity s) {
        return s == MailSecurity::SslTls ? std::string("SSL/TLS")
             : s == MailSecurity::StartTls ? std::string("STARTTLS") : std::string("none");
    };
    const std::string incoming = "**" + icloud.imap.host + "**, port **" +
        std::to_string(icloud.imap.port) + "**, " + security(icloud.imap.security);
    const std::string outgoing = "**" + icloud.smtp.host + "**, port **" +
        std::to_string(icloud.smtp.port) + "**, " + security(icloud.smtp.security);

    return
        "**Before you start**\n\n"
        "- Two-factor authentication must be on for your Apple Account.\n"
        "- iCloud Mail must be turned on for it (in your iCloud settings on an "
        "iPhone, iPad or Mac, or on iCloud.com).\n\n"
        "**1. Create an app-specific password.** Apple does not let other mail "
        "programs sign in with your Apple Account password.\n\n"
        "1. Sign in at **account.apple.com**.\n"
        "2. Open **Sign-In and Security**, then **App-Specific Passwords**.\n"
        "3. Choose **Generate an app-specific password**, name it \"UltraMail\" "
        "and copy the password Apple shows.\n\n"
        "**2. Add the account here.**\n\n"
        "- **Email address:** your @icloud.com, @me.com or @mac.com address - "
        "UltraMail fills in Apple's servers.\n"
        "- **Password:** the app-specific password. Your Apple Account password "
        "fails with \"authentication failed\".\n\n"
        "**Your own domain with iCloud+?** Type your own address above. On the "
        "server settings page that follows, enter:\n\n"
        "- Incoming (IMAP): " + incoming + "\n"
        "- Outgoing (SMTP): " + outgoing + "\n"
        "- Username: your **@icloud.com address** - not the address on your own "
        "domain.\n\n"
        "**Sign-in still fails?**\n\n"
        "- Changing your Apple Account password revokes every app-specific "
        "password: create a new one.\n"
        "- Check that iCloud Mail is turned on.\n"
        "- With an own domain, the username must be your @icloud.com address.\n";
}

bool OffersICloudSetupGuide(const std::string& email) {
    if (email.find('@') == std::string::npos) return true;   // nothing typed yet
    const DiscoveryResult preset = AutoDiscovery::FromPresets(email);
    return !preset.found || preset.displayName == "iCloud";
}

bool LooksLikeEmailAddress(const std::string& email) {
    const auto at = email.find('@');
    if (at == std::string::npos) return false;          // no separator
    if (at == 0) return false;                          // empty local part
    if (email.find('@', at + 1) != std::string::npos) return false;  // more than one

    const std::string domain = email.substr(at + 1);
    if (domain.size() < 3) return false;                // shortest is "a.b"
    const auto dot = domain.find('.');
    if (dot == std::string::npos) return false;         // no dot
    if (dot == 0 || dot + 1 >= domain.size()) return false;  // leading/trailing dot

    for (unsigned char c : email)
        if (std::isspace(c)) return false;
    return true;
}

bool IncomingMailboxChanged(const MailServerSettings& before,
                            const MailServerSettings& after) {
    auto norm = [](const std::string& s) {
        std::size_t a = 0, b = s.size();
        while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
        while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
        std::string out = s.substr(a, b - a);
        for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return out;
    };
    // "mail.example.com." is the same host as "mail.example.com".
    auto hostOf = [&norm](const std::string& s) {
        std::string h = norm(s);
        while (!h.empty() && h.back() == '.') h.pop_back();
        return h;
    };
    const std::string host = hostOf(before.host);
    if (host.empty()) return false;
    return host != hostOf(after.host) || norm(before.username) != norm(after.username);
}

std::string ServerNameProblem(const std::string& host) {
    if (host.empty()) return "The server name is empty.";
    for (unsigned char c : host)
        if (std::isspace(c)) return "A server name has no spaces in it.";
    if (const auto scheme = host.find("://"); scheme != std::string::npos)
        return "Enter the server name only, without \"" + host.substr(0, scheme + 3)
               + "\" - for example " + host.substr(scheme + 3) + ".";
    if (const auto at = host.find('@'); at != std::string::npos) {
        // The usual slip: the address's @ typed where the name has a dot.
        std::string guess = host;
        guess[at] = '.';
        if (guess.find('@') == std::string::npos && ServerNameProblem(guess).empty())
            return "A server name has no @ - did you mean " + guess + "?";
        return "A server name has no @ - that looks like an address.";
    }
    if (host.find('/') != std::string::npos)
        return "A server name has no / - enter just the name, for example mail.example.com.";
    if (host.front() == '[') {   // an IPv6 literal: [2001:db8::1]
        if (host.size() < 4 || host.back() != ']') return "An IPv6 address must be closed with ].";
        for (std::size_t i = 1; i + 1 < host.size(); ++i) {
            const unsigned char c = static_cast<unsigned char>(host[i]);
            if (!std::isxdigit(c) && c != ':' && c != '.') return "That is not an IPv6 address.";
        }
        return std::string();
    }
    if (host.find(':') != std::string::npos)
        return "The port goes in the port field, not after a colon in the server name.";

    std::string name = host;
    if (name.back() == '.') name.pop_back();   // a fully qualified "example.com."
    if (name.empty() || name.size() > 253) return "That server name is too long.";
    std::size_t start = 0;
    while (start <= name.size()) {
        std::size_t dot = name.find('.', start);
        if (dot == std::string::npos) dot = name.size();
        const std::string label = name.substr(start, dot - start);
        if (label.empty()) return "A server name has no empty part between two dots.";
        if (label.size() > 63) return "A part of that server name is too long.";
        if (label.front() == '-' || label.back() == '-')
            return "A part of a server name cannot start or end with -.";
        for (unsigned char c : label) {
            if (std::isalnum(c) || c == '-' || c == '_' || c >= 0x80) continue;
            return std::string("A server name cannot hold \"") + static_cast<char>(c) + "\".";
        }
        start = dot + 1;
    }
    return std::string();
}

namespace {

DiscoveryResult MakePreset(const std::string& display, const std::string& email,
                           const std::string& imapHost, int imapPort,
                           const std::string& smtpHost, int smtpPort,
                           MailSecurity smtpSec, bool oauth) {
    DiscoveryResult r;
    r.found = true;
    r.source = "presets";
    r.displayName = display;
    r.imap.host = imapHost; r.imap.port = imapPort;
    r.imap.security = MailSecurity::SslTls; r.imap.username = email; r.imap.oauth = oauth;
    r.smtp.host = smtpHost; r.smtp.port = smtpPort;
    r.smtp.security = smtpSec; r.smtp.username = email; r.smtp.oauth = oauth;
    // OAuth2 providers take nothing else in a mail program any more; the rest
    // are left to what their servers offer.
    r.imap.auth = r.smtp.auth = oauth ? UltraNetMailAuth::OAuth2 : UltraNetMailAuth::Any;
    return r;
}

// XML: inner text of the first <tag ...> block whose opening tag contains
// `attr` (attr empty = first <tag>).
std::string FindBlock(const std::string& xml, const std::string& tag,
                      const std::string& attr) {
    std::size_t pos = 0;
    const std::string open = "<" + tag;
    while (true) {
        std::size_t o = xml.find(open, pos);
        if (o == std::string::npos) return "";
        std::size_t gt = xml.find('>', o);
        if (gt == std::string::npos) return "";
        std::string openTag = xml.substr(o, gt - o + 1);
        if (attr.empty() || openTag.find(attr) != std::string::npos) {
            std::size_t close = xml.find("</" + tag + ">", gt);
            if (close == std::string::npos) return "";
            return xml.substr(gt + 1, close - gt - 1);
        }
        pos = gt + 1;
    }
}

// Inner text of <tag>...</tag> within `block`.
std::string TagText(const std::string& block, const std::string& tag) {
    std::size_t o = block.find("<" + tag + ">");
    if (o == std::string::npos) return "";
    std::size_t start = o + tag.size() + 2;
    std::size_t close = block.find("</" + tag + ">", start);
    if (close == std::string::npos) return "";
    return UltraCanvas::Trim(block.substr(start, close - start));
}

// Inner text of every <tag>...</tag> within `block`, in order.
std::vector<std::string> TagTexts(const std::string& block, const std::string& tag) {
    std::vector<std::string> out;
    const std::string open = "<" + tag + ">", close = "</" + tag + ">";
    std::size_t pos = 0;
    while (true) {
        std::size_t o = block.find(open, pos);
        if (o == std::string::npos) break;
        std::size_t start = o + open.size();
        std::size_t c = block.find(close, start);
        if (c == std::string::npos) break;
        out.push_back(UltraCanvas::Trim(block.substr(start, c - start)));
        pos = c + close.size();
    }
    return out;
}

// An autoconfig <authentication> value (Mozilla's names). Methods UltraMail
// cannot run — a client certificate, the sender's IP address — read as Any.
UltraNetMailAuth AuthFromAutoconfig(const std::string& method) {
    std::string m = method;
    std::transform(m.begin(), m.end(), m.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (m == "password-cleartext" || m == "plain")    return UltraNetMailAuth::Password;
    if (m == "password-encrypted" || m == "secure")   return UltraNetMailAuth::EncryptedPassword;
    if (m == "oauth2")                                return UltraNetMailAuth::OAuth2;
    if (m == "gssapi")                                return UltraNetMailAuth::Kerberos;
    if (m == "ntlm")                                  return UltraNetMailAuth::NTLM;
    if (m == "none")                                  return UltraNetMailAuth::None;
    return UltraNetMailAuth::Any;
}

MailSecurity SecurityFromSocketType(const std::string& s) {
    std::string t = s;
    std::transform(t.begin(), t.end(), t.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    if (t == "SSL" || t == "SSL/TLS") return MailSecurity::SslTls;
    if (t == "STARTTLS")              return MailSecurity::StartTls;
    if (t == "PLAIN" || t == "NONE")  return MailSecurity::Plain;
    return MailSecurity::SslTls;   // safe default
}

std::string ResolveUsername(const std::string& raw, const std::string& email) {
    if (raw.empty() || raw == "%EMAILADDRESS%") return email;
    if (raw == "%EMAILLOCALPART%") return EmailLocalPart(email);
    return raw;
}

void FillServer(const std::string& block, const std::string& email,
                MailServerSettings& out) {
    out.host = TagText(block, "hostname");
    std::string port = TagText(block, "port");
    out.port = port.empty() ? 0 : std::atoi(port.c_str());
    out.security = SecurityFromSocketType(TagText(block, "socketType"));
    out.username = ResolveUsername(TagText(block, "username"), email);
    // A server block may list several <authentication> methods, preferred
    // first. UltraMail can only run OAuth2 for the providers it has a client
    // for, so the first method that works with a password is taken, and OAuth2
    // only when nothing else is listed.
    bool listsOAuth = false;
    UltraNetMailAuth chosen = UltraNetMailAuth::Any;
    for (const std::string& method : TagTexts(block, "authentication")) {
        const UltraNetMailAuth m = AuthFromAutoconfig(method);
        if (m == UltraNetMailAuth::OAuth2) { listsOAuth = true; continue; }
        if (m != UltraNetMailAuth::Any && chosen == UltraNetMailAuth::Any) chosen = m;
    }
    out.oauth = listsOAuth;
    out.auth  = (chosen == UltraNetMailAuth::Any && listsOAuth) ? UltraNetMailAuth::OAuth2
                                                                 : chosen;
}

std::string PortStr(int port) { return std::to_string(port); }

} // namespace

DiscoveryResult AutoDiscovery::FromPresets(const std::string& email) {
    const std::string d = EmailDomain(email);
    auto is = [&](std::initializer_list<const char*> domains) {
        for (const char* x : domains) if (d == x) return true;
        return false;
    };

    if (is({"gmail.com", "googlemail.com"}))
        return MakePreset("Gmail", email, "imap.gmail.com", 993,
                          "smtp.gmail.com", 465, MailSecurity::SslTls, /*oauth=*/true);
    if (is({"outlook.com", "hotmail.com", "live.com", "msn.com", "office365.com"}))
        return MakePreset("Outlook", email, "outlook.office365.com", 993,
                          "smtp.office365.com", 587, MailSecurity::StartTls, /*oauth=*/true);
    // Yahoo's mail domains as Thunderbird's ISPDB lists them (not yahoo.co.jp:
    // Yahoo! Japan is a separate service).
    if (is({"yahoo.com", "yahoo.de", "ymail.com", "rocketmail.com", "myyahoo.com",
            "yahoo.co.uk", "yahoo.fr", "yahoo.it", "yahoo.es", "yahoo.ca",
            "yahoo.com.au", "yahoo.co.in", "yahoo.com.br", "yahoo.com.mx"}))
        return MakePreset("Yahoo", email, "imap.mail.yahoo.com", 993,
                          "smtp.mail.yahoo.com", 465, MailSecurity::SslTls, /*oauth=*/true);
    if (is({"icloud.com", "me.com", "mac.com"}))
        return MakePreset("iCloud", email, "imap.mail.me.com", 993,
                          "smtp.mail.me.com", 587, MailSecurity::StartTls, false);
    if (is({"gmx.net", "gmx.com", "gmx.de"}))
        return MakePreset("GMX", email, "imap.gmx.net", 993,
                          "mail.gmx.net", 587, MailSecurity::StartTls, false);
    if (is({"web.de"}))
        return MakePreset("WEB.DE", email, "imap.web.de", 993,
                          "smtp.web.de", 587, MailSecurity::StartTls, false);
    if (is({"mailbox.org"}))
        return MakePreset("mailbox.org", email, "imap.mailbox.org", 993,
                          "smtp.mailbox.org", 465, MailSecurity::SslTls, false);
    if (is({"posteo.de", "posteo.net"}))
        return MakePreset("Posteo", email, "posteo.de", 993,
                          "posteo.de", 465, MailSecurity::SslTls, false);

    return DiscoveryResult{};
}

DiscoveryResult AutoDiscovery::ParseAutoconfig(const std::string& xml,
                                               const std::string& email) {
    DiscoveryResult r;
    std::string incoming = FindBlock(xml, "incomingServer", "imap");
    if (incoming.empty()) incoming = FindBlock(xml, "incomingServer", "");
    std::string outgoing = FindBlock(xml, "outgoingServer", "smtp");
    if (outgoing.empty()) outgoing = FindBlock(xml, "outgoingServer", "");

    if (!incoming.empty()) FillServer(incoming, email, r.imap);
    if (!outgoing.empty()) FillServer(outgoing, email, r.smtp);

    std::string provider = FindBlock(xml, "emailProvider", "");
    if (!provider.empty()) r.displayName = TagText(provider, "displayName");

    r.found = r.imap.Valid();
    if (r.found) r.source = "autoconfig";
    return r;
}

DiscoveryResult AutoDiscovery::Discover(const std::string& email) {
    DiscoveryResult preset = FromPresets(email);
    if (preset.found) return preset;

    const std::string domain = EmailDomain(email);
    if (domain.empty()) return DiscoveryResult{};

    if (!UltraNet_IsInitialized()) UltraNet_Initialize();

    const std::vector<std::string> urls = {
        "https://autoconfig." + domain + "/mail/config-v1.1.xml?emailaddress=" + email,
        "https://" + domain + "/.well-known/autoconfig/mail/config-v1.1.xml?emailaddress=" + email,
        "https://autoconfig.thunderbird.net/v1.1/" + domain,
    };
    for (const auto& url : urls) {
        UltraNetResponse resp;
        UltraNetResult r = UltraNet_HttpGet(url, resp);
        if (r && resp.IsSuccess()) {
            DiscoveryResult d = ParseAutoconfig(resp.GetBodyAsString(), email);
            if (d.found) return d;
        }
    }
    return DiscoveryResult{};
}

DiscoveryResult AutoDiscovery::ForAccount(const Account& account) {
    if (account.HasServers()) {
        DiscoveryResult r;
        r.found       = true;
        r.source      = account.providerName.empty() ? "manual" : "stored";
        r.displayName = account.providerName;
        r.imap        = account.imap;
        r.smtp        = account.smtp;
        if (r.imap.username.empty()) r.imap.username = account.email;
        if (r.smtp.username.empty()) r.smtp.username = account.email;
        return r;
    }
    // An account without stored servers predates the authentication setting
    // too, and may sign in with an app password at an OAuth2 provider: leave
    // the method to the server, as it always was.
    DiscoveryResult r = FromPresets(account.email);
    r.imap.auth = r.smtp.auth = UltraNetMailAuth::Any;
    return r;
}

void AutoDiscovery::ApplyTo(Account& account, const DiscoveryResult& discovery) {
    account.imap         = discovery.imap;
    account.smtp         = discovery.smtp;
    account.providerName = discovery.displayName;
}

DiscoveryResult AutoDiscovery::GuessForDomain(const std::string& email) {
    DiscoveryResult r;
    const std::string domain = EmailDomain(email);
    if (domain.empty()) return r;
    r.source = "guess";
    r.imap.host = "imap." + domain; r.imap.port = 993;
    r.imap.security = MailSecurity::SslTls; r.imap.username = email;
    r.smtp.host = "smtp." + domain; r.smtp.port = 587;
    r.smtp.security = MailSecurity::StartTls; r.smtp.username = email;
    return r;   // found stays false: nothing verified
}

std::string AutoDiscovery::ImapServerUrl(const MailServerSettings& imap) {
    if (!imap.Valid()) return "";
    // Reconcile against the port so imaps:// (implicit TLS) is never used on a
    // STARTTLS port; ApplyConnection derives implicitTls the same way.
    std::string scheme =
        EffectiveSecurity(imap.port, imap.security) == MailSecurity::SslTls ? "imaps" : "imap";
    return scheme + "://" + imap.host + ":" + PortStr(imap.port) + "/";
}

std::string AutoDiscovery::SmtpServerUrl(const MailServerSettings& smtp) {
    if (!smtp.Valid()) return "";
    std::string scheme =
        EffectiveSecurity(smtp.port, smtp.security) == MailSecurity::SslTls ? "smtps" : "smtp";
    return scheme + "://" + smtp.host + ":" + PortStr(smtp.port) + "/";
}

} // namespace UltraMail
