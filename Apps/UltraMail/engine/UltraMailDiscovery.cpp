// Apps/UltraMail/engine/UltraMailDiscovery.cpp
// Version: 0.1.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailDiscovery.h"

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetHttp.h>

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

namespace {

std::string Trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b-1]))) --b;
    return s.substr(a, b - a);
}

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
    return Trim(block.substr(start, close - start));
}

MailSecurity SecurityFromSocketType(const std::string& s) {
    std::string t = s;
    std::transform(t.begin(), t.end(), t.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    if (t == "SSL" || t == "SSL/TLS") return MailSecurity::SslTls;
    if (t == "STARTTLS")              return MailSecurity::StartTls;
    if (t == "PLAIN" || t == "NONE")  return MailSecurity::None;
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
    std::string auth = TagText(block, "authentication");
    if (auth.find("OAuth2") != std::string::npos || auth.find("oauth2") != std::string::npos)
        out.oauth = true;
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
    if (is({"yahoo.com", "yahoo.de", "ymail.com"}))
        return MakePreset("Yahoo", email, "imap.mail.yahoo.com", 993,
                          "smtp.mail.yahoo.com", 465, MailSecurity::SslTls, false);
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

std::string AutoDiscovery::ImapServerUrl(const MailServerSettings& imap) {
    if (!imap.Valid()) return "";
    std::string scheme = imap.security == MailSecurity::SslTls ? "imaps" : "imap";
    return scheme + "://" + imap.host + ":" + PortStr(imap.port) + "/";
}

std::string AutoDiscovery::SmtpServerUrl(const MailServerSettings& smtp) {
    if (!smtp.Valid()) return "";
    std::string scheme = smtp.security == MailSecurity::SslTls ? "smtps" : "smtp";
    return scheme + "://" + smtp.host + ":" + PortStr(smtp.port) + "/";
}

} // namespace UltraMail
