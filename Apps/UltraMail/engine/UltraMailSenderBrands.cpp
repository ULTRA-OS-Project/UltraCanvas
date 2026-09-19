// Apps/UltraMail/engine/UltraMailSenderBrands.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailSenderBrands.h"

#include <algorithm>
#include <cctype>
#include <set>

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
    };
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

// The registry. `domains` are exact registrable domains; `labels` match a
// registrable domain whose base label is that word under any public suffix
// (amazon.de, amazon.co.uk); `keywords` are the words that claim the brand in
// a display name, a subject or a host label.
struct BrandRule {
    SenderBrand              brand;
    std::vector<std::string> domains;
    std::vector<std::string> labels;
    std::vector<std::string> keywords;
};

const std::vector<BrandRule>& Rules() {
    static const std::vector<BrandRule> rules = {
        { {"facebook",  "Facebook",  "https://www.facebook.com/favicon.ico",  0x1877F2, BrandCategory::Social},
          {"facebook.com", "facebookmail.com", "fb.com", "meta.com"}, {},
          {"facebook", "meta"} },
        { {"instagram", "Instagram", "https://www.instagram.com/favicon.ico", 0xE1306C, BrandCategory::Social},
          {"instagram.com", "mail.instagram.com"}, {}, {"instagram"} },
        { {"whatsapp",  "WhatsApp",  "https://www.whatsapp.com/favicon.ico",  0x25D366, BrandCategory::Messaging},
          {"whatsapp.com"}, {}, {"whatsapp"} },
        { {"linkedin",  "LinkedIn",  "https://www.linkedin.com/favicon.ico",  0x0A66C2, BrandCategory::Social},
          {"linkedin.com"}, {}, {"linkedin"} },
        { {"x",         "X",         "https://abs.twimg.com/favicons/twitter.3.ico", 0x111111, BrandCategory::Social},
          {"twitter.com", "x.com"}, {}, {"twitter"} },
        { {"claude",    "Claude",    "https://claude.ai/favicon.ico",         0xD97757, BrandCategory::Technology},
          {"anthropic.com", "claude.ai", "claude.com"}, {}, {"anthropic", "claude"} },
        { {"openai",    "OpenAI",    "https://openai.com/favicon.ico",        0x10A37F, BrandCategory::Technology},
          {"openai.com", "chatgpt.com"}, {}, {"openai", "chatgpt"} },
        { {"google",    "Google",    "https://www.google.com/favicon.ico",    0x4285F4, BrandCategory::Technology},
          {"googleapis.com", "googleusercontent.com", "google-analytics.com",
           "withgoogle.com", "firebase.com", "android.com"},
          {"google"}, {"google"} },
        { {"youtube",   "YouTube",   "https://www.youtube.com/favicon.ico",   0xFF0000, BrandCategory::Media},
          {"youtube.com", "youtu.be"}, {}, {"youtube"} },
        { {"apple",     "Apple",     "https://www.apple.com/favicon.ico",     0x555555, BrandCategory::Technology},
          {"apple.com", "itunes.com", "apple.news"}, {},
          {"apple", "itunes", "appleid"} },
        { {"microsoft", "Microsoft", "https://www.microsoft.com/favicon.ico", 0x0078D4, BrandCategory::Technology},
          {"microsoft.com", "microsoftonline.com", "office.com", "office365.com",
           "sharepointonline.com", "azure.com", "windows.com", "skype.com",
           "xbox.com", "bing.com"}, {},
          {"microsoft", "onedrive", "sharepoint", "outlook", "office"} },
        { {"github",    "GitHub",    "https://github.com/favicon.ico",        0x24292F, BrandCategory::Technology},
          {"github.com"}, {}, {"github"} },
        { {"amazon",    "Amazon",    "https://www.amazon.com/favicon.ico",    0xFF9900, BrandCategory::Shopping},
          {"amazon.com", "primevideo.com", "audible.com", "aws.amazon.com"},
          {"amazon"}, {"amazon", "prime"} },
        { {"paypal",    "PayPal",    "https://www.paypal.com/favicon.ico",    0x003087, BrandCategory::Payment},
          {"paypal.com", "paypal-communication.com"}, {}, {"paypal"} },
        { {"stripe",    "Stripe",    "https://stripe.com/favicon.ico",        0x635BFF, BrandCategory::Payment},
          {"stripe.com"}, {}, {"stripe"} },
        { {"ebay",      "eBay",      "https://www.ebay.com/favicon.ico",      0xE53238, BrandCategory::Shopping},
          {}, {"ebay"}, {"ebay"} },
        { {"netflix",   "Netflix",   "https://www.netflix.com/favicon.ico",   0xE50914, BrandCategory::Media},
          {"netflix.com"}, {}, {"netflix"} },
        { {"spotify",   "Spotify",   "https://www.spotify.com/favicon.ico",   0x1DB954, BrandCategory::Media},
          {"spotify.com", "spotifymail.com"}, {}, {"spotify"} },
        { {"dropbox",   "Dropbox",   "https://www.dropbox.com/favicon.ico",   0x0061FF, BrandCategory::Technology},
          {"dropbox.com", "dropboxmail.com"}, {}, {"dropbox"} },
        { {"slack",     "Slack",     "https://slack.com/favicon.ico",         0x4A154B, BrandCategory::Messaging},
          {"slack.com", "slack-mail.com"}, {}, {"slack"} },
        { {"discord",   "Discord",   "https://discord.com/assets/favicon.ico",0x5865F2, BrandCategory::Messaging},
          {"discord.com", "discordapp.com"}, {}, {"discord"} },
        { {"telegram",  "Telegram",  "https://telegram.org/favicon.ico",      0x26A5E4, BrandCategory::Messaging},
          {"telegram.org"}, {}, {"telegram"} },
        { {"reddit",    "Reddit",    "https://www.reddit.com/favicon.ico",    0xFF4500, BrandCategory::Social},
          {"reddit.com", "redditmail.com"}, {}, {"reddit"} },
        { {"tiktok",    "TikTok",    "https://www.tiktok.com/favicon.ico",    0x111111, BrandCategory::Social},
          {"tiktok.com"}, {}, {"tiktok"} },
        { {"zoom",      "Zoom",      "https://zoom.us/favicon.ico",           0x2D8CFF, BrandCategory::Technology},
          {"zoom.us"}, {}, {"zoom"} },
        { {"booking",   "Booking",   "https://www.booking.com/favicon.ico",   0x003580, BrandCategory::Travel},
          {"booking.com"}, {}, {"booking"} },
        { {"airbnb",    "Airbnb",    "https://www.airbnb.com/favicon.ico",    0xFF5A5F, BrandCategory::Travel},
          {"airbnb.com"}, {}, {"airbnb"} },
        { {"dhl",       "DHL",       "https://www.dhl.com/favicon.ico",       0xD40511, BrandCategory::Delivery},
          {}, {"dhl"}, {"dhl"} },
        { {"ups",       "UPS",       "https://www.ups.com/favicon.ico",       0x351C15, BrandCategory::Delivery},
          {"ups.com"}, {}, {"ups"} },
        { {"fedex",     "FedEx",     "https://www.fedex.com/favicon.ico",     0x4D148C, BrandCategory::Delivery},
          {"fedex.com"}, {}, {"fedex"} },

        // ── Crowdfunding and creator support ────────────────────────────────
        // A backed project or a supported creator is a business relationship
        // the user keeps: these are among the entries most worth collecting
        // into the address book (see ContactCollector::CollectSender).
        { {"kickstarter", "Kickstarter", "https://www.kickstarter.com/favicon.ico", 0x05CE78,
           BrandCategory::Crowdfunding},
          {"kickstarter.com"}, {}, {"kickstarter"} },
        { {"indiegogo", "Indiegogo", "https://www.indiegogo.com/favicon.ico", 0xEB1478,
           BrandCategory::Crowdfunding},
          {"indiegogo.com"}, {}, {"indiegogo"} },
        { {"gofundme", "GoFundMe", "https://www.gofundme.com/favicon.ico", 0x02A95C,
           BrandCategory::Crowdfunding},
          {"gofundme.com"}, {}, {"gofundme"} },
        { {"startnext", "Startnext", "https://www.startnext.com/favicon.ico", 0x27ADE3,
           BrandCategory::Crowdfunding},
          {"startnext.com", "startnext.de"}, {}, {"startnext"} },
        { {"crowdsupply", "Crowd Supply", "https://www.crowdsupply.com/favicon.ico", 0x1B4E6B,
           BrandCategory::Crowdfunding},
          {"crowdsupply.com"}, {}, {"crowd supply", "crowdsupply"} },
        { {"patreon", "Patreon", "https://www.patreon.com/favicon.ico", 0xFF424D,
           BrandCategory::CreatorSupport},
          {"patreon.com"}, {}, {"patreon"} },
        { {"buymeacoffee", "Buy Me a Coffee", "https://buymeacoffee.com/favicon.ico", 0xFFDD00,
           BrandCategory::CreatorSupport},
          {"buymeacoffee.com"}, {}, {"buy me a coffee", "buymeacoffee"} },
        { {"kofi", "Ko-fi", "https://ko-fi.com/favicon.ico", 0xFF5E5B,
           BrandCategory::CreatorSupport},
          {"ko-fi.com"}, {}, {"ko-fi", "kofi"} },
        { {"liberapay", "Liberapay", "https://liberapay.com/favicon.ico", 0xF6C915,
           BrandCategory::CreatorSupport},
          {"liberapay.com"}, {}, {"liberapay"} },
        { {"opencollective", "Open Collective", "https://opencollective.com/favicon.ico", 0x3385FF,
           BrandCategory::CreatorSupport},
          {"opencollective.com"}, {}, {"open collective", "opencollective"} },
        { {"gumroad", "Gumroad", "https://gumroad.com/favicon.ico", 0xFF90E8,
           BrandCategory::CreatorSupport},
          {"gumroad.com"}, {}, {"gumroad"} },
        { {"substack", "Substack", "https://substack.com/favicon.ico", 0xFF6719,
           BrandCategory::CreatorSupport},
          {"substack.com"}, {}, {"substack"} },

        // ── More social networks ────────────────────────────────────────────
        { {"pinterest", "Pinterest", "https://www.pinterest.com/favicon.ico", 0xE60023,
           BrandCategory::Social},
          {"pinterest.com", "pinterestmail.com"}, {"pinterest"}, {"pinterest"} },
        { {"tumblr", "Tumblr", "https://www.tumblr.com/favicon.ico", 0x36465D,
           BrandCategory::Social},
          {"tumblr.com", "tumblr.net"}, {}, {"tumblr"} },
        { {"mastodon", "Mastodon", "https://joinmastodon.org/favicon.ico", 0x6364FF,
           BrandCategory::Social},
          {"joinmastodon.org", "mastodon.social"}, {}, {"mastodon"} },
        { {"twitch", "Twitch", "https://www.twitch.tv/favicon.ico", 0x9146FF,
           BrandCategory::Media},
          {"twitch.tv"}, {}, {"twitch"} },
        { {"vimeo", "Vimeo", "https://vimeo.com/favicon.ico", 0x1AB7EA,
           BrandCategory::Media},
          {"vimeo.com"}, {}, {"vimeo"} },
        { {"etsy", "Etsy", "https://www.etsy.com/favicon.ico", 0xF1641E,
           BrandCategory::Shopping},
          {}, {"etsy"}, {"etsy"} },
    };
    return rules;
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
    const std::size_t take = TwoLevelSuffixes().count(lastTwo) ? 3u : 2u;
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
    const std::string label = BaseLabel(reg);
    for (const auto& rule : Rules()) {
        for (const auto& d : rule.domains)
            if (reg == d || RegistrableDomain(d) == reg) return &rule.brand;
        for (const auto& l : rule.labels)
            if (label == l) return &rule.brand;
    }
    return nullptr;
}

