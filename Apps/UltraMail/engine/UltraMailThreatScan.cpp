// Apps/UltraMail/engine/UltraMailThreatScan.cpp
// Version: 0.8.0 - link-domain-lookalike: link targets dressed up as a brand's;
//                  names in look-alike letters of another script, sender and link
// Version: 0.7.0 - sender-domain-lookalike, government-impersonation;
//                  ThreatScanOptions (a kind switched off is dropped)
// Version: 0.6.0 - romance scams (romance-scam) and cryptocurrency (crypto-content,
//                  crypto-wallet-secret, crypto-payment-demand,
//                  crypto-investment-lure); the body's pictures; Codes / Has
// Version: 0.5.2 - ExtractLinks reads an HTML body's links from the parsed page
//                  (HTML::Parser): a[href], area[href], form[action], none from
//                  comments or scripts
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

#include "HTMLReader/HTMLDocument.h"   // HTML::ExtractPlainText
#include "HTMLReader/HTMLParser.h"     // HTML::Parser (the links of a page)

#include <UltraNet/UltraNetMime.h>

#include <algorithm>
#include <cctype>
#include <map>
#include <mutex>
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
        // Money nobody claimed: the "abandoned baggage" variant.
        "abandoned", "no claim", "without claim", "no owner",
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
        "your own share", "split the fund", "split the funds", "share the funds",
        "50% by 50%", "trust worthy", "trustworthy person", "god fearing", "god-fearing",
        "honest christian", "kindred heart", "stay blessed", "remain blessed", "baggage",
        "luggage", "laugages", "legit and secret", "my private email",
        // The "compensation for scam victims" letter in the FBI's name.
        "atm card", "payment warrant", "release order", "interpol", "monetary fund",
        "monitory funds", "scam victims",
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
    r.findings.push_back({ code, detail, score });
    r.score += score;
}

// Case-insensitive header lookup over a parsed part's header map.
std::string Header(const std::map<std::string, std::string>& headers,
                   const std::string& name) {
    const std::string want = Lower(name);
    for (const auto& h : headers) if (Lower(h.first) == want) return h.second;
    return "";
}

// `phrase` in `textLower` on word boundaries: "honey" is not in "honeymoon",
// "sex" not in "Essex", "your ad" not in "your address". A phrase ending in
// '*' matches as the start of a word ("kiss*": kisses, kisssss).
bool ContainsPhrase(const std::string& textLower, const std::string& phrase) {
    if (phrase.empty()) return false;
    const bool prefix = phrase.back() == '*';
    const std::string p = prefix ? phrase.substr(0, phrase.size() - 1) : phrase;
    auto word = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; };
    std::size_t pos = 0;
    while ((pos = textLower.find(p, pos)) != std::string::npos) {
        const std::size_t end = pos + p.size();
        const bool leftOk = pos == 0 || !word(p.front()) || !word(textLower[pos - 1]);
        const bool rightOk = prefix || end >= textLower.size() || !word(p.back()) ||
                             !word(textLower[end]);
        if (leftOk && rightOk) return true;
        ++pos;
    }
    return false;
}

const std::string* FirstWordPhraseIn(const std::string& textLower,
                                     const std::vector<std::string>& phrases) {
    for (const auto& p : phrases) if (ContainsPhrase(textLower, p)) return &p;
    return nullptr;
}

// "’" (and "`") read as "'", so "I’m" matches "i'm".
std::string StraightQuotes(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s.compare(i, 3, "\xE2\x80\x99") == 0 || s.compare(i, 3, "\xE2\x80\x98") == 0) {
            out.push_back('\'');
            i += 2;
            continue;
        }
        out.push_back(s[i] == '`' ? '\'' : s[i]);
    }
    return out;
}

bool IsPictureName(const std::string& filename) {
    static const std::set<std::string> ext = {
        "jpg", "jpeg", "png", "gif", "heic", "heif", "webp", "bmp", "tif", "tiff",
    };
    return ext.count(ExtensionOf(filename)) > 0;
}

// ---------------------------------------------------------------------------
// Romance scams
// ---------------------------------------------------------------------------
// A stranger writes a love letter: pet names and talk of fate, a short
// self-introduction (name, age, divorced, a nurse in Russia), how they came
// to write ("I saw your profile", "it is destiny"), a photo or two, and a push
// to answer - often to a private address, or to a site that "only verifies"
// with a bank card. Later mails of the same thread add the assurances ("I am
// for real", a "scan passport") and the request: a ticket, a visa, a laptop
// for the webcam, the rent, Western Union, crypto. No one phrase gives it
// away, so the rule counts kinds of signs; each kind counts once.
struct RomanceSigns {
    const char*              what;      // how the reason names the kind
    std::vector<std::string> phrases;
};

