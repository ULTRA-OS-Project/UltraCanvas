// Apps/UltraMail/engine/UltraMailSenderBrands.cpp
// Version: 0.3.0 - the table moves to UltraMailSenderBrandTable.cpp and grows to
//                  ~400 brands; indexed lookup; keyword-only claims; a
//                  mailbox-provider address in a display name claims nothing
// Version: 0.2.0 - dating services (Tinder, Bumble, Hinge, OkCupid, Parship)
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailSenderBrands.h"
#include "UltraMailSenderBrandTable.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <unordered_map>

namespace UltraMail {

namespace {

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// The two-level public suffixes common enough to matter for mail. An unknown
// suffix falls back to "last two labels", which is right for every gTLD.
const std::set<std::string>& TwoLevelSuffixes() {
    static const std::set<std::string> s = {
        "co.uk", "org.uk", "ac.uk", "gov.uk", "me.uk", "ltd.uk", "plc.uk",
        "co.jp", "ne.jp", "or.jp", "ac.jp", "go.jp",
        "co.kr", "or.kr", "co.nz", "net.nz", "org.nz",
        "co.za", "org.za", "co.in", "net.in", "org.in", "co.il", "co.id",
        "com.au", "net.au", "org.au", "edu.au", "gov.au",
        "com.br", "com.cn", "net.cn", "org.cn", "com.mx", "com.tr", "com.sg",
        "com.hk", "com.tw", "com.ar", "com.co", "com.pl", "com.ua", "com.my",
        "com.ph", "com.vn", "com.pe", "com.ec", "com.uy", "com.pk", "com.eg",
        "com.sa", "com.ng", "com.gr", "com.pt", "com.es", "com.ru",
        "com.be",
        // Government suffixes the registry's tax offices live under.
        "gov.in", "gouv.fr", "gc.ca",
    };
    return s;
}

// Three-level suffixes under which every name is a separate party: each
// "<service>.service.gov.uk" is a different UK government service.
const std::set<std::string>& ThreeLevelSuffixes() {
    static const std::set<std::string> s = { "service.gov.uk" };
    return s;
}

// A consumer mailbox provider: personal mail, not a service's.
const std::set<std::string>& PersonalMailboxDomains() {
    static const std::set<std::string> s = {
        "gmail.com", "googlemail.com",
        "outlook.com", "hotmail.com", "live.com", "msn.com", "passport.com",
        "yahoo.com", "ymail.com", "rocketmail.com",
        "aol.com", "aim.com",
        "icloud.com", "me.com", "mac.com",
        "gmx.net", "gmx.de", "gmx.at", "gmx.ch", "gmx.com", "web.de",
        "t-online.de", "freenet.de", "posteo.de", "mailbox.org",
        "proton.me", "protonmail.com", "protonmail.ch", "pm.me",
        "tutanota.com", "tutamail.com",
        "fastmail.com", "zoho.com", "mail.com", "mail.ru", "yandex.ru",
        "qq.com", "163.com", "126.com", "naver.com", "seznam.cz",
        "orange.fr", "wanadoo.fr", "free.fr", "laposte.net",
        "libero.it", "virgilio.it", "tiscali.it", "bluewin.ch",
        "btinternet.com", "sky.com", "comcast.net", "verizon.net", "att.net",
    };
    return s;
}

bool ContainsWord(const std::string& haystackLower, const std::string& wordLower) {
    if (wordLower.empty()) return false;
    std::size_t pos = 0;
    while ((pos = haystackLower.find(wordLower, pos)) != std::string::npos) {
        const bool leftOk = pos == 0 ||
            !std::isalnum(static_cast<unsigned char>(haystackLower[pos - 1]));
        const std::size_t end = pos + wordLower.size();
        const bool rightOk = end >= haystackLower.size() ||
            !std::isalnum(static_cast<unsigned char>(haystackLower[end]));
        if (leftOk && rightOk) return true;
        pos = end;
    }
    return false;
}

// A display name is often just the sender's own address ("jane@outlook.com"),
// and a subject can quote one. An address at a mailbox provider names that
// provider, not a brand the sender claims to be, so such a word is blanked
// out. An address at any other domain stays: "service@paypal.com" as the
// display name of mail from elsewhere is exactly the claim to catch.
std::string WithoutMailboxAddresses(std::string text) {
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
        std::size_t end = i;
        while (end < text.size() && !std::isspace(static_cast<unsigned char>(text[end]))) ++end;
        const std::size_t at = text.find('@', i);
        if (at < end) {
            std::string domain = text.substr(at + 1, end - at - 1);
            while (!domain.empty() && !std::isalnum(static_cast<unsigned char>(domain.back())))
                domain.pop_back();
            if (IsPersonalMailboxDomain(domain))
                std::fill(text.begin() + static_cast<std::ptrdiff_t>(i),
                          text.begin() + static_cast<std::ptrdiff_t>(end), ' ');
        }
        i = end;
    }
    return text;
}

// The registry indexed for lookup: BrandForAddress runs for every message a
// folder lists, so it must not walk ~600 domains each time.
struct BrandIndex {
    std::unordered_map<std::string, const SenderBrand*> byDomain;   // registrable domain
    std::unordered_map<std::string, const SenderBrand*> byLabel;    // base label, any suffix
    std::unordered_map<std::string, const SenderBrand*> byId;
};

const BrandIndex& Index() {
    static const BrandIndex index = [] {
        BrandIndex ix;
        for (const BrandRule& rule : SenderBrandRules()) {
            ix.byId.emplace(rule.brand.id, &rule.brand);
            for (const auto& d : rule.domains)
                ix.byDomain.emplace(RegistrableDomain(d), &rule.brand);   // first rule wins
            for (const auto& l : rule.labels)
                ix.byLabel.emplace(l, &rule.brand);
        }
        return ix;
    }();
    return index;
}

} // namespace

