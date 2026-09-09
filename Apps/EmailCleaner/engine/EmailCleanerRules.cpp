// Apps/EmailCleaner/engine/EmailCleanerRules.cpp
// The built-in keyword table and the plain-text rule file format.
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#include "EmailCleanerRules.h"

#include "EmailCleanerText.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace EmailCleaner {

namespace {

std::string Trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// A compact literal row for the built-in table.
struct BuiltInRow {
    MessageCategory category;
    const char*     phrase;
    double          weight;
    MatchField      field;
};

// Weights are on a "points" scale: the classifier treats ~4 points as a
// confident call and caps the visible score at 8 (see the classifier). A
// single strong term (3.0) plus one supporting term therefore flags a message,
// while one weak term (1.0) on its own does not.
const BuiltInRow kBuiltIn[] = {
    // ---- Product spam / unsolicited advertising ----------------------------
    { MessageCategory::ProductSpam, "*viagra*",              3.5, MatchField::Any },
    { MessageCategory::ProductSpam, "*cialis*",              3.5, MatchField::Any },
    { MessageCategory::ProductSpam, "*pharmac*",             2.0, MatchField::Any },
    { MessageCategory::ProductSpam, "canadian pharmacy",     3.5, MatchField::Any },
    { MessageCategory::ProductSpam, "no prescription",       3.0, MatchField::Any },
    { MessageCategory::ProductSpam, "cheap pills",           3.0, MatchField::Any },
    { MessageCategory::ProductSpam, "replica watches",       3.5, MatchField::Any },
    { MessageCategory::ProductSpam, "rolex",                 1.5, MatchField::Any },
    { MessageCategory::ProductSpam, "weight loss",           2.0, MatchField::Any },
    { MessageCategory::ProductSpam, "lose weight fast",      3.0, MatchField::Any },
    { MessageCategory::ProductSpam, "miracle cure",          3.0, MatchField::Any },
    { MessageCategory::ProductSpam, "male enhancement",      3.5, MatchField::Any },
    { MessageCategory::ProductSpam, "*casino*",              2.5, MatchField::Any },
    { MessageCategory::ProductSpam, "online gambling",       3.0, MatchField::Any },
    { MessageCategory::ProductSpam, "free spins",            2.5, MatchField::Any },
    { MessageCategory::ProductSpam, "act now",               1.5, MatchField::Any },
    { MessageCategory::ProductSpam, "limited time offer",    2.0, MatchField::Any },
    { MessageCategory::ProductSpam, "special offer",         1.5, MatchField::Any },
    { MessageCategory::ProductSpam, "risk free",             1.5, MatchField::Any },
    { MessageCategory::ProductSpam, "money back guarantee",  1.5, MatchField::Any },
    { MessageCategory::ProductSpam, "buy now",               1.5, MatchField::Subject },
    { MessageCategory::ProductSpam, "order now",             1.5, MatchField::Subject },
    { MessageCategory::ProductSpam, "click here to buy",     2.5, MatchField::Any },
    { MessageCategory::ProductSpam, "lowest price",          1.5, MatchField::Any },
    { MessageCategory::ProductSpam, "best price",            1.0, MatchField::Any },
    { MessageCategory::ProductSpam, "discount code",         1.0, MatchField::Any },
    { MessageCategory::ProductSpam, "free shipping",         1.0, MatchField::Any },
    { MessageCategory::ProductSpam, "*100% free*",           2.0, MatchField::Any },
    { MessageCategory::ProductSpam, "satisfaction guaranteed", 1.5, MatchField::Any },
    { MessageCategory::ProductSpam, "as seen on tv",         2.0, MatchField::Any },
    { MessageCategory::ProductSpam, "work from home",        2.0, MatchField::Any },
    { MessageCategory::ProductSpam, "make money fast",       3.0, MatchField::Any },
    { MessageCategory::ProductSpam, "earn extra cash",       2.5, MatchField::Any },
    { MessageCategory::ProductSpam, "cheap meds",            3.0, MatchField::Any },
    { MessageCategory::ProductSpam, "*cbd oil*",             2.0, MatchField::Any },
    { MessageCategory::ProductSpam, "*testosterone booster*", 3.0, MatchField::Any },

    // ---- Adult content -----------------------------------------------------
    { MessageCategory::AdultContent, "*porn*",               3.5, MatchField::Any },
    { MessageCategory::AdultContent, "xxx",                  2.5, MatchField::Any },
    { MessageCategory::AdultContent, "sex",                  2.0, MatchField::Any },
    { MessageCategory::AdultContent, "sex chat",             3.5, MatchField::Any },
    { MessageCategory::AdultContent, "sex tonight",          3.5, MatchField::Any },
    { MessageCategory::AdultContent, "adult content",        2.5, MatchField::Any },
    { MessageCategory::AdultContent, "adult videos",         3.0, MatchField::Any },
    { MessageCategory::AdultContent, "*webcam girl*",        3.5, MatchField::Any },
    { MessageCategory::AdultContent, "live cams",            3.0, MatchField::Any },
    { MessageCategory::AdultContent, "nude photos",          3.5, MatchField::Any },
    { MessageCategory::AdultContent, "*nudes*",              2.5, MatchField::Any },
    { MessageCategory::AdultContent, "naked",                1.5, MatchField::Any },
    { MessageCategory::AdultContent, "escort service",       3.0, MatchField::Any },
    { MessageCategory::AdultContent, "hot girls",            3.0, MatchField::Any },
    { MessageCategory::AdultContent, "erotic",               2.5, MatchField::Any },
    { MessageCategory::AdultContent, "*camgirl*",            3.5, MatchField::Any },
    { MessageCategory::AdultContent, "18+",                  1.5, MatchField::Subject },

    // ---- Dating / romance scams --------------------------------------------
    { MessageCategory::DatingScam, "singles in your area",   4.0, MatchField::Any },
    { MessageCategory::DatingScam, "lonely singles",         4.0, MatchField::Any },
    { MessageCategory::DatingScam, "meet single women",      4.0, MatchField::Any },
    { MessageCategory::DatingScam, "meet single men",        4.0, MatchField::Any },
    { MessageCategory::DatingScam, "find your soulmate",     3.0, MatchField::Any },
    { MessageCategory::DatingScam, "i saw your profile",     3.5, MatchField::Any },
    { MessageCategory::DatingScam, "someone likes you",      2.5, MatchField::Any },
    { MessageCategory::DatingScam, "new match waiting",      3.0, MatchField::Any },
    { MessageCategory::DatingScam, "russian bride",          4.0, MatchField::Any },
    { MessageCategory::DatingScam, "ukrainian bride",        4.0, MatchField::Any },
    { MessageCategory::DatingScam, "mail order bride",       4.0, MatchField::Any },
    { MessageCategory::DatingScam, "my dearest",             2.0, MatchField::Any },
    { MessageCategory::DatingScam, "my dear friend",         2.0, MatchField::Any },
    { MessageCategory::DatingScam, "i am looking for a serious relationship", 3.5, MatchField::Any },
    { MessageCategory::DatingScam, "waiting for your reply my love", 4.0, MatchField::Any },
    { MessageCategory::DatingScam, "*dating site*",          2.0, MatchField::Any },
    { MessageCategory::DatingScam, "hookup",                 3.0, MatchField::Any },
    { MessageCategory::DatingScam, "flirt with me",          3.0, MatchField::Any },
    { MessageCategory::DatingScam, "she wants to meet you",  3.5, MatchField::Any },

    // ---- Phishing ----------------------------------------------------------
    { MessageCategory::PhishingScam, "verify your account",  3.5, MatchField::Any },
    { MessageCategory::PhishingScam, "confirm your password", 3.5, MatchField::Any },
    { MessageCategory::PhishingScam, "your account has been suspended", 4.0, MatchField::Any },
    { MessageCategory::PhishingScam, "account will be closed", 3.5, MatchField::Any },
    { MessageCategory::PhishingScam, "unusual sign-in activity", 3.5, MatchField::Any },
    { MessageCategory::PhishingScam, "unusual login attempt", 3.5, MatchField::Any },
    { MessageCategory::PhishingScam, "update your billing",  3.0, MatchField::Any },
    { MessageCategory::PhishingScam, "click here to restore", 3.5, MatchField::Any },
    { MessageCategory::PhishingScam, "validate your identity", 3.0, MatchField::Any },
    { MessageCategory::PhishingScam, "your password expires", 3.0, MatchField::Any },
    { MessageCategory::PhishingScam, "security alert",       1.5, MatchField::Subject },
    { MessageCategory::PhishingScam, "immediate action required", 2.5, MatchField::Any },
    { MessageCategory::PhishingScam, "failed delivery attempt", 2.0, MatchField::Any },
    { MessageCategory::PhishingScam, "customs clearance fee", 3.0, MatchField::Any },
    { MessageCategory::PhishingScam, "reactivate your account", 3.5, MatchField::Any },

    // ---- Financial / advance-fee fraud -------------------------------------
    { MessageCategory::FinancialScam, "unclaimed inheritance", 4.0, MatchField::Any },
    { MessageCategory::FinancialScam, "next of kin",        3.0, MatchField::Any },
    { MessageCategory::FinancialScam, "beneficiary of the sum", 4.0, MatchField::Any },
    { MessageCategory::FinancialScam, "transfer of funds",  2.5, MatchField::Any },
    { MessageCategory::FinancialScam, "western union",      2.5, MatchField::Any },
    { MessageCategory::FinancialScam, "wire transfer fee",  3.0, MatchField::Any },
    { MessageCategory::FinancialScam, "million dollars",    3.0, MatchField::Any },
    { MessageCategory::FinancialScam, "lottery winner",     4.0, MatchField::Any },
    { MessageCategory::FinancialScam, "you have won",       2.5, MatchField::Any },
    { MessageCategory::FinancialScam, "claim your prize",   3.5, MatchField::Any },
    { MessageCategory::FinancialScam, "bitcoin investment", 3.0, MatchField::Any },
    { MessageCategory::FinancialScam, "crypto profit",      3.0, MatchField::Any },
    { MessageCategory::FinancialScam, "guaranteed returns", 3.0, MatchField::Any },
    { MessageCategory::FinancialScam, "double your money",  3.5, MatchField::Any },
    { MessageCategory::FinancialScam, "investment opportunity", 1.5, MatchField::Any },
    { MessageCategory::FinancialScam, "confidential business proposal", 3.5, MatchField::Any },
    { MessageCategory::FinancialScam, "urgent assistance needed", 2.5, MatchField::Any },
    { MessageCategory::FinancialScam, "bank account details", 2.5, MatchField::Any },

    // ---- Attachment-borne risk ---------------------------------------------
    // The classifier also flags risky extensions structurally; these catch the
    // social engineering that travels with them.
    { MessageCategory::MalwareRisk, "enable macros",        4.0, MatchField::Any },
    { MessageCategory::MalwareRisk, "enable editing to view", 4.0, MatchField::Any },
    { MessageCategory::MalwareRisk, "password for the archive", 3.0, MatchField::Any },
    { MessageCategory::MalwareRisk, "invoice attached",     1.5, MatchField::Any },
    { MessageCategory::MalwareRisk, "scanned document attached", 1.5, MatchField::Any },

    // ---- Newsletter / bulk (legitimate, but worth separating) --------------
    { MessageCategory::Newsletter, "unsubscribe",           2.0, MatchField::Body },
    { MessageCategory::Newsletter, "manage your preferences", 2.0, MatchField::Body },
    { MessageCategory::Newsletter, "view this email in your browser", 2.5, MatchField::Body },
    { MessageCategory::Newsletter, "you are receiving this email because", 2.5, MatchField::Body },
    { MessageCategory::Newsletter, "*newsletter*",          1.5, MatchField::Any },
    { MessageCategory::Newsletter, "*newsletter@*",         2.5, MatchField::Sender },
    { MessageCategory::Newsletter, "*news@*",               1.5, MatchField::Sender },

    // ---- Notification / transactional --------------------------------------
    { MessageCategory::Notification, "*no-reply@*",         3.0, MatchField::Sender },
    { MessageCategory::Notification, "*noreply@*",          3.0, MatchField::Sender },
    { MessageCategory::Notification, "*donotreply@*",       3.0, MatchField::Sender },
    { MessageCategory::Notification, "*do-not-reply@*",     3.0, MatchField::Sender },
    { MessageCategory::Notification, "do not reply to this", 2.0, MatchField::Body },
    { MessageCategory::Notification, "your order has shipped", 2.5, MatchField::Any },
    { MessageCategory::Notification, "order confirmation",  2.5, MatchField::Any },
    { MessageCategory::Notification, "payment received",    2.0, MatchField::Any },
    { MessageCategory::Notification, "your receipt",        2.0, MatchField::Any },
    { MessageCategory::Notification, "booking confirmation", 2.5, MatchField::Any },
};

} // namespace

