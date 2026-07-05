// Tests/UltraMail/test_discovery.cpp
// Account auto-discovery: provider presets, Mozilla-autoconfig XML parsing,
// username placeholder resolution and server-URL construction. All pure — no
// network.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "UltraMailDiscovery.h"

#include "UltraMailCredentialVault.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace fs = std::filesystem;
using namespace UltraMail;

// ---- helpers ---------------------------------------------------------------

TEST(email_domain_and_localpart) {
    REQUIRE_EQ(EmailDomain("Erika@Example.COM"), std::string("example.com"));
    REQUIRE_EQ(EmailLocalPart("erika@example.com"), std::string("erika"));
}

// ---- presets ---------------------------------------------------------------

TEST(presets_gmail) {
    DiscoveryResult r = AutoDiscovery::FromPresets("someone@gmail.com");
    REQUIRE(r.found);
    REQUIRE_EQ(r.source, std::string("presets"));
    REQUIRE_EQ(r.imap.host, std::string("imap.gmail.com"));
    REQUIRE_EQ(r.imap.port, 993);
    REQUIRE(r.imap.security == MailSecurity::SslTls);
    REQUIRE(r.imap.oauth);                          // Gmail needs XOAUTH2
    REQUIRE_EQ(r.smtp.host, std::string("smtp.gmail.com"));
    REQUIRE_EQ(r.imap.username, std::string("someone@gmail.com"));
}

TEST(presets_outlook_and_gmx) {
    DiscoveryResult o = AutoDiscovery::FromPresets("me@outlook.com");
    REQUIRE(o.found);
    REQUIRE_EQ(o.imap.host, std::string("outlook.office365.com"));
    REQUIRE(o.smtp.security == MailSecurity::StartTls);

    DiscoveryResult g = AutoDiscovery::FromPresets("me@gmx.net");
    REQUIRE(g.found);
    REQUIRE_EQ(g.imap.host, std::string("imap.gmx.net"));
}

TEST(presets_unknown_domain_not_found) {
    DiscoveryResult r = AutoDiscovery::FromPresets("me@some-random-company.example");
    REQUIRE(!r.found);
}

// ---- autoconfig XML --------------------------------------------------------

TEST(parse_autoconfig_xml) {
    const std::string xml =
        "<clientConfig version=\"1.1\">"
        " <emailProvider id=\"example.com\">"
        "  <domain>example.com</domain>"
        "  <displayName>Example Mail</displayName>"
        "  <incomingServer type=\"imap\">"
        "   <hostname>imap.example.com</hostname>"
        "   <port>993</port>"
        "   <socketType>SSL</socketType>"
        "   <username>%EMAILADDRESS%</username>"
        "   <authentication>password-cleartext</authentication>"
        "  </incomingServer>"
        "  <outgoingServer type=\"smtp\">"
        "   <hostname>smtp.example.com</hostname>"
        "   <port>587</port>"
        "   <socketType>STARTTLS</socketType>"
        "   <username>%EMAILLOCALPART%</username>"
        "  </outgoingServer>"
        " </emailProvider>"
        "</clientConfig>";

    DiscoveryResult r = AutoDiscovery::ParseAutoconfig(xml, "erika@example.com");
    REQUIRE(r.found);
    REQUIRE_EQ(r.source, std::string("autoconfig"));
    REQUIRE_EQ(r.displayName, std::string("Example Mail"));

    REQUIRE_EQ(r.imap.host, std::string("imap.example.com"));
    REQUIRE_EQ(r.imap.port, 993);
    REQUIRE(r.imap.security == MailSecurity::SslTls);
    REQUIRE_EQ(r.imap.username, std::string("erika@example.com"));   // %EMAILADDRESS%

    REQUIRE_EQ(r.smtp.host, std::string("smtp.example.com"));
    REQUIRE_EQ(r.smtp.port, 587);
    REQUIRE(r.smtp.security == MailSecurity::StartTls);
    REQUIRE_EQ(r.smtp.username, std::string("erika"));              // %EMAILLOCALPART%
}

TEST(parse_autoconfig_missing_returns_not_found) {
    DiscoveryResult r = AutoDiscovery::ParseAutoconfig("<clientConfig></clientConfig>",
                                                       "x@y.com");
    REQUIRE(!r.found);
}

// ---- server URLs -----------------------------------------------------------

TEST(server_url_construction) {
    DiscoveryResult r = AutoDiscovery::FromPresets("a@gmail.com");
    REQUIRE_EQ(AutoDiscovery::ImapServerUrl(r.imap), std::string("imaps://imap.gmail.com:993/"));
    REQUIRE_EQ(AutoDiscovery::SmtpServerUrl(r.smtp), std::string("smtps://smtp.gmail.com:465/"));

    DiscoveryResult o = AutoDiscovery::FromPresets("a@outlook.com");
    // STARTTLS submission uses the plain scheme + upgrade.
    REQUIRE_EQ(AutoDiscovery::SmtpServerUrl(o.smtp), std::string("smtp://smtp.office365.com:587/"));
}

// ---- credential vault ------------------------------------------------------

TEST(credential_vault_roundtrip) {
    fs::path dir = fs::temp_directory_path() / "ultramail_vault_test";
    fs::remove_all(dir);
    CredentialVault vault(dir.string());

    REQUIRE(!vault.Has("erika"));
    REQUIRE(vault.Store("erika", "s3cr3t-p@ss"));
    REQUIRE(vault.Has("erika"));

    std::string got;
    REQUIRE(vault.Retrieve("erika", got));
    REQUIRE_EQ(got, std::string("s3cr3t-p@ss"));

    // The stored file must not contain the plaintext secret.
    std::ifstream is((dir / "creds.dat"));
    std::string content((std::istreambuf_iterator<char>(is)), std::istreambuf_iterator<char>());
    REQUIRE(content.find("s3cr3t-p@ss") == std::string::npos);

    REQUIRE(vault.Remove("erika"));
    REQUIRE(!vault.Has("erika"));

    fs::remove_all(dir);
}

TEST(credential_vault_persists_across_instances) {
    fs::path dir = fs::temp_directory_path() / "ultramail_vault_persist";
    fs::remove_all(dir);
    { CredentialVault v(dir.string()); REQUIRE(v.Store("acc", "token-xyz")); }
    { CredentialVault v(dir.string()); std::string got;
      REQUIRE(v.Retrieve("acc", got)); REQUIRE_EQ(got, std::string("token-xyz")); }
    fs::remove_all(dir);
}
