// Tests/UltraMail/test_discovery.cpp
// Account auto-discovery: provider presets, Mozilla-autoconfig XML parsing,
// username placeholder resolution and server-URL construction. All pure — no
// network.
// Version: 0.2.0 - ServerNameProblem
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "UltraMailDiscovery.h"

#include "UltraMailCredentialVault.h"

#include <UltraNet/UltraNetMime.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace UltraMail;

// ---- helpers ---------------------------------------------------------------

TEST(email_domain_and_localpart) {
    REQUIRE_EQ(EmailDomain("Erika@Example.COM"), std::string("example.com"));
    REQUIRE_EQ(EmailLocalPart("erika@example.com"), std::string("erika"));
}

TEST(looks_like_email_address_accepts_real_addresses) {
    REQUIRE(LooksLikeEmailAddress("erika@example.com"));
    REQUIRE(LooksLikeEmailAddress("Erika.Example+mail@mail.example.co.uk"));
    REQUIRE(LooksLikeEmailAddress("a@b.c"));
}

TEST(looks_like_email_address_rejects_typos) {
    REQUIRE(!LooksLikeEmailAddress(""));
    REQUIRE(!LooksLikeEmailAddress("erika"));            // no @
    REQUIRE(!LooksLikeEmailAddress("@example.com"));     // empty local part
    REQUIRE(!LooksLikeEmailAddress("erika@"));           // empty domain
    REQUIRE(!LooksLikeEmailAddress("erika@example"));    // no dot in domain
    REQUIRE(!LooksLikeEmailAddress("erika@.com"));       // leading dot
    REQUIRE(!LooksLikeEmailAddress("erika@example."));   // trailing dot
    REQUIRE(!LooksLikeEmailAddress("a@b@example.com"));  // two @
    REQUIRE(!LooksLikeEmailAddress("erika @example.com"));  // whitespace
}

TEST(server_name_problem_accepts_real_server_names) {
    REQUIRE(ServerNameProblem("mail.interkontakt.net").empty());
    REQUIRE(ServerNameProblem("imap.gmail.com").empty());
    REQUIRE(ServerNameProblem("smtp-relay.example.co.uk").empty());
    REQUIRE(ServerNameProblem("example.com.").empty());          // fully qualified
    REQUIRE(ServerNameProblem("mailserver").empty());            // a one-word LAN name
    REQUIRE(ServerNameProblem("192.168.1.20").empty());
    REQUIRE(ServerNameProblem("[2001:db8::1]").empty());
    REQUIRE(ServerNameProblem("mail.b\xC3\xBC" "cher.de").empty());  // an international name
}

