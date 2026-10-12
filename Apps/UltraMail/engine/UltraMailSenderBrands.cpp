// Apps/UltraMail/engine/UltraMailSenderBrands.cpp
// Version: 0.5.0 - DomainToUnicode (punycode, RFC 3492); names written in
//                  look-alike letters of another script (Homograph); a real
//                  name is padded with phishing words only, a misspelt one
//                  with ordinary words too
// Version: 0.4.0 - BrandImitatedByDomain: names padded with phishing words,
//                  misspelt names, a brand's own domain in front of another
// Version: 0.3.1 - the Russian, Ukrainian and Belarusian mailbox providers
//                  (i.ua, ukr.net, bk.ru, rambler.ru, ...) and foxmail.com are
//                  mailboxes
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
        // Russia, Ukraine and Belarus: where the "bride" letters write from.
        "bk.ru", "list.ru", "inbox.ru", "internet.ru", "rambler.ru", "yandex.com",
        "yandex.ua", "ya.ru", "i.ua", "ukr.net", "meta.ua", "bigmir.net", "email.ua",
        "tut.by",
        "qq.com", "foxmail.com", "163.com", "126.com", "naver.com", "seznam.cz",
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

// ---------------------------------------------------------------------------
// Look-alike domains
// ---------------------------------------------------------------------------
namespace {

// The words a phishing domain pads a brand's real name with. Only these: a
// name next to any other word is a business of its own ("applewood-estates"),
// or the brand's own second domain ("redditmail.com", "zoomcare.com",
// "cdn.discordapp.com").
const std::vector<std::string>& PhishingFillers() {
    static const std::vector<std::string> v = {
        "secure", "security", "login", "logon", "signin", "verify", "verification",
        "validate", "validation", "account", "accounts", "acct", "support", "helpdesk",
        "help", "service", "services", "id", "inbox", "notify", "notification",
        "notifications", "alert", "alerts", "update", "updates", "billing", "invoice",
        "invoices", "payment", "payments", "refund", "refunds", "center", "centre",
        "customer", "customers", "official", "auth", "access", "unlock", "confirm",
        "confirmation", "recovery", "reset", "check", "admin", "member", "members",
        "portal", "the", "client", "safe", "safety", "protect", "protection",
        "message", "messages", "msg", "desk", "dept", "claim", "claims", "reward",
        "rewards", "prize", "bonus", "gift", "winner", "kunden", "konto", "sicherheit",
        "anmelden", "hilfe", "zahlung", "rechnung", "www",
    };
    return v;
}

// And the ordinary words beside them, which pad a name only once it is
// misspelt or written in foreign letters - no brand does either to itself.
const std::vector<std::string>& AnyFillers() {
    static const std::vector<std::string> v = [] {
        std::vector<std::string> all = PhishingFillers();
        for (const char* w : { "mail", "email", "app", "apps", "web", "mobile", "online",
                               "care", "team", "info", "my", "us", "usa", "uk", "de", "eu",
                               "intl", "global", "24", "365", "now", "new", "news", "shop",
                               "store", "pay", "plus", "live", "tv", "cloud", "group", "hq",
                               "net" })
            all.push_back(w);
        return all;
    }();
    return v;
}

// Where a run of fillers can end, reading from the front (0 always), and
// where one can start, reading from the back (the length always).
std::vector<std::size_t> FillerRunEnds(const std::string& s,
                                       const std::vector<std::string>& fillers) {
    std::vector<bool> ok(s.size() + 1, false);
    ok[0] = true;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (!ok[i]) continue;
        for (const auto& f : fillers)
            if (s.compare(i, f.size(), f) == 0) ok[i + f.size()] = true;
    }
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i <= s.size(); ++i) if (ok[i]) out.push_back(i);
    return out;
}