const SenderBrand* BrandForAddress(const std::string& address) {
    return BrandForDomain(DomainOfAddress(address));
}

const SenderBrand* BrandById(const std::string& id) {
    for (const auto& rule : Rules())
        if (rule.brand.id == id) return &rule.brand;
    return nullptr;
}

bool DomainBelongsToBrand(const std::string& domain, const SenderBrand& brand) {
    const SenderBrand* found = BrandForDomain(domain);
    return found && found->id == brand.id;
}

const SenderBrand* BrandNamedIn(const std::string& text) {
    if (text.empty()) return nullptr;
    const std::string lower = Lower(text);
    for (const auto& rule : Rules()) {
        // Names shorter than three letters ("X") would match half the prose in
        // an inbox, so such a brand is claimed through its keywords only.
        if (rule.brand.name.size() >= 3 && ContainsWord(lower, Lower(rule.brand.name)))
            return &rule.brand;
        for (const auto& k : rule.keywords)
            if (ContainsWord(lower, k)) return &rule.brand;
    }
    return nullptr;
}

const std::vector<SenderBrand>& KnownBrands() {
    static const std::vector<SenderBrand> all = [] {
        std::vector<SenderBrand> v;
        for (const auto& rule : Rules()) v.push_back(rule.brand);
        return v;
    }();
    return all;
}

} // namespace UltraMail