TEST(server_name_problem_rejects_what_cannot_be_a_server) {
    // The address's @ typed for the name's dot: the dot is suggested.
    REQUIRE_EQ(ServerNameProblem("mail@interkontakt.net"),
               std::string("A server name has no @ - did you mean mail.interkontakt.net?"));
    REQUIRE(!ServerNameProblem("a@b@example.com").empty());
    REQUIRE(!ServerNameProblem("").empty());
    REQUIRE(!ServerNameProblem("mail .example.com").empty());    // a space
    REQUIRE(!ServerNameProblem("imaps://mail.example.com").empty());
    REQUIRE(!ServerNameProblem("mail.example.com:993").empty()); // port after a colon
    REQUIRE(!ServerNameProblem("mail.example.com/imap").empty());
    REQUIRE(!ServerNameProblem("mail..example.com").empty());    // empty part
    REQUIRE(!ServerNameProblem(".example.com").empty());
    REQUIRE(!ServerNameProblem("-mail.example.com").empty());
    REQUIRE(!ServerNameProblem("mail,example.com").empty());     // a comma
    REQUIRE(!ServerNameProblem(std::string(64, 'a') + ".com").empty());
    REQUIRE(!ServerNameProblem("[2001:db8::1").empty());
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

TEST(presets_cover_yahoo_country_and_legacy_domains) {
    for (const char* e : {"erika@yahoo.co.uk", "erika@yahoo.fr", "erika@rocketmail.com",
                          "erika@myyahoo.com", "erika@yahoo.com.au"}) {
        DiscoveryResult r = AutoDiscovery::FromPresets(e);
        REQUIRE(r.found);
        REQUIRE_EQ(r.imap.host, std::string("imap.mail.yahoo.com"));
        REQUIRE(r.imap.oauth);
    }
    REQUIRE(!AutoDiscovery::FromPresets("erika@yahoo.co.jp").found);   // Yahoo! Japan is separate
}

TEST(presets_yahoo_uses_oauth) {
    DiscoveryResult r = AutoDiscovery::FromPresets("someone@yahoo.com");
    REQUIRE(r.found);
    REQUIRE_EQ(r.imap.host, std::string("imap.mail.yahoo.com"));
    REQUIRE_EQ(r.smtp.host, std::string("smtp.mail.yahoo.com"));
    REQUIRE(r.imap.oauth);   // Yahoo deprecated app passwords: XOAUTH2 now
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

TEST(parse_autoconfig_takes_the_authentication_method) {
    auto parse = [](const std::string& imapAuth, const std::string& smtpAuth) {
        const std::string xml =
            "<clientConfig><emailProvider id=\"example.com\">"
            "<incomingServer type=\"imap\"><hostname>imap.example.com</hostname>"
            "<port>993</port><socketType>SSL</socketType>" + imapAuth + "</incomingServer>"
            "<outgoingServer type=\"smtp\"><hostname>smtp.example.com</hostname>"
            "<port>587</port><socketType>STARTTLS</socketType>" + smtpAuth + "</outgoingServer>"
            "</emailProvider></clientConfig>";
        return AutoDiscovery::ParseAutoconfig(xml, "erika@example.com");
    };
    // The parse_autoconfig_xml document: a normal password in, nothing said out.
    DiscoveryResult r = parse("<authentication>password-cleartext</authentication>", "");
    REQUIRE(r.imap.auth == UltraNetMailAuth::Password);
    REQUIRE(r.smtp.auth == UltraNetMailAuth::Any);

    r = parse("<authentication>password-encrypted</authentication>",
              "<authentication>none</authentication>");
    REQUIRE(r.imap.auth == UltraNetMailAuth::EncryptedPassword);
    REQUIRE(r.smtp.auth == UltraNetMailAuth::None);

    // OAuth2 listed first with a password fallback: the password (UltraMail has
    // no OAuth client for an arbitrary provider), but the capability is kept.
    r = parse("<authentication>OAuth2</authentication>"
              "<authentication>password-cleartext</authentication>",
              "<authentication>OAuth2</authentication>");
    REQUIRE(r.imap.auth == UltraNetMailAuth::Password);
    REQUIRE(r.imap.oauth);
    REQUIRE(r.smtp.auth == UltraNetMailAuth::OAuth2);   // nothing else offered

    r = parse("<authentication>GSSAPI</authentication>", "<authentication>NTLM</authentication>");
    REQUIRE(r.imap.auth == UltraNetMailAuth::Kerberos);
    REQUIRE(r.smtp.auth == UltraNetMailAuth::NTLM);

    // A method UltraMail cannot run leaves the choice to the server.
    r = parse("<authentication>TLS-client-cert</authentication>", "");
    REQUIRE(r.imap.auth == UltraNetMailAuth::Any);
}

TEST(presets_default_to_oauth2_for_gmail_outlook_and_yahoo) {
    for (const char* email : {"erika@gmail.com", "erika@googlemail.com", "erika@live.com",
                              "erika@outlook.com", "erika@hotmail.com", "erika@yahoo.com"}) {
        DiscoveryResult r = AutoDiscovery::FromPresets(email);
        REQUIRE(r.found);
        REQUIRE(r.imap.auth == UltraNetMailAuth::OAuth2);
        REQUIRE(r.smtp.auth == UltraNetMailAuth::OAuth2);
    }
    // Other providers leave it to the server.
    DiscoveryResult gmx = AutoDiscovery::FromPresets("erika@gmx.de");
    REQUIRE(gmx.imap.auth == UltraNetMailAuth::Any);
    REQUIRE(gmx.smtp.auth == UltraNetMailAuth::Any);
    REQUIRE(AutoDiscovery::GuessForDomain("erika@example.com").imap.auth == UltraNetMailAuth::Any);

    // An account stored without servers predates the setting: Automatic, so an
    // old app-password Gmail account keeps signing in.
    Account old; old.accountId = "erika-gmail-com"; old.email = "erika@gmail.com";
    REQUIRE(AutoDiscovery::ForAccount(old).imap.auth == UltraNetMailAuth::Any);
    REQUIRE(AutoDiscovery::ForAccount(old).smtp.auth == UltraNetMailAuth::Any);
}

TEST(mail_auth_round_trips_through_strings) {
    for (UltraNetMailAuth a : {UltraNetMailAuth::Any, UltraNetMailAuth::Password,
                               UltraNetMailAuth::EncryptedPassword, UltraNetMailAuth::OAuth2,
                               UltraNetMailAuth::Kerberos, UltraNetMailAuth::NTLM,
                               UltraNetMailAuth::None})
        REQUIRE(MailAuthFromString(ToString(a)) == a);
    REQUIRE(MailAuthFromString("") == UltraNetMailAuth::Any);   // accounts from before the setting
    REQUIRE(MailAuthFromString("bogus") == UltraNetMailAuth::Any);
}

TEST(parse_autoconfig_missing_returns_not_found) {
    DiscoveryResult r = AutoDiscovery::ParseAutoconfig("<clientConfig></clientConfig>",
                                                       "x@y.com");
    REQUIRE(!r.found);
}

// ---- server URLs -----------------------------------------------------------

TEST(mail_security_round_trips_through_strings) {
    REQUIRE(MailSecurityFromString(ToString(MailSecurity::Plain)) == MailSecurity::Plain);
    REQUIRE(MailSecurityFromString(ToString(MailSecurity::StartTls)) == MailSecurity::StartTls);
    REQUIRE(MailSecurityFromString(ToString(MailSecurity::SslTls)) == MailSecurity::SslTls);
    REQUIRE(MailSecurityFromString("") == MailSecurity::SslTls);   // the safe default
}

TEST(for_account_prefers_stored_settings_over_presets) {
    Account a; a.accountId = "erika-gmail-com"; a.email = "erika@gmail.com";
    // Nothing stored: the provider table answers.
    DiscoveryResult r = AutoDiscovery::ForAccount(a);
    REQUIRE(r.found);
    REQUIRE_EQ(r.imap.host, std::string("imap.gmail.com"));

    // Stored (say, entered by hand): those win, and empty usernames default
    // to the address.
    DiscoveryResult manual;
    manual.imap.host = "mail.example.org"; manual.imap.port = 143;
    manual.imap.security = MailSecurity::StartTls;
    manual.smtp.host = "mail.example.org"; manual.smtp.port = 465;
    manual.smtp.security = MailSecurity::SslTls;
    AutoDiscovery::ApplyTo(a, manual);
    REQUIRE(a.HasServers());
    REQUIRE(a.providerName.empty());
    r = AutoDiscovery::ForAccount(a);
    REQUIRE(r.found);
    REQUIRE_EQ(r.source, std::string("manual"));
    REQUIRE_EQ(r.imap.host, std::string("mail.example.org"));
    REQUIRE_EQ(r.imap.port, 143);
    REQUIRE_EQ(r.imap.username, std::string("erika@gmail.com"));
    REQUIRE_EQ(r.smtp.username, std::string("erika@gmail.com"));
    REQUIRE_EQ(AutoDiscovery::ImapServerUrl(r.imap), std::string("imap://mail.example.org:143/"));
    REQUIRE_EQ(AutoDiscovery::SmtpServerUrl(r.smtp), std::string("smtps://mail.example.org:465/"));

    // A preset stored on the account keeps its name.
    AutoDiscovery::ApplyTo(a, AutoDiscovery::FromPresets(a.email));
    REQUIRE_EQ(AutoDiscovery::ForAccount(a).displayName, std::string("Gmail"));
    REQUIRE_EQ(AutoDiscovery::ForAccount(a).source, std::string("stored"));
}

TEST(guess_for_domain_is_a_prefill_not_a_result) {
    DiscoveryResult g = AutoDiscovery::GuessForDomain("erika@example.org");
    REQUIRE(!g.found);
    REQUIRE_EQ(g.imap.host, std::string("imap.example.org"));
    REQUIRE_EQ(g.imap.port, 993);
    REQUIRE(g.imap.security == MailSecurity::SslTls);
    REQUIRE_EQ(g.smtp.host, std::string("smtp.example.org"));
    REQUIRE_EQ(g.smtp.port, 587);
    REQUIRE(g.smtp.security == MailSecurity::StartTls);
    REQUIRE_EQ(g.imap.username, std::string("erika@example.org"));
    REQUIRE(AutoDiscovery::GuessForDomain("not-an-address").imap.host.empty());
}

TEST(server_url_construction) {
    DiscoveryResult r = AutoDiscovery::FromPresets("a@gmail.com");
    REQUIRE_EQ(AutoDiscovery::ImapServerUrl(r.imap), std::string("imaps://imap.gmail.com:993/"));
    REQUIRE_EQ(AutoDiscovery::SmtpServerUrl(r.smtp), std::string("smtps://smtp.gmail.com:465/"));

    DiscoveryResult o = AutoDiscovery::FromPresets("a@outlook.com");
    // STARTTLS submission uses the plain scheme + upgrade.
    REQUIRE_EQ(AutoDiscovery::SmtpServerUrl(o.smtp), std::string("smtp://smtp.office365.com:587/"));
}

// ---- credential vault ------------------------------------------------------
// The vault is UltraVault-backed and stays locked until the master password is
// supplied, so every test unlocks first. UltraVault is a per-process singleton:
// each Unlock() reconfigures it, so tests must not assume two vaults are open
// at once.

namespace {
const std::string kMaster = "correct horse battery staple";
}

TEST(credential_vault_locked_until_unlocked) {
    fs::path dir = fs::temp_directory_path() / "ultramail_vault_locked";
    fs::remove_all(dir);
    CredentialVault vault(dir.string());

    // Nothing works before the master password is given — and nothing is
    // silently dropped: Store() reports the failure.
    REQUIRE(!vault.IsUnlocked());
    REQUIRE(!vault.Store("erika", "s3cr3t-p@ss"));
    std::string got;
    REQUIRE(!vault.Retrieve("erika", got));
    REQUIRE(!vault.Has("erika"));

    // An empty passphrase is refused: it would derive a reproducible key.
    REQUIRE(vault.Unlock("") != VaultStatus::Ok);
    REQUIRE(!vault.IsUnlocked());

    vault.Lock();
    fs::remove_all(dir);
}

TEST(credential_vault_roundtrip) {
    fs::path dir = fs::temp_directory_path() / "ultramail_vault_test";
    fs::remove_all(dir);
    CredentialVault vault(dir.string());

    REQUIRE_EQ(static_cast<int>(vault.Unlock(kMaster)), static_cast<int>(VaultStatus::Ok));
    REQUIRE(vault.IsUnlocked());

    REQUIRE(!vault.Has("erika"));
    REQUIRE(vault.Store("erika", "s3cr3t-p@ss"));
    REQUIRE(vault.Has("erika"));

    std::string got;
    REQUIRE(vault.Retrieve("erika", got));
    REQUIRE_EQ(got, std::string("s3cr3t-p@ss"));

    // The vault file must not contain the plaintext secret. The stream is
    // closed before Remove() rewrites the file: Windows will not replace a
    // file that is still open.
    {
        std::ifstream is(vault.VaultPath(), std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(is)), std::istreambuf_iterator<char>());
        REQUIRE(content.find("s3cr3t-p@ss") == std::string::npos);
    }

    // The 0.1 key file must not be recreated — the key is derived, not stored.
    REQUIRE(!fs::exists(dir / "vault.key"));

    REQUIRE(vault.Remove("erika"));
    REQUIRE(!vault.Has("erika"));

    vault.Lock();
    fs::remove_all(dir);
}

TEST(credential_vault_persists_across_instances) {
    fs::path dir = fs::temp_directory_path() / "ultramail_vault_persist";
    fs::remove_all(dir);
    {
        CredentialVault v(dir.string());
        REQUIRE_EQ(static_cast<int>(v.Unlock(kMaster)), static_cast<int>(VaultStatus::Ok));
        REQUIRE(v.Store("acc", "token-xyz"));
        v.Lock();
    }
    {
        CredentialVault v(dir.string());
        REQUIRE(v.Exists());
        REQUIRE_EQ(static_cast<int>(v.Unlock(kMaster)), static_cast<int>(VaultStatus::Ok));
        std::string got;
        REQUIRE(v.Retrieve("acc", got));
        REQUIRE_EQ(got, std::string("token-xyz"));
        v.Lock();
    }
    fs::remove_all(dir);
}

TEST(credential_vault_wrong_master_password_is_refused) {
    fs::path dir = fs::temp_directory_path() / "ultramail_vault_wrong";
    fs::remove_all(dir);
    {
        CredentialVault v(dir.string());
        REQUIRE_EQ(static_cast<int>(v.Unlock(kMaster)), static_cast<int>(VaultStatus::Ok));
        REQUIRE(v.Store("acc", "token-xyz"));
        v.Lock();
    }
    {
        CredentialVault v(dir.string());
        REQUIRE_EQ(static_cast<int>(v.Unlock("not the master password")),
                   static_cast<int>(VaultStatus::WrongPassphrase));
        REQUIRE(!v.IsUnlocked());
        std::string got;
        REQUIRE(!v.Retrieve("acc", got));
    }
    fs::remove_all(dir);
}

// A 0.1-format vault (XOR against a key file beside the data) is carried into
// UltraVault on the first unlock, and its files are removed.
TEST(credential_vault_migrates_legacy_format) {
    fs::path dir = fs::temp_directory_path() / "ultramail_vault_migrate";
    fs::remove_all(dir);
    fs::create_directories(dir);

    // Reproduce the 0.1 on-disk format: base64(account) TAB base64(xor(secret)).
    const std::vector<uint8_t> key(32, 0x5A);
    {
        std::ofstream ks(dir / "vault.key", std::ios::binary);
        ks.write(reinterpret_cast<const char*>(key.data()),
                 static_cast<std::streamsize>(key.size()));
    }
    auto xorWith = [&key](const std::string& in) {
        std::string out = in;
        for (std::size_t i = 0; i < out.size(); ++i)
            out[i] = static_cast<char>(static_cast<uint8_t>(out[i]) ^ key[i % key.size()]);
        return out;
    };
    auto b64 = [](const std::string& in) {
        return UltraNet_Base64Encode(std::vector<uint8_t>(in.begin(), in.end()), false);
    };
    {
        std::ofstream os(dir / "creds.dat");
        os << b64("legacy-acc") << '\t' << b64(xorWith("old-password")) << '\n';
    }

    CredentialVault vault(dir.string());
    REQUIRE_EQ(static_cast<int>(vault.Unlock(kMaster)), static_cast<int>(VaultStatus::Ok));

    // The secret came across...
    std::string got;
    REQUIRE(vault.Retrieve("legacy-acc", got));
    REQUIRE_EQ(got, std::string("old-password"));

    // ...and the weak files are gone.
    REQUIRE(!fs::exists(dir / "creds.dat"));
    REQUIRE(!fs::exists(dir / "vault.key"));

    vault.Lock();
    fs::remove_all(dir);
}