std::vector<std::size_t> FillerRunStarts(const std::string& s,
                                         const std::vector<std::string>& fillers) {
    std::vector<bool> ok(s.size() + 1, false);
    ok[s.size()] = true;
    for (std::size_t j = s.size(); j > 0; --j) {
        if (!ok[j]) continue;
        for (const auto& f : fillers)
            if (f.size() <= j && s.compare(j - f.size(), f.size(), f) == 0)
                ok[j - f.size()] = true;
    }
    std::vector<std::size_t> out;
    for (std::size_t j = 0; j <= s.size(); ++j) if (ok[j]) out.push_back(j);
    return out;
}

// What a word looks like to a hurried eye: 0 o, 1 i l, 3 e, 4 a, 5 s, 7 t,
// rn m, vv w.
std::string Skeleton(const std::string& s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == 'r' && i + 1 < s.size() && s[i + 1] == 'n') { out += 'm'; ++i; continue; }
        if (c == 'v' && i + 1 < s.size() && s[i + 1] == 'v') { out += 'w'; ++i; continue; }
        switch (c) {
            case '0': c = 'o'; break;
            case '1': case 'i': c = 'l'; break;
            case '3': c = 'e'; break;
            case '4': c = 'a'; break;
            case '5': c = 's'; break;
            case '7': c = 't'; break;
            default: break;
        }
        out += c;
    }
    return out;
}

// One letter added, dropped, changed, or two neighbours swapped.
bool OneEditApart(const std::string& a, const std::string& b) {
    if (a == b) return false;
    if (a.size() == b.size()) {
        std::vector<std::size_t> diff;
        for (std::size_t i = 0; i < a.size() && diff.size() <= 2; ++i)
            if (a[i] != b[i]) diff.push_back(i);
        if (diff.size() == 1) return true;
        return diff.size() == 2 && diff[1] == diff[0] + 1 && a[diff[0]] == b[diff[1]] &&
               a[diff[1]] == b[diff[0]];
    }
    const std::string& longer  = a.size() > b.size() ? a : b;
    const std::string& shorter = a.size() > b.size() ? b : a;
    if (longer.size() != shorter.size() + 1) return false;
    for (std::size_t i = 0; i < longer.size(); ++i)
        if (longer.substr(0, i) + longer.substr(i + 1) == shorter) return true;
    return false;
}

// `m` is `name` with one of its letters doubled ("paypall", "faceebook").
bool DoubledLetter(const std::string& m, const std::string& name) {
    if (m.size() != name.size() + 1) return false;
    for (std::size_t k = 1; k < m.size(); ++k)
        if (m[k] == m[k - 1] && m.substr(0, k) + m.substr(k + 1) == name) return true;
    return false;
}

std::string Alnum(const std::string& s) {
    std::string out;
    for (char c : Lower(s)) if (std::isalnum(static_cast<unsigned char>(c))) out += c;
    return out;
}

// ---- Internationalised domains -------------------------------------------

// RFC 3492: the code points of a punycode label (without its "xn--").
bool PunycodeDecode(const std::string& in, std::u32string& out) {
    constexpr uint32_t kBase = 36, kTMin = 1, kTMax = 26, kSkew = 38, kDamp = 700;
    out.clear();
    const std::size_t delim = in.rfind('-');
    std::size_t pos = 0;
    if (delim != std::string::npos) {
        for (std::size_t k = 0; k < delim; ++k) {
            if (static_cast<unsigned char>(in[k]) >= 0x80) return false;
            out.push_back(static_cast<unsigned char>(in[k]));
        }
        pos = delim + 1;
    }
    auto adapt = [&](uint32_t delta, uint32_t points, bool first) {
        delta = first ? delta / kDamp : delta / 2;
        delta += delta / points;
        uint32_t k = 0;
        while (delta > ((kBase - kTMin) * kTMax) / 2) {
            delta /= kBase - kTMin;
            k += kBase;
        }
        return k + (kBase - kTMin + 1) * delta / (delta + kSkew);
    };
    uint32_t n = 128, i = 0, bias = 72;
    while (pos < in.size()) {
        const uint32_t oldi = i;
        uint32_t w = 1;
        for (uint32_t k = kBase;; k += kBase) {
            if (pos >= in.size()) return false;
            const char c = in[pos++];
            uint32_t digit;
            if (c >= 'a' && c <= 'z') digit = static_cast<uint32_t>(c - 'a');
            else if (c >= 'A' && c <= 'Z') digit = static_cast<uint32_t>(c - 'A');
            else if (c >= '0' && c <= '9') digit = static_cast<uint32_t>(c - '0') + 26;
            else return false;
            if (digit > (0x7FFFFFFFu - i) / w) return false;
            i += digit * w;
            const uint32_t t = k <= bias ? kTMin : (k >= bias + kTMax ? kTMax : k - bias);
            if (digit < t) break;
            if (w > 0x7FFFFFFFu / (kBase - t)) return false;
            w *= kBase - t;
        }
        const uint32_t points = static_cast<uint32_t>(out.size()) + 1;
        bias = adapt(i - oldi, points, oldi == 0);
        if (i / points > 0x10FFFFu - n) return false;
        n += i / points;
        i %= points;
        out.insert(out.begin() + i, static_cast<char32_t>(n));
        ++i;
    }
    return true;
}