void ParseRulePhrase(const std::string& phrase, std::string& outTerm,
                     bool& outOpenStart, bool& outOpenEnd) {
    std::string t = Trim(phrase);
    outOpenStart = false;
    outOpenEnd   = false;
    if (!t.empty() && t.front() == '*') { outOpenStart = true; t.erase(t.begin()); }
    if (!t.empty() && t.back()  == '*') { outOpenEnd   = true; t.pop_back(); }
    // Terms go through the same normalisation as message text, so a rule can
    // be written the way a human reads it ("no-reply@", "V1AGRA") and still
    // match folded text.
    outTerm = NormalizeForMatching(Trim(t));
}

// ---- RuleSet ---------------------------------------------------------------

RuleSet RuleSet::BuiltIn() {
    RuleSet set;
    set.rules_.reserve(sizeof(kBuiltIn) / sizeof(kBuiltIn[0]));
    for (const BuiltInRow& row : kBuiltIn)
        set.AddTerm(row.category, row.phrase, row.weight, row.field);
    return set;
}

void RuleSet::Add(const KeywordRule& rule) {
    if (rule.Valid()) rules_.push_back(rule);
}

void RuleSet::AddTerm(MessageCategory category, const std::string& phrase,
                      double weight, MatchField field) {
    KeywordRule rule;
    rule.category = category;
    rule.weight   = weight;
    rule.field    = field;
    ParseRulePhrase(phrase, rule.term, rule.openStart, rule.openEnd);
    Add(rule);
}

