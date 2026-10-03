// Apps/UltraMail/engine/UltraMailSenderBrandTable.h
// The data behind the known-sender registry (UltraMailSenderBrands.h): one
// rule per brand, saying which registrable domains are that brand's own and
// which words in a display name, a subject or a host label claim to be it.
// Internal to the engine; callers use the lookups in UltraMailSenderBrands.h.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMailSenderBrands.h"

#include <string>
#include <vector>

namespace UltraMail {

// Whether the brand's display name, on its own, claims the brand in free text.
// A name that is also an ordinary word ("Chase", "Target", "Visa", "Steam",
// "Booking") would turn every hotel confirmation and visa application into an
// impersonation finding, so such a brand is claimed through its keywords only
// ("chase bank", "jpmorgan", "target.com").
enum class NameClaim {
    ByName,
    KeywordsOnly
};

// `domains` are exact registrable domains (a subdomain such as
// "aws.amazon.com" reduces to its registrable domain). `labels` would match
// a registrable domain whose base label is that word under *any* public
// suffix - which trusts a squatted amazon.xyz as much as amazon.de - so no
// entry uses it any more (registry_is_consistent checks that); list the
// country domains in `domains` instead.
// `keywords` are lowercase words or phrases that claim the brand.
// An empty `brand.iconUrl` is filled in from the first domain.
struct BrandRule {
    SenderBrand              brand;
    std::vector<std::string> domains;
    std::vector<std::string> labels;
    std::vector<std::string> keywords;
    NameClaim                nameClaim = NameClaim::ByName;
};

// The registry, in table order. Earlier rules win a keyword both could claim.
const std::vector<BrandRule>& SenderBrandRules();

} // namespace UltraMail
