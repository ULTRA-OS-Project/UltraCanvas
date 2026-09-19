// Apps/UltraMail/engine/UltraMailThreatScan.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailThreatScan.h"

#include "UltraMailSenderBrands.h"

#include <UltraNet/UltraNetMime.h>

#include <algorithm>
#include <cctype>
#include <map>
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

// A handful of entities is all a link target or an anchor text carries.
std::string DecodeEntities(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '&') { out.push_back(s[i]); continue; }
        if (s.compare(i, 5, "&amp;") == 0)   { out.push_back('&'); i += 4; continue; }
        if (s.compare(i, 4, "&lt;") == 0)    { out.push_back('<'); i += 3; continue; }
        if (s.compare(i, 4, "&gt;") == 0)    { out.push_back('>'); i += 3; continue; }
        if (s.compare(i, 6, "&quot;") == 0)  { out.push_back('"'); i += 5; continue; }
        if (s.compare(i, 6, "&nbsp;") == 0)  { out.push_back(' '); i += 5; continue; }
        if (s.compare(i, 6, "&#x2F;") == 0)  { out.push_back('/'); i += 5; continue; }
        out.push_back('&');
    }
    return out;
}

std::string StripTags(const std::string& html) {
    std::string out;
    bool inTag = false;
    for (char c : html) {
        if (c == '<') { inTag = true; continue; }
        if (c == '>') { inTag = false; out.push_back(' '); continue; }
        if (!inTag) out.push_back(c);
    }
    return DecodeEntities(out);
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
    const std::string t = Trim(Lower(StripTags(text)));
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
std::vector<MessageLink> ExtractLinks(const std::string& body, bool isHtml) {
    std::vector<MessageLink> links;
    if (body.empty()) return links;

    if (!isHtml) {
        const std::string& s = body;
        std::size_t i = 0;
        while (i < s.size()) {
            std::size_t start = std::string::npos;
            for (const char* proto : { "http://", "https://", "www." }) {
                const std::size_t p = s.find(proto, i);
                if (p != std::string::npos && (start == std::string::npos || p < start))
                    start = p;
            }
            if (start == std::string::npos) break;
            std::size_t end = start;
            while (end < s.size() && !std::isspace(static_cast<unsigned char>(s[end])) &&
                   s[end] != '<' && s[end] != '>' && s[end] != '"' && s[end] != '\'')
                ++end;
            // Trailing sentence punctuation is not part of the URL.
            while (end > start && std::string(".,;:!?)]").find(s[end - 1]) != std::string::npos)
                --end;
            MessageLink link;
            link.href = s.substr(start, end - start);
            link.host = HostOf(link.href);
            if (!link.host.empty()) links.push_back(link);
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
        href = Trim(DecodeEntities(href));

        std::string text;
        if (attribute == "href") {
            const std::size_t close = lower.find("</a", tagEnd);
            if (close != std::string::npos && close > tagEnd)
                text = StripTags(body.substr(tagEnd + 1, close - tagEnd - 1));
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

    const std::string bodyLower = Lower(input.bodyIsHtml ? StripTags(input.body) : input.body);
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
    const std::string auth = Lower(input.authResults);
    if (!auth.empty()) {
        if (Contains(auth, "dmarc=fail") || Contains(auth, "spf=fail") ||
            Contains(auth, "dkim=fail")) {
            Add(report, 30, "auth-failure",
                "The sending domain failed its own SPF/DKIM/DMARC checks, so the "
                "From address may be forged.");
        } else if (Contains(auth, "dmarc=pass")) {
            report.score -= 10;   // the From address is at least genuinely theirs
        }
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

        // The anchor text names one address and the link goes to another.
        const std::string claimedHost = ClaimedHostIn(link.text);
        if (!claimedHost.empty()) {
            const std::string claimedReg = RegistrableDomain(claimedHost);
            if (!claimedReg.empty() && claimedReg != linkReg && linkReg != senderReg) {
                Add(report, 50, "link-target-mismatch",
                    "A link reads \"" + claimedHost + "\" but actually goes to " +
                    link.host + ".");
            }
        }

        // A button or link that names a brand and goes somewhere else entirely.
        if (!link.text.empty()) {
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

    if (foreignDomains.size() >= 5) {
        Add(report, 8, "many-foreign-domains",
            "The message links to " + std::to_string(foreignDomains.size()) +
            " different domains, none of them the sender's.");
    }

    // ---- Language that asks for credentials, plus a link off-domain -------
    if (!foreignDomains.empty()) {
        for (const auto& phrase : CredentialPhrases()) {
            if (Contains(bodyLower, phrase)) {
                Add(report, 30, "credential-request",
                    "The message asks the reader to confirm account or payment details "
                    "(\"" + phrase + "\") and links away from the sender's own domain.");
                break;
            }
        }
    }

    // ---- Reply-To pointing somewhere else ---------------------------------
    if (!input.replyTo.empty() && !senderReg.empty()) {
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

ThreatReport ScanRawMessage(const std::string& rawMessage) {
    ThreatReport empty;
    if (rawMessage.empty()) return empty;

    UltraNetMimeMessage msg;
    if (!UltraNet_MimeParse(rawMessage, msg)) return empty;

    ScanInput in;
    in.subject         = msg.subject;
    in.fromName        = msg.from;
    in.fromAddr        = msg.from;
    in.replyTo         = Header(msg.root.headers, "Reply-To");
    in.listUnsubscribe = Header(msg.root.headers, "List-Unsubscribe");
    in.precedence      = Header(msg.root.headers, "Precedence");
    in.autoSubmitted   = Header(msg.root.headers, "Auto-Submitted");
    in.spamFlag        = Header(msg.root.headers, "X-Spam-Flag");
    in.spamStatus      = Header(msg.root.headers, "X-Spam-Status");
    in.authResults     = Header(msg.root.headers, "Authentication-Results");

    std::string body;
    bool isHtml = false;
    if (UltraNet_MimeGetDisplayBody(msg, body, isHtml)) {
        in.body       = std::move(body);
        in.bodyIsHtml = isHtml;
    }

    std::vector<UltraNetMimeAttachmentView> atts;
    UltraNet_MimeCollectAttachments(msg, atts, /*includeInline=*/false);
    for (const auto& a : atts) in.attachmentNames.push_back(a.filename);

    return ScanMessage(in);
}

} // namespace UltraMail