std::string ToString(BrandCategory category) {
    switch (category) {
        case BrandCategory::Social:         return "social";
        case BrandCategory::Messaging:      return "messaging";
        case BrandCategory::Crowdfunding:   return "crowdfunding";
        case BrandCategory::CreatorSupport: return "creator-support";
        case BrandCategory::Shopping:       return "shopping";
        case BrandCategory::Payment:        return "payment";
        case BrandCategory::Technology:     return "technology";
        case BrandCategory::Media:          return "media";
        case BrandCategory::Travel:         return "travel";
        case BrandCategory::Delivery:       return "delivery";
        case BrandCategory::Banking:        return "banking";
        case BrandCategory::Crypto:         return "crypto";
        case BrandCategory::CloudHosting:   return "cloud-hosting";
        case BrandCategory::DomainRegistrar:return "domain-registrar";
        case BrandCategory::Government:     return "government";
        case BrandCategory::Telecom:        return "telecom";
        case BrandCategory::Gaming:         return "gaming";
        case BrandCategory::Security:       return "security";
    }
    return "technology";
}

std::string DisplayName(BrandCategory category) {
    switch (category) {
        case BrandCategory::Social:         return "Social network";
        case BrandCategory::Messaging:      return "Messaging service";
        case BrandCategory::Crowdfunding:   return "Crowdfunding platform";
        case BrandCategory::CreatorSupport: return "Creator support platform";
        case BrandCategory::Shopping:       return "Online shop";
        case BrandCategory::Payment:        return "Payment service";
        case BrandCategory::Technology:     return "Online service";
        case BrandCategory::Media:          return "Media service";
        case BrandCategory::Travel:         return "Travel service";
        case BrandCategory::Delivery:       return "Parcel carrier";
        case BrandCategory::Banking:        return "Bank or broker";
        case BrandCategory::Crypto:         return "Crypto exchange or wallet";
        case BrandCategory::CloudHosting:   return "Cloud, hosting or file service";
        case BrandCategory::DomainRegistrar:return "Domain registrar";
        case BrandCategory::Government:     return "Government agency";
        case BrandCategory::Telecom:        return "Telecom provider";
        case BrandCategory::Gaming:         return "Gaming service";
        case BrandCategory::Security:       return "Security software";
    }
    return "Online service";
}

std::string DomainOfAddress(const std::string& address) {
    // Take the angle-addr when there is one, then everything after the last '@'.
    std::string addr = address;
    const std::size_t lt = addr.find('<');
    if (lt != std::string::npos) {
        const std::size_t gt = addr.find('>', lt);
        addr = addr.substr(lt + 1, gt == std::string::npos ? std::string::npos : gt - lt - 1);
    }
    const std::size_t at = addr.rfind('@');
    if (at == std::string::npos) return "";
    std::string domain = Lower(addr.substr(at + 1));
    while (!domain.empty() && (domain.back() == '.' || domain.back() == '>' ||
                               std::isspace(static_cast<unsigned char>(domain.back()))))
        domain.pop_back();
    return domain;
}

