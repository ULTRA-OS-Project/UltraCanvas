// Apps/UltraMail/engine/UltraMailThreatScan.cpp
// Version: 0.5.1 - link texts, link targets and the body read through the HTMLReader
//                  module (HTML::ExtractPlainText, HTML::DecodeEntities): every
//                  entity, no <style>/<script> text, a word split by <b> whole
// Version: 0.5.0 - mail authentication (ParseAuthenticationResults, VerifiedSenderDomain,
//                  TopHeaderValue): proven senders are not flagged for tracking
//                  links, help-desk reply addresses or many link domains
// Version: 0.4.0 - plain text: mailto: and bare mail addresses are links too
// Version: 0.3.0 - PlainLinkAt: the bare URL at a position of plain text
// Version: 0.2.0 - borrowed-brand-pictures rule (a brand's own pictures over links
//                elsewhere); ExtractImageHosts
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailThreatScan.h"

#include "UltraMailSenderBrands.h"

#include "HTMLReader/HTMLDocument.h"   // HTML::ExtractPlainText, HTML::DecodeEntities

#include <UltraNet/UltraNetMime.h>

#include <algorithm>
#include <cctype>
#include <map>
#include <regex>
#include <cstring>
#include <set>

namespace UltraMail {

namespace {

// Score thresholds. A rule's weight is its confidence, not its drama: only the
// rules that catch a link actively lying about its destination can reach Scam
// on their own.
constexpr int kScamScore       = 45;
constexpr int kSuspiciousScore = 22;

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string Trim(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

bool Contains(const std::string& haystackLower, const std::string& needleLower) {
    return haystackLower.find(needleLower) != std::string::npos;
}

// The authority of a URL, split into userinfo and host (port dropped).
void SplitAuthority(const std::string& url, std::string& outUserinfo, std::string& outHost) {
    outUserinfo.clear();
    outHost.clear();
    std::string rest;
    const std::size_t scheme = url.find("://");
    if (scheme != std::string::npos) {
        rest = url.substr(scheme + 3);
    } else if (Lower(url).compare(0, 4, "www.") == 0) {
        rest = url;
    } else {
        return;   // mailto:, tel:, #anchor, a relative path — no host
    }
    const std::size_t end = rest.find_first_of("/?#");
    std::string authority = (end == std::string::npos) ? rest : rest.substr(0, end);
    const std::size_t at = authority.rfind('@');
    if (at != std::string::npos) {
        outUserinfo = authority.substr(0, at);
        authority = authority.substr(at + 1);
    }
    if (!authority.empty() && authority.front() == '[') {          // IPv6 literal
        const std::size_t close = authority.find(']');
        outHost = Lower(authority.substr(0, close == std::string::npos
                                             ? std::string::npos : close + 1));
        return;
    }
    const std::size_t colon = authority.find(':');
    outHost = Lower(colon == std::string::npos ? authority : authority.substr(0, colon));
}

std::string HostOf(const std::string& url) {
    std::string userinfo, host;
    SplitAuthority(url, userinfo, host);
    return host;
}

bool IsIpLiteral(const std::string& host) {
    if (host.size() >= 2 && host.front() == '[' && host.back() == ']') return true;  // IPv6
    int dots = 0;
    for (char c : host) {
        if (c == '.') { ++dots; continue; }
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    return dots == 3 && !host.empty();
}

bool HasPunycodeLabel(const std::string& host) {
    std::size_t start = 0;
    while (start < host.size()) {
        const std::size_t dot = host.find('.', start);
        const std::string label = host.substr(start, dot == std::string::npos
                                                      ? std::string::npos : dot - start);
        if (label.compare(0, 4, "xn--") == 0) return true;
        if (dot == std::string::npos) break;
        start = dot + 1;
    }
    return false;
}

bool HasNonAscii(const std::string& s) {
    for (unsigned char c : s) if (c > 0x7F) return true;
    return false;
}

const std::set<std::string>& Shorteners() {
    static const std::set<std::string> s = {
        "bit.ly", "tinyurl.com", "t.co", "goo.gl", "ow.ly", "is.gd", "buff.ly",
        "cutt.ly", "rb.gy", "shorturl.at", "tiny.cc", "rebrand.ly", "bl.ink",
        "t.ly", "s.id", "short.io", "lnkd.in", "qr.ae",
    };
    return s;
}

// Extensions that execute (or that Windows hides the second half of).
const std::set<std::string>& RiskyExtensions() {
    static const std::set<std::string> s = {
        "exe", "scr", "pif", "com", "bat", "cmd", "msi", "jar", "js", "jse",
        "vbs", "vbe", "wsf", "wsh", "ps1", "hta", "lnk", "reg", "apk", "iso",
        "img", "cpl", "dll", "chm", "scf",
    };
    return s;
}

std::string ExtensionOf(const std::string& filename) {
    const std::size_t dot = filename.rfind('.');
    return dot == std::string::npos ? std::string() : Lower(filename.substr(dot + 1));
}

// Anchor text that is itself an address the reader will read as the target:
// "www.paypal.com", "https://paypal.com/login", "paypal.com". Returns the host
// it claims, or "".
std::string ClaimedHostIn(const std::string& text) {
    const std::string t = Trim(Lower(UltraCanvas::HTML::ExtractPlainText(text)));
    if (t.empty() || t.find(' ') != std::string::npos) return "";
    std::string host = HostOf(t);
    if (!host.empty()) return host;
    // A bare "paypal.com" / "paypal.com/login" with no scheme.
    const std::size_t slash = t.find('/');
    const std::string candidate = slash == std::string::npos ? t : t.substr(0, slash);
    if (candidate.find('@') != std::string::npos) return "";        // an email address
    if (candidate.find('.') == std::string::npos) return "";
    const std::string tld = candidate.substr(candidate.rfind('.') + 1);
    if (tld.size() < 2) return "";
    for (char c : tld) if (!std::isalpha(static_cast<unsigned char>(c))) return "";
    for (char c : candidate)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '-') return "";
    return candidate;
}

const std::vector<std::string>& CredentialPhrases() {
    static const std::vector<std::string> v = {
        "verify your account", "verify your identity", "confirm your account",
        "confirm your password", "update your password", "update your payment",
        "update your billing", "your account will be suspended",
        "your account has been suspended", "account has been locked",
        "unusual sign-in", "unusual activity", "unauthorized login",
        "click here to unlock", "re-enter your password", "reenter your password",
        "validate your account", "confirm your identity", "security alert",
        "your payment could not", "payment failed", "billing problem",
        "konto wurde gesperrt", "bestätigen sie ihr konto", "ihr passwort",
        "verifizieren sie", "zahlung fehlgeschlagen",
    };
    return v;
}

// The advance-fee fraud ("Nigeria connection", 419 scam): a stranger offers
// millions from a dead relative's estate, a dormant account or a contract,
// and only needs the reader to pay the taxes or fees to release it. No single
// phrase gives it away, so the rule counts the story's ingredients.
const std::vector<std::string>& DeceasedPhrases() {
    static const std::vector<std::string> v = {
        "deceased", "late husband", "late wife", "late father", "late mother",
        "late client", "next of kin", "inheritance", "inherit", "beneficiary",
        "his estate", "her estate", "the estate of", "died in", "passed away",
        "plane crash", "car accident", "unclaimed", "dormant account",
        "no heir", "without a will", "verstorben", "erbschaft", "nachlass",
    };
    return v;
}

const std::vector<std::string>& FeePhrases() {
    static const std::vector<std::string> v = {
        "pay the tax", "pay the taxes", "taxes", "processing fee", "transfer fee",
        "clearance fee", "release fee", "administrative fee", "legal fee",
        "handling fee", "registration fee", "delivery fee", "stamp duty",
        "small fee", "upfront", "advance payment", "western union", "moneygram",
        "gift card", "bitcoin", "geb\xC3\xBChr", "steuern",
    };
    return v;
}

const std::vector<std::string>& AdvanceFeeStoryPhrases() {
    static const std::vector<std::string> v = {
        "nigeria", "lagos", "abuja", "ghana", "benin", "cote d'ivoire", "togo",
        "barrister", "attorney at law", "central bank", "united nations",
        "strictly confidential", "utmost confidentiality", "top secret",
        "my share", "your share", "percent of the total", "% of the total",
        "foreign partner", "trustworthy partner", "god bless", "dear friend",
        "dear beloved", "compensation fund", "lottery", "you have won",
        "consignment", "diplomat", "secure vault",
    };
    return v;
}

// A sum in the millions, as the scam writes it: "$12,500,000", "US$ 4.5
// million", "10.5 million united states dollars", "USD 7,000,000". Returns the
// phrase as found (for the reason), or "" when there is none.
std::string LargeMoneySum(const std::string& textLower) {
    static const std::regex digits(
        R"((?:us\s?\$|\$|usd|eur|euro|€|£|gbp)\s?\d{1,3}(?:[,.' ]\d{3}){2,}(?:\.\d+)?)"
        R"(|\d{1,3}(?:[,.' ]\d{3}){2,}(?:\.\d+)?\s?(?:us\s?dollars|usd|dollars|euros?|pounds|\$|€))");
    static const std::regex words(
        R"((?:(?:us\s?\$|\$|usd|eur|€|£)\s?)?\d+(?:[.,]\d+)?\s?(?:million|billion|m\b|mio)\s?)"
        R"((?:(?:united states |us |u\.s\. |american )?dollars|usd|euros?|pounds|\(?\s?(?:us\s?\$|usd)\)?)?)");
    std::smatch m;
    if (std::regex_search(textLower, m, digits)) return m.str();
    auto it = std::sregex_iterator(textLower.begin(), textLower.end(), words);
    for (; it != std::sregex_iterator(); ++it) {
        const std::string hit = it->str();
        // "2 million" alone is a newspaper headline; with a currency it is an offer.
        const bool currency = Contains(hit, "$") || Contains(hit, "usd") ||
            Contains(hit, "dollar") || Contains(hit, "euro") || Contains(hit, "eur") ||
            Contains(hit, "pound") || Contains(hit, "\xE2\x82\xAC") ||
            Contains(hit, "\xC2\xA3");
        if (currency) return Trim(hit);
    }
    return std::string();
}

const std::string* FirstPhraseIn(const std::string& textLower,
                                 const std::vector<std::string>& phrases) {
    for (const auto& p : phrases) if (Contains(textLower, p)) return &p;
    return nullptr;
}

void Add(ThreatReport& r, int score, const char* code, const std::string& detail) {
    for (const auto& f : r.findings) if (f.code == code) return;   // one of each
    r.findings.push_back({ code, detail });
    r.score += score;
}

// Case-insensitive header lookup over a parsed part's header map.
std::string Header(const std::map<std::string, std::string>& headers,
                   const std::string& name) {
    const std::string want = Lower(name);
    for (const auto& h : headers) if (Lower(h.first) == want) return h.second;
    return "";
}

} // namespace

// ---------------------------------------------------------------------------
// Level names (stored in the database, so they must stay stable)
// ---------------------------------------------------------------------------
std::string ToString(ThreatLevel level) {
    switch (level) {
        case ThreatLevel::Unscanned:     return "unscanned";
        case ThreatLevel::Clean:         return "clean";
        case ThreatLevel::Advertisement: return "advertisement";
        case ThreatLevel::Suspicious:    return "suspicious";
        case ThreatLevel::Scam:          return "scam";
    }
    return "unscanned";
}

ThreatLevel ThreatLevelFromString(const std::string& s) {
    const std::string v = Lower(s);
    if (v == "clean")         return ThreatLevel::Clean;
    if (v == "advertisement") return ThreatLevel::Advertisement;
    if (v == "suspicious")    return ThreatLevel::Suspicious;
    if (v == "scam")          return ThreatLevel::Scam;
    return ThreatLevel::Unscanned;
}

std::string ThreatReport::Summary() const {
    std::string out;
    for (const auto& f : findings) {
        if (!out.empty()) out.push_back('\n');
        out += "\xE2\x80\xA2 ";   // "• "
        out += f.detail;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Link extraction
// ---------------------------------------------------------------------------
std::vector<std::string> ExtractImageHosts(const std::string& body) {
    std::vector<std::string> hosts;
    const std::string lower = Lower(body);
    // src="…" / background="…" / url(…) with an http(s) source.
    for (const char* key : { "src=", "background=", "url(" }) {
        std::size_t pos = 0;
        while ((pos = lower.find(key, pos)) != std::string::npos) {
            std::size_t v = pos + std::strlen(key);
            while (v < lower.size() && (std::isspace(static_cast<unsigned char>(lower[v])) ||
                                        lower[v] == '"' || lower[v] == '\'')) ++v;
            if (lower.compare(v, 7, "http://") == 0 || lower.compare(v, 8, "https://") == 0) {
                std::size_t end = v;
                while (end < lower.size() && !std::isspace(static_cast<unsigned char>(lower[end])) &&
                       lower[end] != '"' && lower[end] != '\'' && lower[end] != ')' && lower[end] != '>')
                    ++end;
                const std::string host = HostOf(body.substr(v, end - v));
                if (!host.empty() && std::find(hosts.begin(), hosts.end(), host) == hosts.end())
                    hosts.push_back(host);
            }
            pos = v;
        }
    }
    return hosts;
}

// A bare mail address around the '@' at `at`: [start, end), or false when
// the text there is not one ("name@example.com" - a local part, and a domain
// with a dot and a top-level part of two letters or more).
static bool MailAddressAt(const std::string& s, std::size_t at, std::size_t& start,
                          std::size_t& end) {
    auto localChar = [](unsigned char c) {
        return std::isalnum(c) || c == '.' || c == '_' || c == '%' || c == '+' || c == '-';
    };
    auto domainChar = [](unsigned char c) { return std::isalnum(c) || c == '.' || c == '-'; };
    start = at;
    while (start > 0 && localChar(static_cast<unsigned char>(s[start - 1]))) --start;
    while (start < at && s[start] == '.') ++start;   // "...name" in running text
    end = at + 1;
    while (end < s.size() && domainChar(static_cast<unsigned char>(s[end]))) ++end;
    while (end > at + 1 && (s[end - 1] == '.' || s[end - 1] == '-')) --end;
    if (start == at || end == at + 1 || s[at + 1] == '.') return false;
    const std::string domain = s.substr(at + 1, end - at - 1);
    const std::size_t dot = domain.rfind('.');
    if (dot == std::string::npos || domain.size() - dot - 1 < 2) return false;
    for (std::size_t i = dot + 1; i < domain.size(); ++i)
        if (!std::isalpha(static_cast<unsigned char>(domain[i]))) return false;
    return true;
}

// The next link in plain text from byte `from`: a web address (http://,
// https://, www.), a mailto: address or a bare mail address, at [start, end).
// `href` is what it opens - "mailto:name@example.com" for a bare address -
// or empty when the text there turned out not to be a link. False when there
// is nothing more.
static bool NextPlainLink(const std::string& s, std::size_t from, std::size_t& start,
                          std::size_t& end, std::string& href) {
    start = std::string::npos;
    for (const char* proto : { "http://", "https://", "www.", "mailto:" }) {
        const std::size_t p = s.find(proto, from);
        if (p != std::string::npos && (start == std::string::npos || p < start))
            start = p;
    }
    // A bare address that starts before the first such link wins.
    for (std::size_t at = s.find('@', from); at != std::string::npos &&
             (start == std::string::npos || at < start);
         at = s.find('@', at + 1)) {
        std::size_t a = 0, b = 0;
        if (MailAddressAt(s, at, a, b) && a >= from && (start == std::string::npos || a < start)) {
            start = a;
            end = b;
            href = "mailto:" + s.substr(a, b - a);
            return true;
        }
    }
    if (start == std::string::npos) return false;
    end = start;
    while (end < s.size() && !std::isspace(static_cast<unsigned char>(s[end])) &&
           s[end] != '<' && s[end] != '>' && s[end] != '"' && s[end] != '\'')
        ++end;
    // Trailing sentence punctuation is not part of the link.
    while (end > start && std::string(".,;:!?)]").find(s[end - 1]) != std::string::npos)
        --end;
    href = s.substr(start, end - start);
    const bool mailto = href.compare(0, 7, "mailto:") == 0;
    if (mailto ? href.find('@') == std::string::npos : HostOf(href).empty()) href.clear();
    return true;
}

std::string PlainLinkAt(const std::string& text, std::size_t offset) {
    std::size_t i = 0, start = 0, end = 0;
    std::string href;
    while (i < text.size() && NextPlainLink(text, i, start, end, href)) {
        if (start > offset) break;
        if (offset < end) return href;
        i = end > start ? end : start + 1;
    }
    return std::string();
}

std::vector<MessageLink> ExtractLinks(const std::string& body, bool isHtml) {
    std::vector<MessageLink> links;
    if (body.empty()) return links;

    if (!isHtml) {
        std::size_t i = 0, start = 0, end = 0;
        std::string href;
        while (i < body.size() && NextPlainLink(body, i, start, end, href)) {
            if (!href.empty()) {
                MessageLink link;
                link.href = href;
                link.host = HostOf(href);   // "" for a mail address, as for HTML's mailto:
                links.push_back(link);
            }
            i = end > start ? end : start + 1;
        }
        return links;
    }

    const std::string lower = Lower(body);
    std::size_t pos = 0;
    while (pos < lower.size()) {
        // The tags that navigate: <a href>, <area href>, <form action>.
        std::size_t tag = std::string::npos;
        std::string attribute;
        struct Candidate { const char* open; const char* attr; };
        for (const Candidate& c : { Candidate{"<a ", "href"}, Candidate{"<a\n", "href"},
                                    Candidate{"<area", "href"}, Candidate{"<form", "action"} }) {
            const std::size_t p = lower.find(c.open, pos);
            if (p != std::string::npos && (tag == std::string::npos || p < tag)) {
                tag = p;
                attribute = c.attr;
            }
        }
        if (tag == std::string::npos) break;
        const std::size_t tagEnd = lower.find('>', tag);
        if (tagEnd == std::string::npos) break;
        const std::string tagText = body.substr(tag, tagEnd - tag);
        const std::string tagLower = lower.substr(tag, tagEnd - tag);

        std::string href;
        const std::size_t attrPos = tagLower.find(attribute + "=");
        if (attrPos != std::string::npos) {
            std::size_t v = attrPos + attribute.size() + 1;
            while (v < tagText.size() && std::isspace(static_cast<unsigned char>(tagText[v]))) ++v;
            if (v < tagText.size() && (tagText[v] == '"' || tagText[v] == '\'')) {
                const char quote = tagText[v++];
                const std::size_t close = tagText.find(quote, v);
                href = tagText.substr(v, close == std::string::npos ? std::string::npos : close - v);
            } else {
                std::size_t e = v;
                while (e < tagText.size() && !std::isspace(static_cast<unsigned char>(tagText[e])))
                    ++e;
                href = tagText.substr(v, e - v);
            }
        }
        href = Trim(UltraCanvas::HTML::DecodeEntities(href));

        std::string text;
        if (attribute == "href") {
            const std::size_t close = lower.find("</a", tagEnd);
            if (close != std::string::npos && close > tagEnd)
                text = UltraCanvas::HTML::ExtractPlainText(body.substr(tagEnd + 1, close - tagEnd - 1));
        }
        if (!href.empty()) {
            MessageLink link;
            link.href = href;
            link.text = Trim(text);
            link.host = HostOf(href);
            links.push_back(link);
        }
        pos = tagEnd + 1;
    }
    return links;
}

// ---------------------------------------------------------------------------
// The scan
// ---------------------------------------------------------------------------
ThreatReport ScanMessage(const ScanInput& input) {
    ThreatReport report;
    report.level = ThreatLevel::Clean;

    const std::string senderDomain = DomainOfAddress(
        input.fromAddr.empty() ? input.fromName : input.fromAddr);
    const std::string senderReg    = RegistrableDomain(senderDomain);
    const SenderBrand* senderBrand = BrandForDomain(senderDomain);

    const std::string bodyLower = Lower(input.bodyIsHtml ? UltraCanvas::HTML::ExtractPlainText(input.body)
                                                         : input.body);
    const auto links = ExtractLinks(input.body, input.bodyIsHtml);

    // ---- Bulk / marketing markers -----------------------------------------
    const std::string precedence = Lower(input.precedence);
    const std::string autoSubmitted = Lower(Trim(input.autoSubmitted));
    report.bulk = !input.listUnsubscribe.empty() || precedence == "bulk" ||
                  precedence == "list" || precedence == "junk" ||
                  (!autoSubmitted.empty() && autoSubmitted != "no");

    // ---- Server-side spam verdicts ----------------------------------------
    if (Lower(Trim(input.spamFlag)) == "yes" ||
        Lower(input.spamStatus).compare(0, 4, "yes,") == 0 ||
        Lower(Trim(input.spamStatus)) == "yes") {
        Add(report, 40, "spam-flag",
            "The receiving server already marked this message as spam.");
    }

    // ---- Authentication results -------------------------------------------
    // The receiving server's verdict on the From domain. A proven domain is
    // the sender's own - not a promise that the mail is harmless (a fraudster
    // can sign for a domain of their own), but it does mean the things a
    // genuine sender's mail service does are not signs of forgery: links
    // through its click tracker, a reply address at its help desk, links to
    // many sites.
    const AuthResults auth = ParseAuthenticationResults(input.authResults);
    report.verifiedDomain = VerifiedSenderDomain(auth, senderDomain, &report.verifiedBy);
    const bool authenticated = !report.verifiedDomain.empty();
    // A registry brand's own domain, proven: the mail really is the brand's.
    const bool verifiedBrand = authenticated && senderBrand != nullptr;
    bool dkimFailed = false;
    for (const auto& sig : auth.dkim) dkimFailed = dkimFailed || sig.first == "fail";
    // A failure counts when DMARC says so, or - with no DMARC result - when
    // SPF or a signature failed and nothing passed: forwarded mail fails SPF
    // and a second, foreign signature may fail while the sender's own passes.
    if (auth.dmarc == "fail" ||
        (auth.dmarc.empty() && !authenticated && (auth.spf == "fail" || dkimFailed))) {
        Add(report, 30, "auth-failure",
            "The sending domain failed its own SPF/DKIM/DMARC checks, so the "
            "From address may be forged.");
    } else if (authenticated) {
        report.score -= 10;   // the From address is at least genuinely theirs
    }

    // ---- The sender claims a brand its address does not belong to ---------
    const SenderBrand* claimed = BrandNamedIn(input.fromName);
    if (!claimed) claimed = BrandNamedIn(input.subject);
    if (claimed && !DomainBelongsToBrand(senderDomain, *claimed)) {
        const bool fromMailbox = IsPersonalMailboxDomain(senderDomain);
        Add(report, fromMailbox ? 55 : 40, "brand-impersonation",
            "The message presents itself as " + claimed->name + ", but it was sent from " +
            (senderDomain.empty() ? std::string("an address with no domain")
                                  : senderDomain) + ", which is not " + claimed->name + ".");
    }

    // ---- Pictures borrowed from a brand the mail is not from --------------
    // The message's pictures come from a site its display name (or subject)
    // names - gotinder.com for "Tinder" - but it was sent from elsewhere and
    // none of its links go to that site: the look of a well-known service
    // dressed over links to somewhere else. Needs no brand table.
    if (input.bodyIsHtml) {
        std::vector<std::string> nameWords;
        {
            const std::string names = Lower(input.fromName + " " + input.subject);
            std::string word;
            for (std::size_t i = 0; i <= names.size(); ++i) {
                const char c = i < names.size() ? names[i] : ' ';
                if (std::isalnum(static_cast<unsigned char>(c))) word += c;
                else {
                    if (word.size() >= 4) nameWords.push_back(word);
                    word.clear();
                }
            }
        }
        std::set<std::string> linkRegs;
        for (const auto& link : links)
            if (!link.host.empty()) linkRegs.insert(RegistrableDomain(link.host));
        const std::string senderLower = Lower(senderReg);
        for (const std::string& imageHost : ExtractImageHosts(input.body)) {
            const std::string imageReg = RegistrableDomain(imageHost);
            if (imageReg.empty() || imageReg == senderReg || linkRegs.count(imageReg)) continue;
            const std::string label = imageReg.substr(0, imageReg.find('.'));
            std::string named;
            for (const std::string& w : nameWords)
                if (label.find(w) != std::string::npos && senderLower.find(w) == std::string::npos) {
                    named = w;
                    break;
                }
            if (named.empty()) continue;
            const std::string where = linkRegs.empty() ? std::string("nowhere on that site")
                                                       : *linkRegs.begin();
            Add(report, 30, "borrowed-brand-pictures",
                "The message shows pictures from " + imageReg + " (the \"" + named +
                "\" it names) but was sent from " +
                (senderDomain.empty() ? std::string("an unknown address") : senderDomain) +
                ", and its links go to " + where + ", not to " + imageReg + ".");
            break;
        }
    }

    // ---- Link rules --------------------------------------------------------
    std::set<std::string> foreignDomains;
    for (const auto& link : links) {
        if (link.host.empty()) continue;
        const std::string linkReg = RegistrableDomain(link.host);
        const bool ownDomain = !senderReg.empty() && linkReg == senderReg;
        if (!ownDomain) foreignDomains.insert(linkReg);

        // A target hidden behind userinfo: http://paypal.com@203.0.113.9/…
        std::string userinfo, host;
        SplitAuthority(link.href, userinfo, host);
        if (!userinfo.empty() && userinfo.find('.') != std::string::npos) {
            Add(report, 45, "link-userinfo",
                "A link is written as \"" + userinfo + "@" + host +
                "\": everything before the @ is ignored by the browser, which goes to " +
                host + ".");
        }

        if (IsIpLiteral(link.host)) {
            Add(report, 35, "link-ip-host",
                "A link points straight at the numeric address " + link.host +
                " instead of a named site.");
        }
        if (HasPunycodeLabel(link.host)) {
            Add(report, 25, "link-punycode",
                "A link uses a punycode host (" + link.host +
                "), which can be drawn to look like a familiar name.");
        }
        if (HasNonAscii(link.host)) {
            Add(report, 25, "link-nonascii-host",
                "A link's host contains non-Latin characters that can imitate ordinary letters.");
        }
        if (Shorteners().count(linkReg)) {
            Add(report, 12, "link-shortener",
                "A link is hidden behind the shortener " + linkReg +
                ", so its real destination cannot be seen.");
        }

        // The anchor text names one address and the link goes to another -
        // unless the address named is the proven sender's own, and the link
        // goes through its mail service's click tracker.
        const std::string claimedHost = ClaimedHostIn(link.text);
        if (!claimedHost.empty()) {
            const std::string claimedReg = RegistrableDomain(claimedHost);
            if (!claimedReg.empty() && claimedReg != linkReg && linkReg != senderReg &&
                !(authenticated && claimedReg == senderReg)) {
                Add(report, 50, "link-target-mismatch",
                    "A link reads \"" + claimedHost + "\" but actually goes to " +
                    link.host + ".");
            }
        }

        // A button or link that names a brand and goes somewhere else entirely
        // (the brand itself, proven, links where it likes).
        if (!link.text.empty() && !verifiedBrand) {
            const SenderBrand* linkBrand = BrandNamedIn(link.text);
            if (linkBrand && !DomainBelongsToBrand(link.host, *linkBrand) &&
                !(senderBrand && senderBrand->id == linkBrand->id)) {
                Add(report, 20, "link-brand-mismatch",
                    "A link labelled \"" + Trim(link.text) + "\" does not go to " +
                    linkBrand->name + " but to " + link.host + ".");
            }
        }

        // A brand's name worn as a subdomain: apple-id.verify.example.ru.
        const std::string hostPrefix = link.host.size() > linkReg.size()
            ? link.host.substr(0, link.host.size() - linkReg.size() - 1) : std::string();
        if (!hostPrefix.empty()) {
            std::string spaced = hostPrefix;
            for (char& c : spaced) if (c == '-' || c == '.') c = ' ';
            const SenderBrand* worn = BrandNamedIn(spaced);
            if (worn && !DomainBelongsToBrand(link.host, *worn)) {
                Add(report, 40, "link-brand-lookalike",
                    "A link wears " + worn->name + "'s name in front of an unrelated domain (" +
                    link.host + ").");
            }
        }

        // A login form or sign-in link over plain HTTP.
        if (Lower(link.href).compare(0, 7, "http://") == 0) {
            const std::string hrefLower = Lower(link.href);
            for (const char* word : { "login", "signin", "sign-in", "account", "verify",
                                      "password", "secure", "update" }) {
                if (Contains(hrefLower, word)) {
                    Add(report, 20, "insecure-login-link",
                        "A sign-in link is unencrypted (http://), so anything typed on that "
                        "page travels in the clear.");
                    break;
                }
            }
        }
    }

    if (!authenticated && foreignDomains.size() >= 5) {
        Add(report, 8, "many-foreign-domains",
            "The message links to " + std::to_string(foreignDomains.size()) +
            " different domains, none of them the sender's.");
    }

    // ---- Language that asks for credentials, plus a link off-domain -------
    // Not from a proven registry brand: the bank itself asking to update
    // account details is the bank.
    if (!foreignDomains.empty() && !verifiedBrand) {
        for (const auto& phrase : CredentialPhrases()) {
            if (Contains(bodyLower, phrase)) {
                Add(report, 30, "credential-request",
                    "The message asks the reader to confirm account or payment details "
                    "(\"" + phrase + "\") and links away from the sender's own domain.");
                break;
            }
        }
    }

    // ---- Advance-fee fraud ("Nigeria connection" / 419) --------------------
    {
        const std::string story = Lower(input.subject) + "\n" + bodyLower;
        const std::string sum = LargeMoneySum(story);
        if (!sum.empty()) {
            const std::string* deceased = FirstPhraseIn(story, DeceasedPhrases());
            const std::string* fee      = FirstPhraseIn(story, FeePhrases());
            const std::string* setting  = FirstPhraseIn(story, AdvanceFeeStoryPhrases());
            const int parts = (deceased ? 1 : 0) + (fee ? 1 : 0) + (setting ? 1 : 0);
            if (parts >= 1) {
                std::string why = "The message promises a large sum of money (\"" + sum + "\")";
                std::vector<std::string> extras;
                if (deceased) extras.push_back("a dead relative, estate or inheritance (\"" + *deceased + "\")");
                if (fee)      extras.push_back("taxes or fees to be paid first (\"" + *fee + "\")");
                if (setting)  extras.push_back("\"" + *setting + "\"");
                for (std::size_t i = 0; i < extras.size(); ++i)
                    why += (i == 0 ? " and mentions " : (i + 1 == extras.size() ? " and " : ", ")) + extras[i];
                why += ". That is the pattern of an advance-fee (\"Nigeria connection\") scam: "
                       "the money never exists, the fees you pay are the theft.";
                // Money plus one ingredient is worth a second look; money plus
                // two (an estate *and* a fee, say) is the scam itself.
                Add(report, parts >= 2 ? 50 : 25, "advance-fee-fraud", why);
            }
        }
    }

    // ---- Reply-To pointing somewhere else ---------------------------------
    // A proven sender's replies may well go to its help desk's domain.
    if (!authenticated && !input.replyTo.empty() && !senderReg.empty()) {
        const std::string replyReg = RegistrableDomain(DomainOfAddress(input.replyTo));
        if (!replyReg.empty() && replyReg != senderReg) {
            Add(report, 15, "reply-to-mismatch",
                "Replies would not go back to " + senderReg + " but to " + replyReg + ".");
        }
    }

    // ---- Attachments that execute -----------------------------------------
    for (const auto& name : input.attachmentNames) {
        const std::string ext = ExtensionOf(name);
        const std::string stem = name.substr(0, name.size() - (ext.empty() ? 0 : ext.size() + 1));
        const std::string inner = ExtensionOf(stem);
        const bool looksLikeDocument =
            inner == "pdf" || inner == "doc" || inner == "docx" || inner == "xls" ||
            inner == "xlsx" || inner == "jpg" || inner == "png" || inner == "txt" ||
            inner == "zip" || inner == "rtf" || inner == "csv";

        if (RiskyExtensions().count(ext)) {
            // A program sent as a program is a reason to be careful; a program
            // dressed as an invoice is the attack itself, so only the disguise
            // is weighted as one. (A colleague really does send the odd .jar.)
            if (looksLikeDocument)
                Add(report, 55, "attachment-disguised-executable",
                    "The attachment \"" + name + "\" is a program wearing a document's "
                    "name: everything before the final \"." + ext + "\" is decoration.");
            else
                Add(report, 40, "attachment-executable",
                    "The attachment \"" + name + "\" is a program, not a document.");
            continue;
        }
        // "invoice.pdf.zip" is not executable by itself, but two extensions on
        // an attachment are still worth saying out loud.
        if (!inner.empty() && !ext.empty() && inner.size() <= 4 && looksLikeDocument) {
            Add(report, 25, "attachment-double-extension",
                "The attachment \"" + name + "\" carries two extensions, which is how a "
                "program is made to look like a document.");
        }
    }

    if (report.score < 0) report.score = 0;
    if (report.score >= kScamScore)            report.level = ThreatLevel::Scam;
    else if (report.score >= kSuspiciousScore) report.level = ThreatLevel::Suspicious;
    else if (report.bulk)                      report.level = ThreatLevel::Advertisement;
    else                                       report.level = ThreatLevel::Clean;
    return report;
}

namespace {

// The scan's view of a raw RFC 5322 message; false when it does not parse.
bool BuildScanInput(const std::string& rawMessage, ScanInput& in) {
    if (rawMessage.empty()) return false;
    UltraNetMimeMessage msg;
    if (!UltraNet_MimeParse(rawMessage, msg)) return false;

    in.subject         = msg.subject;
    in.fromName        = msg.from;
    in.fromAddr        = msg.from;
    in.replyTo         = Header(msg.root.headers, "Reply-To");
    in.listUnsubscribe = Header(msg.root.headers, "List-Unsubscribe");
    in.precedence      = Header(msg.root.headers, "Precedence");
    in.autoSubmitted   = Header(msg.root.headers, "Auto-Submitted");
    in.spamFlag        = Header(msg.root.headers, "X-Spam-Flag");
    in.spamStatus      = Header(msg.root.headers, "X-Spam-Status");
    // The topmost: the parsed headers keep the last of a repeated header,
    // and the bottom-most Authentication-Results may be anyone's.
    in.authResults     = TopHeaderValue(rawMessage, "Authentication-Results");

    std::string body;
    bool isHtml = false;
    if (UltraNet_MimeGetDisplayBody(msg, body, isHtml)) {
        in.body       = std::move(body);
        in.bodyIsHtml = isHtml;
    }

    std::vector<UltraNetMimeAttachmentView> atts;
    UltraNet_MimeCollectAttachments(msg, atts, /*includeInline=*/false);
    for (const auto& a : atts) in.attachmentNames.push_back(a.filename);
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// Mail authentication
// ---------------------------------------------------------------------------
std::string TopHeaderValue(const std::string& raw, const std::string& name) {
    const std::string want = Lower(name) + ":";
    std::size_t pos = 0;
    while (pos < raw.size()) {
        std::size_t end = raw.find('\n', pos);
        if (end == std::string::npos) end = raw.size();
        std::string line = raw.substr(pos, end - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) break;   // the end of the header block
        if (Lower(line.substr(0, want.size())) == want) {
            std::string value = line.substr(want.size());
            // Folded: the lines that follow and begin with white space.
            for (std::size_t next = end + 1; next < raw.size();) {
                if (raw[next] != ' ' && raw[next] != '\t') break;
                std::size_t stop = raw.find('\n', next);
                if (stop == std::string::npos) stop = raw.size();
                std::string more = raw.substr(next, stop - next);
                if (!more.empty() && more.back() == '\r') more.pop_back();
                value += " " + Trim(more);
                next = stop + 1;
            }
            return Trim(value);
        }
        pos = end + 1;
    }
    return "";
}

namespace {

// `s` split at `sep` outside double quotes.
std::vector<std::string> SplitOutsideQuotes(const std::string& s, char sep) {
    std::vector<std::string> parts;
    std::string current;
    bool quoted = false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (quoted && c == '\\' && i + 1 < s.size()) { current += c; current += s[++i]; continue; }
        if (c == '"') quoted = !quoted;
        if (!quoted && (sep == ' ' ? std::isspace(static_cast<unsigned char>(c)) != 0 : c == sep)) {
            if (!Trim(current).empty()) parts.push_back(Trim(current));
            current.clear();
            continue;
        }
        current += c;
    }
    if (!Trim(current).empty()) parts.push_back(Trim(current));
    return parts;
}

std::string Unquote(const std::string& s) {
    if (s.size() < 2 || s.front() != '"' || s.back() != '"') return s;
    std::string out;
    for (std::size_t i = 1; i + 1 < s.size(); ++i) {
        if (s[i] == '\\' && i + 2 < s.size()) ++i;
        out += s[i];
    }
    return out;
}

} // namespace

AuthResults ParseAuthenticationResults(const std::string& value) {
    AuthResults out;
    // Comments out - "(p=REJECT sp=REJECT dis=NONE)", "(google.com: domain
    // of ... designates ...)" - nested ones too; quoted strings kept.
    std::string clean;
    int depth = 0;
    bool quoted = false;
    for (std::size_t i = 0; i < value.size(); ++i) {
        const char c = value[i];
        if (depth > 0) {
            if (c == '\\') ++i;
            else if (c == '(') ++depth;
            else if (c == ')') --depth;
            continue;
        }
        if (quoted) {
            clean += c;
            if (c == '\\' && i + 1 < value.size()) clean += value[++i];
            else if (c == '"') quoted = false;
            continue;
        }
        if (c == '(') { ++depth; clean += ' '; continue; }
        if (c == '"') quoted = true;
        clean += c;
    }

    // The authserv-id, then one result per ';'.
    const std::vector<std::string> parts = SplitOutsideQuotes(clean, ';');
    if (parts.empty()) return out;
    const std::vector<std::string> head = SplitOutsideQuotes(parts[0], ' ');
    if (!head.empty()) out.authservId = Lower(head[0]);
    for (std::size_t p = 1; p < parts.size(); ++p) {
        const std::vector<std::string> tokens = SplitOutsideQuotes(parts[p], ' ');
        if (tokens.empty()) continue;
        const std::size_t eq = tokens[0].find('=');
        if (eq == std::string::npos) continue;   // "none": nothing was checked
        std::string method = Lower(Trim(tokens[0].substr(0, eq)));
        method = method.substr(0, method.find('/'));   // "dkim/1" -> "dkim"
        const std::string result = Lower(Trim(tokens[0].substr(eq + 1)));
        std::map<std::string, std::string> props;
        for (std::size_t t = 1; t < tokens.size(); ++t) {
            const std::size_t at = tokens[t].find('=');
            if (at == std::string::npos) continue;
            props.emplace(Lower(tokens[t].substr(0, at)), Unquote(tokens[t].substr(at + 1)));
        }
        auto prop = [&props](const char* key) {
            const auto it = props.find(key);
            return it == props.end() ? std::string() : Lower(Trim(it->second));
        };
        auto domainOf = [](const std::string& s) {
            const std::size_t at = s.rfind('@');
            return at == std::string::npos ? s : s.substr(at + 1);
        };
        if (method == "dmarc" && out.dmarc.empty()) {
            out.dmarc     = result;
            out.dmarcFrom = domainOf(prop("header.from"));
        } else if (method == "spf" && out.spf.empty()) {
            out.spf       = result;
            out.spfDomain = domainOf(prop("smtp.mailfrom"));
        } else if (method == "dkim") {
            std::string signer = prop("header.d");
            if (signer.empty()) signer = domainOf(prop("header.i"));
            out.dkim.emplace_back(result, signer);
        }
    }
    return out;
}

std::string VerifiedSenderDomain(const AuthResults& auth, const std::string& fromDomain,
                                 std::string* method) {
    const std::string from = Lower(Trim(fromDomain));
    const std::string fromReg = RegistrableDomain(from);
    if (fromReg.empty() || auth.dmarc == "fail") return "";
    const bool dmarc = auth.dmarc == "pass" &&
                       (auth.dmarcFrom.empty() || RegistrableDomain(auth.dmarcFrom) == fromReg);
    bool dkim = false;
    for (const auto& sig : auth.dkim)
        if (sig.first == "pass" && !sig.second.empty() && RegistrableDomain(sig.second) == fromReg)
            dkim = true;
    if (!dmarc && !dkim) return "";
    if (method) *method = dkim && dmarc ? "DKIM signature and DMARC" : dkim ? "DKIM signature" : "DMARC";
    return from;
}

namespace {

std::string StateWord(const std::string& result) {
    if (result == "pass") return "passed";
    if (result == "fail") return "failed";
    if (result == "softfail") return "soft fail (not authorised, but the domain does not insist)";
    if (result == "neutral") return "neutral (the domain makes no statement)";
    if (result == "none") return "none (the domain publishes no record)";
    if (result == "temperror") return "temporary error (the check could not be completed)";
    if (result == "permerror") return "error (the domain's record is broken)";
    if (result == "policy") return "not accepted by the receiving server's policy";
    return result;
}

AuthCheckState StateOf(const std::string& result) {
    if (result == "pass") return AuthCheckState::Passed;
    if (result == "fail") return AuthCheckState::Failed;
    return AuthCheckState::Neutral;
}

} // namespace

std::vector<AuthCheck> DescribeAuthentication(const AuthResults& auth,
                                              const std::string& fromDomain) {
    std::vector<AuthCheck> checks;
    const std::string from    = Lower(Trim(fromDomain));
    const std::string fromReg = RegistrableDomain(from);
    const std::string fromName = from.empty() ? std::string("the sender's domain") : from;
    const std::string checkedBy = auth.authservId.empty()
        ? std::string("Checked by the receiving server when the message arrived.")
        : "Checked by " + auth.authservId + " when the message arrived.";

    if (!auth.dmarc.empty()) {
        AuthCheck c;
        c.label = "DMARC";
        c.state = StateOf(auth.dmarc);
        const std::string domain = auth.dmarcFrom.empty() ? fromName : auth.dmarcFrom;
        c.tooltip = "DMARC: " + StateWord(auth.dmarc) + "\n";
        if (auth.dmarc == "pass")
            c.tooltip += "The message meets " + domain + "'s own rules for mail with its "
                         "From address: a DKIM signature or SPF check of " + domain +
                         " passed. The From address is genuine - the strongest of the "
                         "three checks.";
        else if (auth.dmarc == "fail")
            c.tooltip += "The message does not meet " + domain + "'s own rules for mail "
                         "with its From address: neither a signature nor the delivering "
                         "server belongs to " + domain + ". The From address is likely forged.";
        else
            c.tooltip += "No verdict on whether " + domain + " sent this message.";
        c.tooltip += "\n" + checkedBy;
        checks.push_back(c);
    }

    if (!auth.dkim.empty()) {
        AuthCheck c;
        c.label = "DKIM";
        bool ownPass = false, anyPass = false, anyFail = false;
        for (const auto& sig : auth.dkim) {
            const bool own = !sig.second.empty() && RegistrableDomain(sig.second) == fromReg;
            if (sig.first == "pass") { anyPass = true; ownPass = ownPass || own; }
            if (sig.first == "fail") anyFail = true;
        }
        c.state = anyPass ? AuthCheckState::Passed
                : anyFail ? AuthCheckState::Failed : AuthCheckState::Neutral;
        c.tooltip = std::string("DKIM: ") + (anyPass ? "passed" : anyFail ? "failed" : "no verdict");
        for (const auto& sig : auth.dkim) {
            const std::string signer = sig.second.empty() ? std::string("an unnamed domain")
                                                           : sig.second;
            const bool own = !sig.second.empty() && RegistrableDomain(sig.second) == fromReg;
            c.tooltip += "\n- Signature of " + signer + ": " + StateWord(sig.first);
            if (sig.first == "pass")
                c.tooltip += own ? " - the message comes from " + signer +
                                       " and was not changed on the way."
                                 : " - " + signer + " (a mail service) sent it; that says "
                                       "nothing about the From address.";
            else if (sig.first == "fail")
                c.tooltip += " - the message was changed on the way, or the signature "
                             "is forged.";
        }
        if (anyPass && !ownPass)
            c.tooltip += "\nNo signature is " + fromName + "'s own.";
        c.tooltip += "\n" + checkedBy;
        checks.push_back(c);
    }

    if (!auth.spf.empty()) {
        AuthCheck c;
        c.label = "SPF";
        c.state = StateOf(auth.spf);
        const std::string domain = auth.spfDomain.empty() ? std::string("the envelope sender's domain")
                                                          : auth.spfDomain;
        c.tooltip = "SPF: " + StateWord(auth.spf) + "\n";
        if (auth.spf == "pass")
            c.tooltip += "The server that delivered the message is one " + domain +
                         " allows to send its mail.";
        else if (auth.spf == "fail")
            c.tooltip += "The server that delivered the message is not one " + domain +
                         " allows to send its mail. Forwarded mail fails this check too.";
        else
            c.tooltip += "No clear answer whether " + domain + " allows the server that "
                         "delivered the message.";
        if (!auth.spfDomain.empty() && RegistrableDomain(auth.spfDomain) != fromReg)
            c.tooltip += "\n(" + auth.spfDomain + " is the envelope sender, often a mail "
                         "service - not necessarily the From address.)";
        c.tooltip += "\n" + checkedBy;
        checks.push_back(c);
    }

    if (checks.empty()) {
        AuthCheck c;
        c.label = "Not checked";
        c.state = AuthCheckState::Neutral;
        c.tooltip = "The receiving server recorded no sender checks (DKIM, SPF, DMARC) for "
                    "this message, so whether " + fromName + " really sent it cannot be "
                    "told from here. Not a warning: many mail servers do not record them.";
        checks.push_back(c);
    }
    return checks;
}

std::string MessageSignatureKind(const std::string& rawMessage) {
    const std::string type = Lower(TopHeaderValue(rawMessage, "Content-Type"));
    if (Contains(type, "multipart/signed")) {
        if (Contains(type, "pkcs7-signature")) return "S/MIME";
        if (Contains(type, "pgp-signature"))   return "OpenPGP";
    }
    if (Contains(type, "application/pkcs7-mime") || Contains(type, "application/x-pkcs7-mime"))
        if (Contains(type, "signed-data")) return "S/MIME";
    return "";
}

std::vector<AuthCheck> DescribeMessageAuthentication(const std::string& rawMessage) {
    const std::string from = DomainOfAddress(TopHeaderValue(rawMessage, "From"));
    std::vector<AuthCheck> checks = DescribeAuthentication(
        ParseAuthenticationResults(TopHeaderValue(rawMessage, "Authentication-Results")), from);
    const std::string kind = MessageSignatureKind(rawMessage);
    if (!kind.empty()) {
        AuthCheck c;
        c.label = kind;
        c.state = AuthCheckState::Neutral;
        c.tooltip = kind + ": the author signed this message with " +
                    (kind == "S/MIME" ? std::string("a personal certificate")
                                      : std::string("an OpenPGP key")) +
                    ".\nUltraMail does not check such signatures yet, so the signature "
                    "says nothing on its own - anyone can attach one.";
        checks.push_back(c);
    }
    return checks;
}

ThreatReport ScanRawMessage(const std::string& rawMessage) {
    ScanInput in;
    if (!BuildScanInput(rawMessage, in)) return ThreatReport{};
    return ScanMessage(in);
}

DomainMismatch FindDomainMismatch(const ScanInput& input) {
    DomainMismatch out;
    const std::string senderDomain = DomainOfAddress(
        input.fromAddr.empty() ? input.fromName : input.fromAddr);
    const std::string senderReg = RegistrableDomain(senderDomain);
    if (senderReg.empty()) return out;

    const MessageLink* button   = nullptr;   // off-domain, with text
    const MessageLink* footer   = nullptr;   // off-domain "unsubscribe" link
    const MessageLink* bareLink = nullptr;   // off-domain, no text
    const auto links = ExtractLinks(input.body, input.bodyIsHtml);   // outlives the picks
    for (const auto& link : links) {
        if (link.host.empty()) continue;
        const std::string linkReg = RegistrableDomain(link.host);
        if (linkReg.empty() || linkReg == senderReg) continue;
        const std::string text = Trim(link.text);
        if (text.empty()) {
            if (!bareLink) bareLink = &link;
        } else if (Contains(Lower(text), "unsubscribe")) {
            if (!footer) footer = &link;
        } else if (!button) {
            button = &link;
        }
    }
    if (!button) button = footer;
    const MessageLink* pick = button ? button : bareLink;
    if (!pick) return out;

    out.found        = true;
    out.senderDomain = senderDomain;
    out.linkDomain   = pick->host;
    out.linkText     = Trim(pick->text);
    out.isButton     = button != nullptr;
    return out;
}

DomainMismatch FindDomainMismatchInRaw(const std::string& rawMessage) {
    ScanInput in;
    if (!BuildScanInput(rawMessage, in)) return DomainMismatch{};
    return FindDomainMismatch(in);
}

} // namespace UltraMail