void AppendUtf8(std::string& s, char32_t cp) {
    if (cp < 0x80) { s += static_cast<char>(cp); return; }
    if (cp < 0x800) {
        s += static_cast<char>(0xC0 | (cp >> 6));
    } else if (cp < 0x10000) {
        s += static_cast<char>(0xE0 | (cp >> 12));
        s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    } else {
        s += static_cast<char>(0xF0 | (cp >> 18));
        s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    }
    s += static_cast<char>(0x80 | (cp & 0x3F));
}

std::u32string DecodeUtf8(const std::string& s) {
    std::u32string out;
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        int len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
        if (len == 0 || i + static_cast<std::size_t>(len) > s.size()) { out.push_back(0xFFFD); ++i; continue; }
        char32_t cp = len == 1 ? c : len == 2 ? (c & 0x1F) : len == 3 ? (c & 0x0F) : (c & 0x07);
        for (int k = 1; k < len; ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        out.push_back(cp);
        i += static_cast<std::size_t>(len);
    }
    return out;
}

// The Latin letter a character of another script (or an accented one) is
// read as, and whose letters they are; 0 when it has no Latin look-alike.
char LatinLookalike(char32_t cp, const char** script) {
    struct Range { char32_t from, to; char latin; const char* script; };
    static const Range ranges[] = {
        // Accented Latin (Latin-1 Supplement, Latin Extended-A).
        { 0xC0, 0xC5, 'a', "accented" }, { 0xE0, 0xE5, 'a', "accented" },
        { 0xC7, 0xC7, 'c', "accented" }, { 0xE7, 0xE7, 'c', "accented" },
        { 0xC8, 0xCB, 'e', "accented" }, { 0xE8, 0xEB, 'e', "accented" },
        { 0xCC, 0xCF, 'i', "accented" }, { 0xEC, 0xEF, 'i', "accented" },
        { 0xD1, 0xD1, 'n', "accented" }, { 0xF1, 0xF1, 'n', "accented" },
        { 0xD2, 0xD6, 'o', "accented" }, { 0xD8, 0xD8, 'o', "accented" },
        { 0xF2, 0xF6, 'o', "accented" }, { 0xF8, 0xF8, 'o', "accented" },
        { 0xD9, 0xDC, 'u', "accented" }, { 0xF9, 0xFC, 'u', "accented" },
        { 0xDD, 0xDD, 'y', "accented" }, { 0xFD, 0xFD, 'y', "accented" },
        { 0xFF, 0xFF, 'y', "accented" },
        { 0x100, 0x105, 'a', "accented" }, { 0x106, 0x10D, 'c', "accented" },
        { 0x10E, 0x111, 'd', "accented" }, { 0x112, 0x11B, 'e', "accented" },
        { 0x11C, 0x123, 'g', "accented" }, { 0x124, 0x127, 'h', "accented" },
        { 0x128, 0x131, 'i', "accented" }, { 0x134, 0x135, 'j', "accented" },
        { 0x136, 0x138, 'k', "accented" }, { 0x139, 0x142, 'l', "accented" },
        { 0x143, 0x14B, 'n', "accented" }, { 0x14C, 0x151, 'o', "accented" },
        { 0x154, 0x159, 'r', "accented" }, { 0x15A, 0x161, 's', "accented" },
        { 0x162, 0x167, 't', "accented" }, { 0x168, 0x173, 'u', "accented" },
        { 0x174, 0x175, 'w', "accented" }, { 0x176, 0x178, 'y', "accented" },
        { 0x179, 0x17E, 'z', "accented" }, { 0x261, 0x261, 'g', "IPA" },
        // Greek.
        { 0x391, 0x391, 'a', "Greek" }, { 0x392, 0x392, 'b', "Greek" },
        { 0x395, 0x395, 'e', "Greek" }, { 0x396, 0x396, 'z', "Greek" },
        { 0x397, 0x397, 'h', "Greek" }, { 0x399, 0x399, 'i', "Greek" },
        { 0x39A, 0x39A, 'k', "Greek" }, { 0x39C, 0x39C, 'm', "Greek" },
        { 0x39D, 0x39D, 'n', "Greek" }, { 0x39F, 0x39F, 'o', "Greek" },
        { 0x3A1, 0x3A1, 'p', "Greek" }, { 0x3A4, 0x3A4, 't', "Greek" },
        { 0x3A5, 0x3A5, 'y', "Greek" }, { 0x3A7, 0x3A7, 'x', "Greek" },
        { 0x3B1, 0x3B1, 'a', "Greek" }, { 0x3B5, 0x3B5, 'e', "Greek" },
        { 0x3B9, 0x3B9, 'i', "Greek" }, { 0x3BA, 0x3BA, 'k', "Greek" },
        { 0x3BD, 0x3BD, 'v', "Greek" }, { 0x3BF, 0x3BF, 'o', "Greek" },
        { 0x3C1, 0x3C1, 'p', "Greek" }, { 0x3C4, 0x3C4, 't', "Greek" },
        { 0x3C5, 0x3C5, 'u', "Greek" }, { 0x3C7, 0x3C7, 'x', "Greek" },
        // Cyrillic.
        { 0x405, 0x405, 's', "Cyrillic" }, { 0x406, 0x406, 'i', "Cyrillic" },
        { 0x408, 0x408, 'j', "Cyrillic" }, { 0x410, 0x410, 'a', "Cyrillic" },
        { 0x412, 0x412, 'b', "Cyrillic" }, { 0x415, 0x415, 'e', "Cyrillic" },
        { 0x41A, 0x41A, 'k', "Cyrillic" }, { 0x41C, 0x41C, 'm', "Cyrillic" },
        { 0x41D, 0x41D, 'h', "Cyrillic" }, { 0x41E, 0x41E, 'o', "Cyrillic" },
        { 0x420, 0x420, 'p', "Cyrillic" }, { 0x421, 0x421, 'c', "Cyrillic" },
        { 0x422, 0x422, 't', "Cyrillic" }, { 0x423, 0x423, 'y', "Cyrillic" },
        { 0x425, 0x425, 'x', "Cyrillic" }, { 0x430, 0x430, 'a', "Cyrillic" },
        { 0x435, 0x435, 'e', "Cyrillic" }, { 0x43A, 0x43A, 'k', "Cyrillic" },
        { 0x43E, 0x43E, 'o', "Cyrillic" }, { 0x440, 0x440, 'p', "Cyrillic" },
        { 0x441, 0x441, 'c', "Cyrillic" }, { 0x443, 0x443, 'y', "Cyrillic" },
        { 0x445, 0x445, 'x', "Cyrillic" }, { 0x451, 0x451, 'e', "Cyrillic" },
        { 0x455, 0x455, 's', "Cyrillic" }, { 0x456, 0x457, 'i', "Cyrillic" },
        { 0x458, 0x458, 'j', "Cyrillic" }, { 0x4BB, 0x4BB, 'h', "Cyrillic" },
        { 0x4CF, 0x4CF, 'l', "Cyrillic" }, { 0x501, 0x501, 'd', "Cyrillic" },
        { 0x51B, 0x51B, 'q', "Cyrillic" }, { 0x51D, 0x51D, 'w', "Cyrillic" },
    };
    if (cp >= 0xFF10 && cp <= 0xFF5A) {   // full-width digits and letters
        const char32_t ascii = cp - 0xFEE0;
        if (std::isalnum(static_cast<int>(ascii))) {
            *script = "full-width";
            return static_cast<char>(std::tolower(static_cast<int>(ascii)));
        }
    }
    for (const Range& r : ranges)
        if (cp >= r.from && cp <= r.to) { *script = r.script; return r.latin; }
    return 0;
}

