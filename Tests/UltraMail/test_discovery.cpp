// Tests/UltraMail/test_discovery.cpp
// Account auto-discovery: provider presets, Mozilla-autoconfig XML parsing,
// username placeholder resolution and server-URL construction. All pure — no
// network.
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

    // The vault file must not contain the plaintext secret.
    std::ifstream is(vault.VaultPath(), std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(is)), std::istreambuf_iterator<char>());
    REQUIRE(content.find("s3cr3t-p@ss") == std::string::npos);

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
