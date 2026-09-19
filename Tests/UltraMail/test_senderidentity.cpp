// Tests/UltraMail/test_senderidentity.cpp
// Exercises what the sender badge rests on: the registrable-domain helpers,
// the known-brand registry (including the rule that a mailbox provider is not
// a brand and that a brand name worn by a foreign domain is not a match), the
// address-book index, the classification that turns all of it into a badge,
// and the icon cache with a fake fetcher.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "UltraMailSenderBrands.h"
#include "UltraMailSenderIconCache.h"
#include "UltraMailSenderTrust.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using namespace UltraMail;

namespace {

Contact MakeContact(const std::string& name, ContactSection section,
                    const std::string& email) {
    Contact c;
    c.displayName = name;
    c.section = section;
    ContactEmail e; e.address = email; e.primary = true;
    c.emails.push_back(e);
    return c;
}

// A minimal PNG header — enough for the cache's format sniffing.
std::vector<uint8_t> FakePng() {
    return { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x01, 0x02, 0x03 };
}

fs::path TempDir(const std::string& tag) {
    fs::path dir = fs::temp_directory_path() / ("ultramail-icons-" + tag);
    std::error_code ec;
    fs::remove_all(dir, ec);
    return dir;
}

} // namespace

// ---------------------------------------------------------------------------
// Domain helpers
// ---------------------------------------------------------------------------
TEST(domain_of_address_handles_angle_addr_and_case) {
    REQUIRE_EQ(DomainOfAddress("Erika <ERIKA@Mail.Example.COM>"),
               std::string("mail.example.com"));
    REQUIRE_EQ(DomainOfAddress("plain@example.org"), std::string("example.org"));
    REQUIRE_EQ(DomainOfAddress("no-domain"), std::string(""));
}

TEST(registrable_domain_uses_two_level_suffixes) {
    REQUIRE_EQ(RegistrableDomain("a.b.example.com"), std::string("example.com"));
    REQUIRE_EQ(RegistrableDomain("mail.example.co.uk"), std::string("example.co.uk"));
    REQUIRE_EQ(RegistrableDomain("example.com"), std::string("example.com"));
    REQUIRE_EQ(BaseLabel("shop.amazon.co.uk"), std::string("amazon"));
}

// ---------------------------------------------------------------------------
// The brand registry
// ---------------------------------------------------------------------------
TEST(brand_lookup_matches_service_domains) {
    const SenderBrand* fb = BrandForAddress("notification@facebookmail.com");
    REQUIRE(fb != nullptr);
    REQUIRE_EQ(fb->id, std::string("facebook"));

    const SenderBrand* claude = BrandForAddress("noreply@anthropic.com");
    REQUIRE(claude != nullptr);
    REQUIRE_EQ(claude->id, std::string("claude"));

    const SenderBrand* google = BrandForAddress("no-reply@accounts.google.com");
    REQUIRE(google != nullptr);
    REQUIRE_EQ(google->id, std::string("google"));

    // Country domains of a label-matched brand.
    const SenderBrand* amazon = BrandForAddress("versand@amazon.co.uk");
    REQUIRE(amazon != nullptr);
    REQUIRE_EQ(amazon->id, std::string("amazon"));
}

TEST(personal_mailbox_domains_are_not_brands) {
    // The user's requirement in one test: Google's *services* are Google, an
    // ordinary gmail.com address is just a person.
    REQUIRE(BrandForAddress("someone@gmail.com") == nullptr);
    REQUIRE(BrandForAddress("someone@googlemail.com") == nullptr);
    REQUIRE(BrandForAddress("someone@icloud.com") == nullptr);
    REQUIRE(BrandForAddress("someone@hotmail.co.uk") == nullptr);
    REQUIRE(IsPersonalMailboxDomain("gmx.de"));
    REQUIRE(!IsPersonalMailboxDomain("apple.com"));
}