void RuleSet::Merge(const RuleSet& other) {
    rules_.insert(rules_.end(), other.rules_.begin(), other.rules_.end());
}

RuleSet RuleSet::Parse(const std::string& text, std::vector<std::string>* outErrors) {
    RuleSet set;
    std::istringstream in(text);
    std::string line;
    int lineNo = 0;

    while (std::getline(in, line)) {
        ++lineNo;
        // Strip a trailing comment, then the whole line if nothing is left.
        const size_t hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        line = Trim(line);
        if (line.empty()) continue;

        std::vector<std::string> fields;
        std::string current;
        for (char c : line) {
            if (c == '|') { fields.push_back(Trim(current)); current.clear(); }
            else          { current.push_back(c); }
        }
        fields.push_back(Trim(current));

        if (fields.size() < 2) {
            if (outErrors)
                outErrors->push_back("line " + std::to_string(lineNo) +
                                     ": expected 'category | weight | field | phrase'");
            continue;
        }

        KeywordRule rule;
        rule.category = CategoryFromString(fields[0]);
        if (rule.category == MessageCategory::Unclassified &&
            Lower(Trim(fields[0])) != "unclassified") {
            if (outErrors)
                outErrors->push_back("line " + std::to_string(lineNo) +
                                     ": unknown category '" + fields[0] + "'");
            continue;
        }

        // Accept "category | phrase", "category | weight | phrase" and the
        // full "category | weight | field | phrase".
        std::string phrase;
        if (fields.size() == 2) {
            rule.weight = 1.0;
            phrase = fields[1];
        } else if (fields.size() == 3) {
            rule.weight = std::atof(fields[1].c_str());
            phrase = fields[2];
        } else {
            rule.weight = std::atof(fields[1].c_str());
            rule.field  = MatchFieldFromString(fields[2]);
            phrase = fields[3];
        }

        if (rule.weight == 0.0) rule.weight = 1.0;
        ParseRulePhrase(phrase, rule.term, rule.openStart, rule.openEnd);
        if (rule.term.empty()) {
            if (outErrors)
                outErrors->push_back("line " + std::to_string(lineNo) + ": empty phrase");
            continue;
        }
        set.Add(rule);
    }
    return set;
}

std::string RuleSet::Serialize() const {
    std::ostringstream out;
    out << "# EmailCleaner keyword rules\n"
        << "# category | weight | field | phrase   ('*' = no word boundary)\n";
    for (const KeywordRule& rule : rules_) {
        out << ToString(rule.category) << " | " << rule.weight << " | "
            << ToString(rule.field) << " | "
            << (rule.openStart ? "*" : "") << rule.term << (rule.openEnd ? "*" : "")
            << "\n";
    }
    return out.str();
}

bool RuleSet::LoadFile(const std::string& path, std::vector<std::string>* outErrors) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream buffer;
    buffer << in.rdbuf();
    *this = Parse(buffer.str(), outErrors);
    return true;
}

bool RuleSet::SaveFile(const std::string& path) const {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << Serialize();
    return out.good();
}

} // namespace EmailCleaner