const std::vector<RomanceSigns>& RomanceSignKinds() {
    static const std::vector<RomanceSigns> v = {
        { "pet names", {
            "my dear", "my darling", "darling", "sweetheart", "sweetie", "honey", "my love",
            "dearest", "my sweet", "my angel", "my king", "my queen", "my prince",
            "my princess", "my beloved", "dear one", "join me dear", "kiss*", "xoxo",
            "truly yours", "yours only", "yours forever", "forever yours",
            "mein schatz", "meine liebe", "liebling", "k\xC3\xBCsse", "kuss" } },
        { "talk of love or attraction", {
            "romantic", "romance", "soul mate", "soulmate", "true love", "real love",
            "look for love", "looking for love", "search of love", "search of real love",
            "find love", "find my love", "believe in love", "believes in love",
            "fall in love", "falling in love", "in love with you", "future husband",
            "future wife", "right man", "right woman", "man of my dreams",
            "woman of my dreams", "serious relationship", "long-term relationship",
            "long term relationship", "life partner", "serious intentions",
            "get to know each other", "get to know you better", "single lady",
            "single woman", "single girl", "lonely", "loneliness", "attracted to you",
            "i am attracted", "i'm attracted", "attracted regarding", "drawn to you",
            "heart beat faster", "my heart", "eternal happiness", "everlasting",
            "our journey together", "together forever", "attractive girl",
            "attractive woman", "attractive lady", "young, attractive", "beautiful girl",
            "pretty girl", "hot girl", "handsome", "love to share", "wahre liebe",
            "marriage in future", "make baby", "i am rich", "i'm rich", "im rich",
            "gro\xC3\x9F" "e liebe", "ernsthafte beziehung", "den richtigen mann",
            "partner f\xC3\xBCr" "s leben", "einsam" } },
        { "an offer of sex or a meeting", {
            "hook up", "hookup", "hooking up", "meet up", "meeting up", "escort", "escorts",
            "sexy", "horny", "naughty", "fuck*", "have sex", "sex with", "sex tonight",
            "explicit", "your desires",
            "one night stand", "get laid", "nude*", "pleasant time", "have fun together",
            "please you" } },
        { "a self-introduction", {
            "my name is", "my age is", "years young", "divorced", "never married",
            "never been married", "no kids", "no children", "single mother", "single mom",
            "widow", "about me", "about myself", "i work as", "my character",
            "my hobbies", "cm tall", "blue eyes", "brown eyes", "green eyes",
            "blonde hair", "brown hair", "black hair", "people say that",
            "i work and live in", "mein name ist", "ich bin geschieden",
            "keine kinder", "\xC3\xBC" "ber mich" } },
        { "how they came to write to you", {
            "your profile", "your posting", "your ad", "your advert", "your advertisement",
            "dating site", "dating website", "dating app", "dating service",
            "dating agency", "marriage agency", "on the site", "on this site",
            "found your email", "found your e-mail", "found your address",
            "got your email", "got your e-mail", "got your address",
            "your email address on", "i have information that you",
            "among the millions", "lucky star", "horoscope", "destiny", "it's fate",
            "it is fate", "it was fate", "chance meeting", "by chance",
            "you don't know me", "you do not know me", "you dont know me",
            "are you real", "real deal", "are you genuine", "fake profile",
            "fake profiles", "untrue humans", "tired of fake", "sick of fake",
            "my new friend", "is writing to you", "are you still looking for",
            "still looking for friend", "still looking for a friend",
            "partnervermittlung", "schicksal",
            "dein profil", "ihr profil" } },
        { "a site to sign up on or to be \"verified\" at", {
            "get my number", "my number will be", "my number is on", "login there",
            "log in there", "sign up there", "signup there", "register there",
            "create an account", "require you to signup", "require you to sign up",
            "signup with your", "sign up with your", "they never charge", "won't charge",
            "will not charge", "free to join", "criminal history", "criminal record",
            "background check", "never be too careful", "posted a review", "my review",
            "escorts button", "verify that you are", "verify you are not",
            "to make sure you are not" } },
        { "guilt or pressure to answer", {
            "don't upset", "do not upset", "make her bored", "make me bored",
            "she is bored", "she's bored", "she is waiting", "she's waiting",
            "keep her waiting", "don't make her wait", "keep me waiting",
            "play with my feelings", "playing with my feelings", "no playing",
            "playing fool", "don't play with me", "if you don't trust me",
            "i have been waiting for you" } },
        { "a pretended acquaintance", {
            "see you again", "remember me", "it's me again", "it is me again",
            "emailed each other", "wrote each other", "we talked before",
            "we chatted before",
            "hey again", "heyy again", "heyyy again", "did you get my", "did you see my",
            "did you receive my", "haven't heard from you", "have not heard from you",
            "have not being hearing from you", "not hearing from you", "where are you my",
            "why don't you answer", "why don't you write", "why didn't you answer",
            "why didn't you write", "you forgot me", "have you forgotten me",
            "wo bist du" } },
        { "assurances of being real and honest", {
            "i am for real", "i'm for real", "am for real", "i am real", "i'm real",
            "the real me", "everything about me", "i am honest", "i'm honest",
            "honest to you", "sincere to you", "i am sincere", "i am serious",
            "i'm serious", "not a scammer", "not a fake", "not an escort",
            "not a prostitute", "not here for your money", "not after your money",
            "not asking you for much", "scan passport", "scanned passport",
            "copy of my passport", "my passport", "my id card", "believe in your words" } },
        { "photos", {
            "my photo", "my photos", "my picture", "my pictures", "my pics", "my pic",
            "photo of me", "photos of me", "picture of me", "pictures of me",
            "two pictures", "two photos", "my 2 photos", "my two photos", "my 3 photos",
            "my three photos", "emailing you my", "sending you my", "some photos",
            "some pictures",
            "hope you like them", "hope you like my", "here is my photo", "here is mine",
            "your photo", "your photos", "your picture", "your pictures", "and photos",
            "look through my pictures", "meine fotos", "mein foto", "dein foto" } },
        { "a push to write back", {
            "write back", "write me", "write to me", "answer back", "answer me",
            "reply me", "reply to me", "reply as soon as possible", "please reply",
            "waiting for your", "wait for your", "await your", "awaiting your",
            "earliest response", "soonest reply", "waiting fo u", "waiting for u",
            "waiting to hear from you", "hope to hear from you", "hope to find your email",
            "personal details", "personal email", "personal e-mail", "private email",
            "private e-mail", "my email is", "my e-mail is", "contact me at",
            "write to my", "waiting for your call", "join me", "email me", "e-mail me",
            "give me your email", "give me your new email", "your email addresses",
            "schreib mir",
            "warte auf deine antwort", "antworte mir" } },
        { "a request for money", {
            "send me the money", "send me money", "send the money", "send me some money",
            "send money", "the money for", "western union", "moneygram", "money gram",
            "gift card", "itunes card", "steam card", "google play card", "amazon card",
            "plane ticket", "air ticket", "flight ticket", "ticket to come",
            "travel expenses", "visa fee", "customs fee", "customs", "hospital bill",
            "medical bill", "pay my rent", "rent payment", "housing obligations",
            "assistance with paying", "help me pay", "help me with money", "lend me",
            "loan me", "how much you can send", "how much can you send", "a new laptop",
            "cheap laptop", "buy a laptop", "buy a webcam", "internet cafe",
            "geld schicken", "\xC3\xBC" "berweisen" } },
        { "a hardship or far-away story", {
            "deployed", "peacekeeping", "peace keeping", "military base", "us army",
            "u.s. army", "soldier", "oil rig", "offshore", "widower", "my late wife",
            "my old mother", "sick mother", "my mother is sick", "the war", "war zone",
            "because of war", "my state is not good", "luhansk", "lugansk", "donetsk" } },
    };
    return v;
}