// Every brand's names as a domain label would spell them - the name and
// the keywords, alphanumerics only - and how each looks to a hurried eye.
struct LabelNames {
    const SenderBrand*       brand = nullptr;
    std::vector<std::string> names;
    std::vector<std::string> skeletons;
};

const std::vector<LabelNames>& BrandLabelNames() {
    static const std::vector<LabelNames> all = [] {
        std::vector<LabelNames> v;
        for (const auto& rule : SenderBrandRules()) {
            LabelNames entry;
            entry.brand = &rule.brand;
            const std::string own = Alnum(rule.brand.name);
            if (rule.nameClaim == NameClaim::ByName && own.size() >= 4) entry.names.push_back(own);
            for (const auto& k : rule.keywords) {
                const std::string kw = Alnum(k);
                if (kw.size() >= 5 && kw != own) entry.names.push_back(kw);
            }
            for (const auto& n : entry.names) entry.skeletons.push_back(Skeleton(n));
            if (!entry.names.empty()) v.push_back(std::move(entry));
        }
        return v;
    }();
    return all;
}

// The brand a label (its alphanumerics) is dressed as. `foreign`: the label
// was written with look-alike letters, so a bare name, or one padded with
// ordinary words, is a claim too.
DomainLookalike MatchLabel(const std::string& word, bool foreign) {
    DomainLookalike none;
    if (word.size() < 4) return none;
    const auto phishEnds = FillerRunEnds(word, PhishingFillers());
    const auto phishStarts = FillerRunStarts(word, PhishingFillers());
    const auto anyEnds = FillerRunEnds(word, AnyFillers());
    const auto anyStarts = FillerRunStarts(word, AnyFillers());
    const LookalikeKind misspeltKind = foreign ? LookalikeKind::Homograph : LookalikeKind::Misspelt;
    // The candidates are the same for every brand: the stretches between a
    // run of padding at the front and one at the back.
    struct Stretch { std::size_t i, j; bool phishing; std::string text, skeleton; };
    std::vector<Stretch> stretches;
    for (std::size_t i : anyEnds)
        for (std::size_t j : anyStarts) {
            if (j <= i) continue;
            const bool phishing =
                std::find(phishEnds.begin(), phishEnds.end(), i) != phishEnds.end() &&
                std::find(phishStarts.begin(), phishStarts.end(), j) != phishStarts.end();
            const std::string text = word.substr(i, j - i);
            stretches.push_back({ i, j, phishing, text, Skeleton(text) });
        }
    for (const LabelNames& entry : BrandLabelNames()) {
        for (std::size_t n = 0; n < entry.names.size(); ++n) {
            const std::string& name = entry.names[n];
            for (const Stretch& st : stretches) {
                const std::size_t len = st.j - st.i;
                if (len + 1 < name.size() || len > name.size() + 1) continue;
                const bool padded = st.i > 0 || st.j < word.size();
                if (st.text == name) {
                    // The real name: padded with phishing words - or, in
                    // foreign letters, with any word or none.
                    if (foreign || (padded && st.phishing))
                        return { entry.brand, foreign ? LookalikeKind::Homograph
                                                      : LookalikeKind::Name, name, "", "" };
                    continue;
                }
                // A misspelt name, padded with any word or none.
                const bool misspelt =
                    (name.size() >= 4 && st.skeleton == entry.skeletons[n]) ||
                    (name.size() >= 5 && DoubledLetter(st.text, name)) ||
                    (name.size() >= 8 && OneEditApart(st.text, name));
                if (misspelt) return { entry.brand, misspeltKind, st.text, "", "" };
            }
        }
    }
    return none;
}

} // namespace

