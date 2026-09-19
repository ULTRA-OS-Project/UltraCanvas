// Apps/UltraMail/engine/UltraMailSenderBrands.h
// The known-sender registry: the curated table of the services whose mail an
// inbox actually carries (Facebook, LinkedIn, X, Claude, Instagram, the Google
// and Apple services, PayPal, Amazon, the parcel carriers …), keyed by the
// *registrable* domain of the envelope From address.
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
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace UltraMail {

// One entry of the registry. `id` doubles as the icon file's base name in the
// sender-icon cache, and `accentRgb` is the brand colour the monogram tile
// falls back to until (or unless) the real icon is cached.
struct SenderBrand {
    std::string id;         // "facebook"
    std::string name;       // "Facebook"
    std::string iconUrl;    // official icon, fetched into the cache on first sight
    uint32_t    accentRgb = 0x5B6470;   // 0xRRGGBB
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
// must be a word of its own, not a substring of another.
const SenderBrand* BrandNamedIn(const std::string& text);

// Every brand in the registry, in table order (the icon cache warms from this).
const std::vector<SenderBrand>& KnownBrands();

} // namespace UltraMail