// The countries the "bride" letters write from, as "I live in Russia".
const std::vector<std::string>& RomanceHomeCountries() {
    static const std::vector<std::string> v = {
        "russia", "ukraine", "belarus", "kazakhstan", "moldova", "kyrgyzstan",
        "uzbekistan", "philippines", "russland",
    };
    return v;
}

// A job application introduces its writer too, with a photo, and asks for an
// answer - and is not a love letter.
const std::vector<std::string>& JobApplicationPhrases() {
    static const std::vector<std::string> v = {
        "curriculum vitae", "my cv", "my resume", "my r\xC3\xA9sum\xC3\xA9", "cover letter",
        "vacancy", "vacancies", "job posting", "job application", "job offer",
        "job interview", "apply for the", "application for the", "advertised position",
        "the position of", "bewerbung", "lebenslauf", "stellenanzeige",
    };
    return v;
}

// How a domain imitates a brand, for a reason: "imitates Facebook:
// \"faceebook\" is Facebook's name misspelt".
std::string DescribeLookalike(const DomainLookalike& l) {
    const std::string& name = l.brand->name;
    switch (l.kind) {
        case LookalikeKind::Misspelt:
            return "imitates " + name + ": \"" + l.worn + "\" is " + name + "'s name misspelt";
        case LookalikeKind::OwnDomain:
            return "puts " + name + "'s own domain (" + l.worn + ") in front of an unrelated one";
        case LookalikeKind::Homograph:
            return "reads \"" + l.unicode + "\" - " + name + "'s name written with " +
                   l.letters + " letters that look like Latin ones";
        default:
            return "wears " + name + "'s name (\"" + l.worn + "\") padded with other words";
    }
}

// ---------------------------------------------------------------------------
// Government agencies and international organisations
// ---------------------------------------------------------------------------
// The names the "compensation for scam victims", "your ATM card" and "warrant
// for your arrest" letters write in. None of these bodies writes to a private
// person out of the blue about money or an arrest - least of all from an
// address that is not theirs.
struct Agency {
    const char*              name;      // as the reason says it
    std::vector<std::string> claims;    // lowercase, matched as words
    std::vector<std::string> domains;   // its own, beyond the government suffixes
};