std::string DomainToUnicode(const std::string& domain) {
    std::string out;
    std::size_t start = 0;
    while (start <= domain.size()) {
        std::size_t dot = domain.find('.', start);
        if (dot == std::string::npos) dot = domain.size();
        const std::string label = domain.substr(start, dot - start);
        std::u32string decoded;
        if (Lower(label).compare(0, 4, "xn--") == 0 && PunycodeDecode(label.substr(4), decoded)) {
            for (char32_t cp : decoded) AppendUtf8(out, cp);
        } else {
            out += label;
        }
        if (dot == domain.size()) break;
        out += '.';
        start = dot + 1;
    }
    return out;
}

DomainLookalike BrandImitatedByDomain(const std::string& domainIn) {
    DomainLookalike none;
    std::string d = Lower(domainIn);
    while (!d.empty() && d.back() == '.') d.pop_back();
    if (d.empty() || BrandForDomain(d) || IsPersonalMailboxDomain(d)) return none;

    // Foreign letters: what the domain reads as, in Latin ones. A letter with
    // no Latin look-alike ("東京", most of "москва") makes no imitation.
    const std::string unicode = DomainToUnicode(d);
    bool foreign = false;
    std::string skeleton;
    std::vector<std::string> scripts;
    for (char32_t cp : DecodeUtf8(unicode)) {
        if (cp < 0x80) { skeleton += static_cast<char>(std::tolower(static_cast<int>(cp))); continue; }
        const char* script = nullptr;
        const char latin = LatinLookalike(cp, &script);
        if (!latin) return none;
        foreign = true;
        skeleton += latin;
        if (std::find(scripts.begin(), scripts.end(), script) == scripts.end())
            scripts.push_back(script);
    }
    std::string letters;
    for (std::size_t k = 0; k < scripts.size(); ++k)
        letters += (k == 0 ? "" : k + 1 == scripts.size() ? " and " : ", ") + std::string(scripts[k]);
    auto homograph = [&](DomainLookalike found) {
        if (foreign) {
            found.letters = letters;
            found.unicode = unicode;
            if (found.kind != LookalikeKind::OwnDomain) found.kind = LookalikeKind::Homograph;
        }
        return found;
    };
    if (foreign) {
        // A brand's own domain in look-alike letters: "pаypal.com".
        if (const SenderBrand* brand = BrandForDomain(skeleton)) {
            const std::string reg = RegistrableDomain(unicode);
            return homograph({ brand, LookalikeKind::Homograph,
                               reg.substr(0, reg.find('.')), "", "" });
        }
        if (IsPersonalMailboxDomain(skeleton)) return none;
        d = skeleton;
    }

    const std::string reg = RegistrableDomain(d);
    const std::string front = d.size() > reg.size() ? d.substr(0, d.size() - reg.size() - 1)
                                                     : std::string();

    // A brand's own domain in front of a foreign one: paypal.com.account-check.ru.
    if (!front.empty()) {
        const std::string dotted = "." + front + ".";
        for (const auto& rule : SenderBrandRules())
            for (const auto& own : rule.domains)
                if (own.find('.') != std::string::npos &&
                    dotted.find("." + own + ".") != std::string::npos)
                    return homograph({ &rule.brand, LookalikeKind::OwnDomain, own, "", "" });
    }

    // Each label on its own, the registrable one first. A bare name is no
    // claim in Latin letters: "hermes.uni-example.de" is a server called Hermes.
    std::vector<std::string> labels = { BaseLabel(reg) };
    for (std::size_t start = 0; start < front.size();) {
        std::size_t dot = front.find('.', start);
        if (dot == std::string::npos) dot = front.size();
        labels.push_back(front.substr(start, dot - start));
        start = dot + 1;
    }
    for (const auto& label : labels) {
        DomainLookalike found = MatchLabel(Alnum(label), foreign);   // "paypal-secure" -> "paypalsecure"
        if (found.brand) return homograph(found);
    }
    return none;
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