TEST(a_brand_name_in_a_foreign_domain_is_not_that_brand) {
    // The whole point of matching on the registrable domain: a phishing host
    // that wears the name must not be handed the brand's icon.
    REQUIRE(BrandForDomain("amazon.secure-login.ru") == nullptr);
    REQUIRE(BrandForDomain("paypal.verify-account.example.com") == nullptr);
    REQUIRE(!DomainBelongsToBrand("apple-id.example.net", *BrandById("apple")));
    REQUIRE(DomainBelongsToBrand("email.apple.com", *BrandById("apple")));
}

TEST(brand_named_in_text_matches_whole_words_only) {
    const SenderBrand* paypal = BrandNamedIn("PayPal Service Team");
    REQUIRE(paypal != nullptr);
    REQUIRE_EQ(paypal->id, std::string("paypal"));
    // A short name ("X") is claimed through its keywords, never as a bare word.
    REQUIRE(BrandNamedIn("Re: x") == nullptr);
    REQUIRE(BrandNamedIn("nothing familiar here") == nullptr);
}

// ---------------------------------------------------------------------------
// The address book index
// ---------------------------------------------------------------------------
TEST(contact_index_is_case_insensitive_and_prefers_private_sections) {
    ContactIndex index;
    index.Build({ MakeContact("Anna", ContactSection::Friends, "Anna@Example.com"),
                  MakeContact("ACME", ContactSection::Work, "billing@acme.test") });
    ContactSection section = ContactSection::Other;
    REQUIRE(index.Lookup("anna@example.com", section));
    REQUIRE(section == ContactSection::Friends);
    REQUIRE(index.Lookup("Erika <BILLING@acme.test>", section));
    REQUIRE(section == ContactSection::Work);
    REQUIRE(!index.Contains("stranger@example.com"));

    // The same address in two contacts keeps the private section.
    index.Add("anna@example.com", ContactSection::Services);
    REQUIRE(index.Lookup("anna@example.com", section));
    REQUIRE(section == ContactSection::Friends);
}

// ---------------------------------------------------------------------------
// Classification — the six badge states
// ---------------------------------------------------------------------------
TEST(classification_covers_every_badge_state) {
    ContactIndex index;
    index.Build({ MakeContact("Anna", ContactSection::Friends, "anna@example.com"),
                  MakeContact("ACME", ContactSection::Work, "billing@acme.test") });

    SenderIdentity who;
    who.address = "anna@example.com";
    REQUIRE(ClassifySender(who, index).cls == SenderClass::Friend);

    who.address = "billing@acme.test";
    REQUIRE(ClassifySender(who, index).cls == SenderClass::Business);

    who.address = "stranger@example.net";
    REQUIRE(ClassifySender(who, index).cls == SenderClass::New);

    who.bulk = true;
    REQUIRE(ClassifySender(who, index).cls == SenderClass::Advertisement);

    who.bulk = false;
    who.level = ThreatLevel::Suspicious;
    REQUIRE(ClassifySender(who, index).cls == SenderClass::Spam);

    who.level = ThreatLevel::Scam;
    REQUIRE(ClassifySender(who, index).cls == SenderClass::Scam);
}

TEST(a_scam_verdict_outranks_the_address_book) {
    // An address book entry says who an address belongs to, not that this
    // message really came from them — so a scam stays a scam.
    ContactIndex index;
    index.Build({ MakeContact("Anna", ContactSection::Friends, "anna@example.com") });
    SenderIdentity who;
    who.address = "anna@example.com";
    who.level   = ThreatLevel::Scam;
    const SenderStatus status = ClassifySender(who, index);
    REQUIRE(status.cls == SenderClass::Scam);
    REQUIRE(status.Dangerous());
}

TEST(junk_folder_marks_an_unknown_sender_as_spam) {
    ContactIndex index;
    SenderIdentity who;
    who.address    = "stranger@example.net";
    who.junkFolder = true;
    REQUIRE(ClassifySender(who, index).cls == SenderClass::Spam);
}