const std::vector<Agency>& Agencies() {
    static const std::vector<Agency> v = {
        { "the FBI", { "fbi", "federal bureau of investigation" }, { "fbi.gov", "ic3.gov" } },
        { "Interpol", { "interpol" }, { "interpol.int" } },
        { "the IMF", { "imf", "international monetary fund", "international monitory fund",
                       "international monitory funds" }, { "imf.org" } },
        { "the United Nations", { "united nations" }, { "un.org" } },
        { "the World Bank", { "world bank" }, { "worldbank.org" } },
        { "Europol", { "europol" }, { "europa.eu" } },
        { "the CIA", { "central intelligence agency" }, { "cia.gov" } },
        { "Homeland Security", { "homeland security" }, { "dhs.gov" } },
        { "the Department of Justice", { "department of justice" },
          { "justice.gov", "usdoj.gov" } },
        { "the US Treasury", { "department of the treasury", "us treasury", "u.s. treasury",
                               "treasury department" }, { "treasury.gov" } },
        { "the Federal Reserve", { "federal reserve" }, { "federalreserve.gov" } },
        { "the Secret Service", { "secret service" }, { "secretservice.gov" } },
        { "the IRS", { "irs", "internal revenue service" }, { "irs.gov" } },
        { "the European Central Bank", { "european central bank" }, { "ecb.europa.eu" } },
        { "the European Commission", { "european commission" }, { "europa.eu" } },
        { "the Bundeskriminalamt", { "bundeskriminalamt", "bka" }, { "bka.de" } },
        { "the Bundespolizei", { "bundespolizei" }, { "bundespolizei.de" } },
        { "Scotland Yard", { "scotland yard", "metropolitan police" }, { "met.police.uk" } },
        { "the National Crime Agency", { "national crime agency" },
          { "nationalcrimeagency.gov.uk" } },
        { "the Central Bank of Nigeria", { "central bank of nigeria" }, { "cbn.gov.ng" } },
        { "ECOWAS", { "ecowas" }, { "ecowas.int" } },
    };
    return v;
}

// A government's own domain: .gov, .mil, .int, a country's gov.xx / gob.xx /
// gouv.xx / govt.xx / go.xx / gv.xx, and the federal and EU domains that do
// not say so in their suffix (bund.de, admin.ch, gv.at, gc.ca, europa.eu,
// police.uk).
bool IsGovernmentDomain(const std::string& domainIn) {
    const std::string d = "." + Lower(domainIn);
    auto endsWith = [&d](const std::string& tail) {
        return d.size() >= tail.size() && d.compare(d.size() - tail.size(), tail.size(), tail) == 0;
    };
    for (const char* tail : { ".gov", ".mil", ".int", ".bund.de", ".admin.ch", ".gv.at",
                              ".gc.ca", ".canada.ca", ".europa.eu", ".police.uk",
                              ".gouv.fr" })
        if (endsWith(tail)) return true;
    const std::size_t last = d.rfind('.');
    const std::size_t prev = last == 0 ? std::string::npos : d.rfind('.', last - 1);
    if (prev == std::string::npos || d.size() - last - 1 != 2) return false;
    const std::string second = d.substr(prev + 1, last - prev - 1);
    for (const char* s : { "gov", "gob", "gouv", "govt", "go", "gv", "mil", "police" })
        if (second == s) return true;
    return false;
}

// What makes a mention of an agency a letter in its name: it addresses the
// reader about their money, their case or their arrest.
const std::vector<std::string>& OfficialLetterPhrases() {
    static const std::vector<std::string> v = {
        "attention beneficiary", "dear beneficiary", "beneficiary", "your payment",
        "your fund", "your funds", "atm card", "compensation", "scam victim",
        "scam victims", "arrest warrant", "warrant of arrest", "warrant for your arrest",
        "you will be arrested", "legal action against you", "pay the fine", "pay a fine",
        "your case", "case number", "this office", "our office", "we the", "hereby",
        "officially", "haftbefehl", "ihre zahlung", "aktenzeichen",
    };
    return v;
}

// ---------------------------------------------------------------------------
// Cryptocurrency
// ---------------------------------------------------------------------------
const std::vector<std::string>& CryptoTerms() {
    static const std::vector<std::string> v = {
        "bitcoin", "bitcoins", "btc", "ethereum", "usdt", "crypto", "cryptocurrency",
        "cryptocurrencies", "crypto currency", "crypto-currency", "cryptocoin",
        "blockchain", "altcoin", "altcoins", "dogecoin", "litecoin", "solana", "xrp",
        "binance", "coinbase", "metamask", "trust wallet", "crypto wallet",
        "bitcoin wallet", "wallet address", "seed phrase", "recovery phrase", "nft",
        "nfts", "defi", "krypto", "kryptow\xC3\xA4hrung",
        "kryptow\xC3\xA4hrungen", "kryptowaehrung",
    };
    return v;
}

// What a crypto "investment" promises: profit without risk, a platform, a
// balance waiting to be withdrawn, something free to claim.
const std::vector<std::string>& CryptoProfitPhrases() {
    static const std::vector<std::string> v = {
        "guaranteed profit", "guaranteed profits", "guaranteed return",
        "guaranteed returns", "guaranteed income", "double your", "triple your",
        "daily profit", "daily profits", "daily return", "daily returns", "weekly profit",
        "monthly profit", "risk-free", "risk free", "no risk", "passive income",
        "earn up to", "investment opportunity", "investment platform",
        "trading platform", "trading account", "trading bot", "your profit",
        "your profits", "your earnings", "withdraw your", "withdrawal fee",
        "account balance", "your balance", "has been credited", "claim your", "airdrop",
        "giveaway", "free bitcoin", "free btc", "free crypto", "mining contract",
        "cloud mining", "account manager", "investment advisor", "insider",
        "garantierte rendite", "gewinn garantiert",
    };
    return v;
}

// The secrets that own a wallet. Whoever has one of them has the coins.
const std::vector<std::string>& WalletSecretPhrases() {
    static const std::vector<std::string> v = {
        "seed phrase", "recovery phrase", "secret phrase", "secret recovery phrase",
        "mnemonic phrase", "mnemonic", "private key", "private keys", "12-word",
        "24-word", "12 word", "24 word", "backup phrase", "wallet phrase",
    };
    return v;
}

