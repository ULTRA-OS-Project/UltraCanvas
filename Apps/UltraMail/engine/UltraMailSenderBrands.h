// Apps/UltraMail/engine/UltraMailSenderBrands.h
// The known-sender registry: the curated table of the services whose mail an
// inbox actually carries (Facebook, LinkedIn, X, Claude, Instagram, the Google
// and Apple services, PayPal, Amazon, the parcel carriers …) and of the brands
// phishing most often dresses up as (banks, payment services, crypto
// exchanges, shops, cloud and hosting services, domain registrars, tax
// offices, telecoms …), keyed by the *registrable* domain of the envelope
// From address. The data is in UltraMailSenderBrandTable.cpp.
//
// Two rules hold this together, and both exist because the table is also what
// the phishing scan reasons about:
//
//  * A brand is matched on the registrable domain only — never on a display
//    name, never on a label anywhere in the host. "amazon.secure-login.ru" is
//    not Amazon, and must not be handed Amazon's icon.
//  * A mailbox provider is not a brand. Mail from gmail.com, icloud.com or
//    gmx.net is personal mail that happens to be carried by Google, Apple or
//    GMX, so those domains resolve to no brand at all — only a Google *service*
//    domain (google.com, youtube.com) is Google.
//
// Version: 0.4.0 - DomainToUnicode (punycode); BrandImitatedByDomain finds
//                  names written with look-alike letters of another script
// Version: 0.3.0 - BrandImitatedByDomain: a domain dressed up as a brand's
// Version: 0.2.0 - banking, crypto, cloud/hosting, domain, government, telecom,
//                  gaming and security categories
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace UltraMail {

// What a service is, for the badge tooltip and for the address book: a brand
// the user hears from is a business relationship, and knowing what kind of one
// is what makes a collected contact worth reading later.
enum class BrandCategory {
    Social = 0,
    Messaging,
    Crowdfunding,   // Kickstarter, Indiegogo, GoFundMe …
    CreatorSupport, // Patreon, Buy Me a Coffee, Ko-fi, Liberapay …
    Shopping,
    Payment,
    Technology,
    Media,
    Travel,
    Delivery,
    // Appended, so the earlier values keep their numbers.
    Banking,         // banks, brokers, card issuers
    Crypto,          // exchanges and wallets
    CloudHosting,    // cloud, web hosting, file sharing
    DomainRegistrar, // registrars and registries
    Government,      // tax offices and agencies
    Telecom,
    Gaming,
    Security         // antivirus, password managers, VPNs
};

std::string ToString(BrandCategory category);      // stable identifier
std::string DisplayName(BrandCategory category);   // "Crowdfunding platform", …

// One entry of the registry. `id` doubles as the icon file's base name in the
// sender-icon cache, and `accentRgb` is the brand colour the monogram tile
// falls back to until (or unless) the real icon is cached.
struct SenderBrand {
    std::string   id;         // "facebook"
    std::string   name;       // "Facebook"
    std::string   iconUrl;    // the site's own favicon, fetched on first sight
    uint32_t      accentRgb = 0x5B6470;   // 0xRRGGBB
    BrandCategory category  = BrandCategory::Technology;
};

// ---------------------------------------------------------------------------
// Domain helpers
// ---------------------------------------------------------------------------
// "Erika <ERIKA@Mail.Example.COM>" -> "mail.example.com" (lowercased, no
// trailing dot). Empty when the address carries no domain.
std::string DomainOfAddress(const std::string& address);

// The registrable domain: "mail.example.co.uk" -> "example.co.uk",
// "a.b.example.com" -> "example.com". Uses a small two-level public-suffix
// list; an unknown suffix falls back to the last two labels.
std::string RegistrableDomain(const std::string& domain);

// The label in front of the public suffix: "amazon.co.uk" -> "amazon".
std::string BaseLabel(const std::string& domain);

// True for a consumer mailbox provider (gmail.com, icloud.com, gmx.net …).
// Mail from one of these is personal mail, never a service's.
bool IsPersonalMailboxDomain(const std::string& domain);

// ---------------------------------------------------------------------------
// Lookup
// ---------------------------------------------------------------------------
// The brand a host belongs to, or nullptr. Matching is on the registrable
// domain; a personal mailbox domain never matches.
const SenderBrand* BrandForDomain(const std::string& domain);
const SenderBrand* BrandForAddress(const std::string& address);
const SenderBrand* BrandById(const std::string& id);

// True when `domain` is one of `brand`'s own domains — the question the
// impersonation check asks ("this mail says PayPal; is it from PayPal?").
bool DomainBelongsToBrand(const std::string& domain, const SenderBrand& brand);

// The brand a piece of free text claims to be, or nullptr: matches a brand
// name or one of its keywords as a whole word, case-insensitively. Used on
// display names and subjects ("Apple ID Support"), and on host labels
// ("apple-id-verify.example.com"), so it is deliberately narrow — a keyword
// must be a word of its own, not a substring of another, a brand whose name
// is an ordinary word is claimed through its keywords only, and an address
// at a mailbox provider inside the text ("jane@outlook.com") claims nothing.
const SenderBrand* BrandNamedIn(const std::string& text);

// A domain dressed up as a brand's - the sender's address itself pretending.
// Three ways, each on a domain that is none of the brand's own:
//  * Name: the brand's name plus words phishing pads it with -
//    "paypal-secure-login.com", "appleidverify.com" ("secure", "login",
//    "verify", "account", "support", "inbox" …). The bare name under another
//    suffix ("paypal.xyz") is not claimed either way, as the table says, and a
//    name next to an ordinary word is no claim ("applewood", "amazonas-reisen").
//  * Misspelt: the name with look-alike characters ("amaz0n", "paypa1",
//    "rnicrosoft"), a doubled letter ("paypall", "faceebook"), or - for names
//    of eight letters or more - one letter added, dropped, changed or swapped
//    ("facebok"), with or without padding: "faceebookinbox.biz".
//  * OwnDomain: one of the brand's own domains in front of an unrelated one:
//    "paypal.com.account-check.ru".
//  * Homograph: the name written with letters of another script that look
//    like Latin ones - "pаypal.com" with a Cyrillic "а", which arrives as
//    "xn--pypal-4ve.com" - bare or padded with any word: such a domain is
//    made to deceive.
// The real brand names are padded with phishing words only; a misspelt or
// foreign-lettered name with ordinary ones too ("faceboookmail.com"), since
// no brand misspells itself. Personal mailbox domains and every brand's own
// domains are never flagged.
enum class LookalikeKind { None, Name, Misspelt, OwnDomain, Homograph };
struct DomainLookalike {
    const SenderBrand* brand = nullptr;
    LookalikeKind      kind  = LookalikeKind::None;
    std::string        worn;      // the part that looks like the brand: "faceebook", "pаypal"
    std::string        letters;   // Homograph: whose letters - "Cyrillic", "Greek and accented"
    std::string        unicode;   // Homograph: the domain as it reads ("pаypal.com")
};
DomainLookalike BrandImitatedByDomain(const std::string& domain);

// An internationalised domain as it reads: every punycode label ("xn--…",
// RFC 3492) decoded to UTF-8; other labels as they are. A label that does not
// decode is kept as written.
std::string DomainToUnicode(const std::string& domain);

// Every brand in the registry, in table order (the icon cache warms from this).
const std::vector<SenderBrand>& KnownBrands();

} // namespace UltraMail