TEST(a_known_service_carries_its_brand_into_the_badge) {
    ContactIndex index;
    SenderIdentity who;
    who.address = "notify@linkedin.com";
    const SenderStatus status = ClassifySender(who, index);
    REQUIRE_EQ(status.brandId, std::string("linkedin"));
    REQUIRE_EQ(status.brandName, std::string("LinkedIn"));
    REQUIRE(status.cls == SenderClass::New);   // known service, still not a contact
}

// ---------------------------------------------------------------------------
// The icon cache
// ---------------------------------------------------------------------------
TEST(icon_cache_stores_fetches_and_remembers_misses) {
    const fs::path dir = TempDir("fetch");
    SenderIconCache cache;
    cache.SetRoot(dir.string());

    int calls = 0;
    cache.SetFetcher([&calls](const std::string& url, std::vector<uint8_t>& out) {
        ++calls;
        if (url.find("facebook") == std::string::npos) return false;   // only one works
        out = FakePng();
        return true;
    });

    // Nothing cached yet.
    REQUIRE(cache.IconForAddress("notification@facebookmail.com").empty());

    const std::string path = cache.EnsureIconForAddress("notification@facebookmail.com");
    REQUIRE(!path.empty());
    REQUIRE(fs::exists(path));
    REQUIRE(path.substr(path.size() - 4) == ".png");   // sniffed from the bytes
    REQUIRE_EQ(calls, 1);

    // A second look is answered from disk, not from the network.
    REQUIRE_EQ(cache.EnsureIconForAddress("other@facebook.com"), path);
    REQUIRE_EQ(calls, 1);
    REQUIRE_EQ(cache.IconForBrand("facebook"), path);

    // A failed fetch is remembered, so it is not retried on the next message.
    REQUIRE(cache.EnsureIconForAddress("news@linkedin.com").empty());
    REQUIRE_EQ(calls, 2);
    REQUIRE(cache.EnsureIconForAddress("news@linkedin.com").empty());
    REQUIRE_EQ(calls, 2);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(icon_cache_never_asks_about_an_unknown_domain) {
    const fs::path dir = TempDir("unknown");
    SenderIconCache cache;
    cache.SetRoot(dir.string());
    int calls = 0;
    cache.SetFetcher([&calls](const std::string&, std::vector<uint8_t>& out) {
        ++calls;
        out = FakePng();
        return true;
    });
    // Neither a stranger's domain nor a personal mailbox is in the registry, so
    // nothing about the user's correspondents ever leaves the machine.
    REQUIRE(cache.EnsureIconForAddress("someone@private-company.example").empty());
    REQUIRE(cache.EnsureIconForAddress("friend@gmail.com").empty());
    REQUIRE_EQ(calls, 0);
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(icon_cache_respects_the_download_preference) {
    const fs::path dir = TempDir("offline");
    SenderIconCache cache;
    cache.SetRoot(dir.string());
    cache.SetNetworkEnabled(false);
    int calls = 0;
    cache.SetFetcher([&calls](const std::string&, std::vector<uint8_t>& out) {
        ++calls;
        out = FakePng();
        return true;
    });
    REQUIRE(cache.EnsureIconForAddress("notification@facebookmail.com").empty());
    REQUIRE_EQ(calls, 0);

    // An icon already in the folder is still shown when downloading is off.
    REQUIRE(!cache.Store("facebook", FakePng()).empty());
    REQUIRE(!cache.IconForAddress("notification@facebookmail.com").empty());
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(icon_cache_rejects_bytes_that_are_not_an_image) {
    const fs::path dir = TempDir("notimage");
    SenderIconCache cache;
    cache.SetRoot(dir.string());
    const std::string html = "<!DOCTYPE html><html><body>Sign in to the Wi-Fi</body></html>";
    REQUIRE(cache.Store("facebook", { html.begin(), html.end() }).empty());
    REQUIRE_EQ(cache.CachedCount(), 0);
    std::error_code ec;
    fs::remove_all(dir, ec);
}