// A sentence of `textLower` that asks for a wallet's secret - not one that
// warns never to give it away. Returns the secret named, or "".
std::string WalletSecretRequest(const std::string& textLower) {
    static const std::vector<std::string> asks = {
        "enter", "confirm", "verify", "validate", "provide", "submit", "send us",
        "send your", "share your", "import", "synchronize", "synchronise", "sync",
        "type in", "fill in", "re-enter", "update", "reactivate",
    };
    static const std::vector<std::string> negations = {
        "never", "not", "don't", "do not", "no one", "nobody", "won't", "nie",
        "niemals", "nicht",
    };
    std::size_t start = 0;
    while (start < textLower.size()) {
        std::size_t end = textLower.find_first_of(".!?;\n", start);
        if (end == std::string::npos) end = textLower.size();
        const std::string sentence = textLower.substr(start, end - start);
        if (const std::string* secret = FirstWordPhraseIn(sentence, WalletSecretPhrases()))
            if (FirstWordPhraseIn(sentence, asks) && !FirstWordPhraseIn(sentence, negations))
                return *secret;
        start = end + 1;
    }
    return std::string();
}

// A wallet address in `text` (case kept: Base58 tells 0/O and l/I apart):
// Bitcoin (bc1…, or 1…/3… of 26-35 characters), Ethereum (0x + 40 hex) or
// TRON (T…). "" when there is none.
std::string CryptoWalletAddress(const std::string& text) {
    static const std::regex bech32(R"(\b[bB][cC]1[ac-hj-np-zAC-HJ-NP-Z02-9]{25,87}\b)");
    static const std::regex ethereum(R"(\b0x[0-9a-fA-F]{40}\b)");
    static const std::regex base58(R"(\b[13T][1-9A-HJ-NP-Za-km-z]{25,34}\b)");
    // Not a part of a link: a token after "?token=" or "/" is not an address.
    auto inLink = [&text](std::ptrdiff_t at) {
        return at > 0 && std::string("/=?&#:%").find(text[at - 1]) != std::string::npos;
    };
    for (const std::regex* re : { &bech32, &ethereum })
        for (auto it = std::sregex_iterator(text.begin(), text.end(), *re);
             it != std::sregex_iterator(); ++it)
            if (!inLink(it->position())) return it->str();
    // A Base58 address mixes capitals, small letters and digits; a word or a
    // long number does not.
    for (auto it = std::sregex_iterator(text.begin(), text.end(), base58);
         it != std::sregex_iterator(); ++it) {
        if (inLink(it->position())) continue;
        const std::string hit = it->str();
        bool upper = false, lower = false, digit = false;
        for (std::size_t i = 1; i < hit.size(); ++i) {
            const unsigned char c = static_cast<unsigned char>(hit[i]);
            upper = upper || std::isupper(c);
            lower = lower || std::islower(c);
            digit = digit || std::isdigit(c);
        }
        if (upper && lower && digit) return hit;
    }
    return std::string();
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

std::string ThreatReport::Codes() const {
    std::string out;
    for (const auto& f : findings) {
        if (!out.empty()) out.push_back(',');
        out += f.code;
    }
    return out;
}

bool ThreatReport::Has(const std::string& code) const {
    for (const auto& f : findings) if (f.code == code) return true;
    return false;
}

bool FindingEnabled(const ThreatScanOptions& o, const std::string& code) {
    if (code == "romance-scam")             return o.romance;
    if (code == "government-impersonation") return o.government;
    if (code == "advance-fee-fraud" || code == "reply-elsewhere") return o.advanceFee;
    if (code == "crypto-content")           return o.cryptoCaution;
    if (code.compare(0, 7, "crypto-") == 0) return o.cryptoScams;
    if (code.compare(0, 11, "attachment-") == 0) return o.attachments;
    if (code == "spam-flag")                return o.spamFlag;
    static const std::set<std::string> phishing = {
        "auth-failure", "brand-impersonation", "sender-domain-lookalike",
        "borrowed-brand-pictures", "link-userinfo", "link-ip-host", "link-punycode",
        "link-nonascii-host", "link-shortener", "link-target-mismatch",
        "link-brand-mismatch", "link-brand-lookalike", "link-domain-lookalike",
        "insecure-login-link",
        "many-foreign-domains", "credential-request", "reply-to-mismatch",
    };
    if (phishing.count(code)) return o.phishing;
    return true;
}

namespace {
std::mutex         g_optionsMutex;
ThreatScanOptions  g_options;
} // namespace

void SetThreatScanOptions(const ThreatScanOptions& options) {
    std::lock_guard<std::mutex> lock(g_optionsMutex);
    g_options = options;
}

ThreatScanOptions GetThreatScanOptions() {
    std::lock_guard<std::mutex> lock(g_optionsMutex);
    return g_options;
}

bool HasFindingCode(const std::string& codes, const std::string& code) {
    if (code.empty()) return false;
    std::size_t start = 0;
    while (start <= codes.size()) {
        std::size_t end = codes.find(',', start);
        if (end == std::string::npos) end = codes.size();
        if (codes.compare(start, end - start, code) == 0 && end - start == code.size())
            return true;
        start = end + 1;
    }
    return false;
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

    // HTML: the elements that navigate - <a href>, <area href>, <form action> -
    // from the parsed page, in document order. The parser decodes the
    // attribute values; a link's text is what it shows (a word dressed up with
    // <b> stays whole, a "button" table's cells a space apart). Comments,
    // <script> and <style> hold no links.
    UltraCanvas::HTML::Parser parser;
    UltraCanvas::HTML::ParseOptions options;
    options.keepWhitespaceNodes = true;   // "Click <b>here</b>": the space between
    UltraCanvas::HTML::Document document = parser.Parse(body, options);
    if (!document.root) return links;
    document.root->ForEachElement([&](UltraCanvas::HTML::Node& element) {
        const bool anchor = element.tag == "a" || element.tag == "area";
        if (!anchor && element.tag != "form") return true;
        const std::string href = Trim(element.GetAttribute(anchor ? "href" : "action"));
        if (href.empty()) return true;
        MessageLink link;
        link.href = href;
        if (element.tag == "a") link.text = UltraCanvas::HTML::ExtractPlainText(element);
        link.host = HostOf(href);
        links.push_back(link);
        return true;
    });
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

    // ---- A sender domain dressed up as a brand's ---------------------------
    // The From address itself pretends: "faceebookinbox.biz",
    // "paypal-secure-login.com", "amaz0n-billing.com",
    // "paypal.com.account-check.ru" (BrandImitatedByDomain). A domain can sign
    // its own mail, so a proven look-alike is still a look-alike.
    {
        const DomainLookalike lookalike = BrandImitatedByDomain(senderDomain);
        if (lookalike.brand) {
            Add(report, lookalike.kind == LookalikeKind::Name ? 45 : 50,
                "sender-domain-lookalike",
                "The sender's domain " + senderDomain + " " + DescribeLookalike(lookalike) +
                ", but it is not one of " + lookalike.brand->name + "'s domains.");
        }
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
    std::set<std::string> lookalikeHostsChecked;   // once per host, not per link
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

        // A link to a domain dressed up as a brand's - "faceebook-login.com",
        // "paypa1.com", "pаypal.com" in Cyrillic letters (BrandImitatedByDomain,
        // as for the sender). The sender's own domain is the sender rule's.
        if (!ownDomain && !report.Has("link-domain-lookalike") &&
            lookalikeHostsChecked.insert(link.host).second) {
            const DomainLookalike l = BrandImitatedByDomain(link.host);
            if (l.brand) {
                Add(report, l.kind == LookalikeKind::Name ? 40 : 50, "link-domain-lookalike",
                    "A link" + (link.text.empty() ? std::string()
                                                  : " labelled \"" + Trim(link.text) + "\"") +
                    " goes to " + link.host + ", which " + DescribeLookalike(l) +
                    " - not one of " + l.brand->name + "'s domains.");
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
                if (deceased) extras.push_back("a dead relative, an estate or money nobody claimed (\"" + *deceased + "\")");
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

    // ---- Government agencies and international organisations -------------
    // A letter in the FBI's, Interpol's or the IMF's name from an address that
    // is not theirs. In the sender's name or domain it is a claim on its own
    // ("FBI <director@fbi-atm-center.example>"); in the subject or the text it
    // counts when it addresses the reader about money, a case or an arrest -
    // a news item about the FBI does not. Not for a newsletter from a domain
    // of its own, and not where the brand table already said so.
    bool agencyDomain = IsGovernmentDomain(senderDomain);   // one agency may name another
    for (const Agency& agency : Agencies())
        for (const auto& d : agency.domains)
            agencyDomain = agencyDomain || RegistrableDomain(senderDomain) == RegistrableDomain(d);
    if (!(input.options.phishing && report.Has("brand-impersonation")) && !agencyDomain &&
        !(report.bulk && !IsPersonalMailboxDomain(senderDomain))) {
        std::string who = input.fromName.empty() ? input.fromAddr : input.fromName;
        if (const std::size_t lt = who.find('<'); lt != std::string::npos) who = who.substr(0, lt);
        std::string domainWords = senderDomain;
        for (char& c : domainWords) if (c == '.' || c == '-' || c == '_') c = ' ';
        const std::string named = Lower(who) + " " + domainWords;
        const std::string letterText = Lower(input.subject) + "\n" + bodyLower;
        for (const Agency& agency : Agencies()) {
            const std::string from = senderDomain.empty() ? std::string("an address with no domain")
                                                          : senderDomain;
            if (const std::string* claim = FirstWordPhraseIn(named, agency.claims)) {
                Add(report, 45, "government-impersonation",
                    "The message presents itself as " + std::string(agency.name) + " (\"" +
                    *claim + "\"), but it was sent from " + from +
                    ", which is not a government address.");
                break;
            }
            const std::string* claim = FirstWordPhraseIn(letterText, agency.claims);
            const std::string* letter = claim ? FirstWordPhraseIn(letterText, OfficialLetterPhrases())
                                              : nullptr;
            if (claim && letter) {
                Add(report, 30, "government-impersonation",
                    "The message speaks in the name of " + std::string(agency.name) +
                    " about your money, a case or an arrest (\"" + *letter + "\"), but it "
                    "was sent from " + from + ", which is not a government address. "
                    "Agencies do not announce payments, compensation or arrests by email.");
                break;
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

    // ---- Answers asked for at another address ------------------------------
    // "Please find my contact email address for us to proceed: (x@yahoo.com)"
    // - the advance-fee and romance letters move the conversation to a free
    // mailbox other than the one they were sent from (which the provider may
    // already have closed). An address of a company domain in a signature is
    // not this, and neither is the sender repeating their own.
    if (!verifiedBrand) {
        std::string from = Lower(input.fromAddr);
        if (const std::size_t lt = from.find('<'); lt != std::string::npos) {
            const std::size_t gt = from.find('>', lt);
            from = from.substr(lt + 1, gt == std::string::npos ? std::string::npos : gt - lt - 1);
        }
        from = Trim(from);
        static const std::vector<std::string> invitations = {
            "contact me", "contact email", "write me", "write to me", "reply to",
            "email me", "e-mail me", "my email", "my e-mail", "my private email",
            "my personal email", "reach me", "get back to me", "send your reply",
            "to proceed", "private email", "kontaktieren sie mich", "schreib mir",
            "meine e-mail",
        };
        const std::string* invitation = FirstWordPhraseIn(bodyLower, invitations);
        for (std::size_t at = bodyLower.find('@'); invitation && at != std::string::npos;
             at = bodyLower.find('@', at + 1)) {
            std::size_t a = 0, b = 0;
            if (!MailAddressAt(bodyLower, at, a, b)) continue;
            const std::string address = bodyLower.substr(a, b - a);
            if (address == from || !IsPersonalMailboxDomain(DomainOfAddress(address))) continue;
            Add(report, 15, "reply-elsewhere",
                "The message asks for answers at another address (" + address +
                "), a free mailbox that is not the one it was sent from" +
                (from.empty() ? std::string() : " (" + from + ")") + ".");
            break;
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

    // ---- Romance scams -----------------------------------------------------
    // A stranger's love letter (RomanceSignKinds): it needs both something
    // romantic (pet names, love, sex, a guilt trip) and something only a
    // stranger writes (an introduction, how they "found" you, a site to sign
    // up on, assurances of being real) - a partner's "my dear, here are the
    // photos, write back" has the first and not the second. Not a proven
    // brand's mail (a dating service writing about matches), not a newsletter
    // from a domain of its own, not a job application, and not a long mail,
    // which these never are.
    const bool freeMailbox = IsPersonalMailboxDomain(senderDomain);
    const std::string letter = StraightQuotes(Lower(input.subject) + "\n" + bodyLower);
    const std::string* cryptoTerm = FirstWordPhraseIn(letter, CryptoTerms());
    if (!verifiedBrand && !(report.bulk && !freeMailbox) && letter.size() < 60000 &&
        !FirstWordPhraseIn(letter, JobApplicationPhrases())) {
        enum Kind { kPetNames, kLove, kSex, kIntro, kContact, kSite, kPressure,
                    kAcquaintance, kSincerity, kPhotos, kReply, kMoney, kStory };
        const auto& kinds = RomanceSignKinds();
        std::vector<std::string> found(kinds.size());
        for (std::size_t k = 0; k < kinds.size(); ++k)
            if (const std::string* p = FirstWordPhraseIn(letter, kinds[k].phrases))
                found[k] = *p;
        if (found[kIntro].empty()) {
            // "I'm 31 years old", "ich bin 30 Jahre", "I live in Russia".
            // "I'm single", "Im lawyer", "I am a nurse".
            static const std::regex age(
                R"(\b(?:i am|i'm|im|ich bin)\s+(?:a\s+)?\d{2}\s?(?:years?|yrs|jahre)\b)");
            static const std::regex status(
                R"(\b(?:i am|i'm|im)\s+(?:an?\s+)?(?:single|divorced|nurse|doctor|lawyer|)"
                R"(engineer|teacher|soldier|surgeon|widow|widower|businessman|businesswoman)\b)");
            std::smatch m;
            if (std::regex_search(letter, m, age) || std::regex_search(letter, m, status))
                found[kIntro] = m.str();
            for (const std::string& country : RomanceHomeCountries()) {
                if (!found[kIntro].empty()) break;
                for (const char* lead : { "i live in ", "i am from ", "i'm from ", "living in ",
                                          "ich lebe in ", "ich komme aus " })
                    if (ContainsPhrase(letter, lead + country)) {
                        found[kIntro] = lead + country;
                        break;
                    }
            }
        }
        if (found[kPressure].empty()) {
            // A screen name speaking of herself: "don't upset Shui98 or make her bored".
            static const std::regex screenName(
                R"((?:^|\s)([a-z]{3,}_?\d{2,4})(?=[\s,.!?]|$)[^.!?\n]{0,30}?\b(?:her|she)\b)");
            std::smatch m;
            if (std::regex_search(letter, m, screenName)) found[kPressure] = Trim(m.str());
        }
        if (found[kMoney].empty() && cryptoTerm) found[kMoney] = *cryptoTerm;

        const bool romantic = !found[kPetNames].empty() || !found[kLove].empty() ||
                              !found[kSex].empty() || !found[kPressure].empty();
        const bool stranger = !found[kIntro].empty() || !found[kContact].empty() ||
                              !found[kSite].empty() || !found[kPressure].empty() ||
                              !found[kSincerity].empty();

        // The photo these letters nearly always carry.
        std::vector<std::string> photos;
        for (const auto& name : input.attachmentNames)
            if (IsPictureName(name)) photos.push_back(name);
        for (const auto& name : input.pictureNames) photos.push_back(name);

        int signs = (photos.empty() ? 0 : 1) + (freeMailbox ? 1 : 0);
        for (const auto& f : found) if (!f.empty()) ++signs;
        if (romantic && stranger && signs >= 3) {
            std::vector<std::string> parts;
            for (std::size_t k = 0; k < kinds.size(); ++k)
                if (!found[k].empty())
                    parts.push_back(std::string(kinds[k].what) + " (\"" + found[k] + "\")");
            std::string why = "The message reads like a romance scam: ";
            for (std::size_t i = 0; i < parts.size(); ++i)
                why += (i == 0 ? "" : (i + 1 == parts.size() ? " and " : ", ")) + parts[i];
            if (!photos.empty()) {
                std::string named = photos.front().empty() ? std::string()
                                                           : " (\"" + photos.front() + "\")";
                why += photos.size() == 1
                    ? ", with a photo attached" + named
                    : ", with " + std::to_string(photos.size()) + " photos attached" + named;
            }
            if (freeMailbox) why += ", sent from a free mailbox (" + senderDomain + ")";
            why += ". Romance scammers write to strangers with a made-up profile and someone "
                   "else's photos, and once they are trusted they ask for money - a ticket, "
                   "a visa, the rent, a laptop, crypto.";
            Add(report, signs >= 5 ? 50 : signs == 4 ? 35 : 22, "romance-scam", why);
        }
    }

    // ---- Cryptocurrency ----------------------------------------------------
    // Any mail about crypto gets a word of caution: a payment cannot be called
    // back. Three patterns are scams outright - asking for a wallet's recovery
    // phrase, an address to pay into (blackmail, fake invoices), and profit
    // promised on an "investment". A proven exchange writing about its own
    // service is spared the last two (a deposit address, "your balance").
    if (cryptoTerm) {
        Add(report, verifiedBrand ? 0 : 10, "crypto-content",
            "The message is about cryptocurrency (\"" + *cryptoTerm + "\"). A crypto payment "
            "cannot be called back: whoever receives it keeps it.");
        const std::string secret = WalletSecretRequest(letter);
        if (!secret.empty()) {
            Add(report, 50, "crypto-wallet-secret",
                "The message asks for a crypto wallet's " + secret + ". No genuine wallet, "
                "exchange or help desk ever asks for it: whoever has it owns the wallet and "
                "everything in it.");
        }
        if (!verifiedBrand) {
            const std::string address =
                CryptoWalletAddress(input.bodyIsHtml ? UltraCanvas::HTML::ExtractPlainText(input.body)
                                                 : input.body);
            if (!address.empty()) {
                Add(report, 40, "crypto-payment-demand",
                    "The message gives a crypto wallet address to pay into (" + address +
                    "). Blackmail (\"I recorded you through your camera\") and fake invoices "
                    "ask for payment this way, because it cannot be traced or reversed.");
            }
            std::string lure;
            if (const std::string* p = FirstWordPhraseIn(letter, CryptoProfitPhrases())) {
                lure = *p;
            } else {
                static const std::regex percent(
                    R"(\d+(?:[.,]\d+)?\s?%\s?(?:daily|per day|a day|weekly|per week|a week|monthly|per month|a month|returns?|roi|profit))");
                std::smatch m;
                if (std::regex_search(letter, m, percent)) lure = m.str();
            }
            if (!lure.empty()) {
                Add(report, 35, "crypto-investment-lure",
                    "The message pairs cryptocurrency with a promise of profit (\"" + lure +
                    "\"). No genuine investment guarantees a return; the \"platforms\" "
                    "strangers recommend show made-up gains and keep what is paid in.");
            }
        }
    }

    // Settings > Spam/scam warnings: a kind switched off is not reported.
    for (auto it = report.findings.begin(); it != report.findings.end();) {
        if (FindingEnabled(input.options, it->code)) { ++it; continue; }
        report.score -= it->score;
        it = report.findings.erase(it);
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
    in.options         = GetThreatScanOptions();

    std::string body;
    bool isHtml = false;
    if (UltraNet_MimeGetDisplayBody(msg, body, isHtml)) {
        in.body       = std::move(body);
        in.bodyIsHtml = isHtml;
    }

    // The attachments, and the body's own pictures (cid:) - with the image
    // attachments a picture's file name does not show.
    std::vector<UltraNetMimeAttachmentView> atts;
    UltraNet_MimeCollectAttachments(msg, atts, /*includeInline=*/true);
    for (const auto& a : atts) {
        const bool image = a.mediaType.rfind("image/", 0) == 0;
        if (!a.isInline) in.attachmentNames.push_back(a.filename);
        if (image && (a.isInline || !IsPictureName(a.filename)))
            in.pictureNames.push_back(a.filename);
    }
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