std::string RegistrableDomain(const std::string& domain) {
    std::string d = Lower(domain);
    while (!d.empty() && d.back() == '.') d.pop_back();
    std::vector<std::string> labels;
    std::size_t start = 0;
    while (start <= d.size()) {
        const std::size_t dot = d.find('.', start);
        labels.push_back(d.substr(start, dot == std::string::npos ? std::string::npos
                                                                 : dot - start));
        if (dot == std::string::npos) break;
        start = dot + 1;
    }
    if (labels.size() <= 2) return d;
    const std::string lastTwo = labels[labels.size() - 2] + "." + labels.back();
    std::size_t take = TwoLevelSuffixes().count(lastTwo) ? 3u : 2u;
    if (take == 3 && labels.size() >= 3 &&
        ThreeLevelSuffixes().count(labels[labels.size() - 3] + "." + lastTwo))
        take = 4;
    if (labels.size() <= take) return d;
    std::string out;
    for (std::size_t i = labels.size() - take; i < labels.size(); ++i) {
        if (!out.empty()) out.push_back('.');
        out += labels[i];
    }
    return out;
}

std::string BaseLabel(const std::string& domain) {
    const std::string reg = RegistrableDomain(domain);
    const std::size_t dot = reg.find('.');
    return dot == std::string::npos ? reg : reg.substr(0, dot);
}

bool IsPersonalMailboxDomain(const std::string& domain) {
    const std::string reg = RegistrableDomain(domain);
    if (PersonalMailboxDomains().count(reg)) return true;
    // Country variants of the big providers (hotmail.co.uk, yahoo.de, gmx.fr).
    static const std::set<std::string> labels = {
        "hotmail", "yahoo", "gmx", "aol", "live", "outlook", "laposte", "orange",
    };
    return labels.count(BaseLabel(reg)) > 0;
}

const SenderBrand* BrandForDomain(const std::string& domain) {
    if (domain.empty()) return nullptr;
    const std::string reg = RegistrableDomain(domain);
    if (IsPersonalMailboxDomain(reg)) return nullptr;   // a mailbox, not a brand
    const BrandIndex& ix = Index();
    if (const auto it = ix.byDomain.find(reg); it != ix.byDomain.end()) return it->second;
    if (const auto it = ix.byLabel.find(BaseLabel(reg)); it != ix.byLabel.end()) return it->second;
    return nullptr;
}

const SenderBrand* BrandForAddress(const std::string& address) {
    return BrandForDomain(DomainOfAddress(address));
}

const SenderBrand* BrandById(const std::string& id) {
    const BrandIndex& ix = Index();
    const auto it = ix.byId.find(id);
    return it == ix.byId.end() ? nullptr : it->second;
}

bool DomainBelongsToBrand(const std::string& domain, const SenderBrand& brand) {
    const SenderBrand* found = BrandForDomain(domain);
    return found && found->id == brand.id;
}

const SenderBrand* BrandNamedIn(const std::string& text) {
    if (text.empty()) return nullptr;
    const std::string lower = WithoutMailboxAddresses(Lower(text));
    for (const auto& rule : SenderBrandRules()) {
        // Names shorter than three letters ("X") would match half the prose in
        // an inbox, and a name that is an ordinary word ("Chase", "Visa") all
        // of it, so such a brand is claimed through its keywords only.
        if (rule.nameClaim == NameClaim::ByName && rule.brand.name.size() >= 3 &&
            ContainsWord(lower, Lower(rule.brand.name)))
            return &rule.brand;
        for (const auto& k : rule.keywords)
            if (ContainsWord(lower, k)) return &rule.brand;
    }
    return nullptr;
}

const std::vector<SenderBrand>& KnownBrands() {
    static const std::vector<SenderBrand> all = [] {
        std::vector<SenderBrand> v;
        for (const auto& rule : SenderBrandRules()) v.push_back(rule.brand);
        return v;
    }();
    return all;
}

} // namespace UltraMail
