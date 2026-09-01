// Apps/EmailCleaner/engine/EmailCleanerRules.h
// The keyword rule set behind EmailCleaner's content detection: which terms
// point at which category, how strongly, and in which part of a message they
// count. The built-in table covers the families the user asked for — product
// advertising, adult content, dating/romance scams — plus phishing, financial
// fraud and malware-bearing attachments.
//
// Rules are data, not code: a rule set round-trips through a plain-text file
// so a user can add their own terms without a rebuild. Parsing and
// serialisation are pure and unit-tested.
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "EmailCleanerTypes.h"

#include <string>
#include <vector>

namespace EmailCleaner {

// One term to look for. `term` is stored already normalised (lowercase, no
// leet spelling) so it can be compared against normalised message text.
//
// Boundaries: by default a term only counts as a whole word, so "sex" does not
// fire inside "Essex". A '*' at either end of the phrase in the file format
// drops the boundary on that side ("*pharmac*" matches "pharmacies").
struct KeywordRule {
    MessageCategory category = MessageCategory::ProductSpam;
    std::string     term;
    double          weight    = 1.0;
    MatchField      field     = MatchField::Any;
    bool            openStart = false;   // no word boundary required before
    bool            openEnd   = false;   // no word boundary required after

    bool Valid() const { return !term.empty() && weight != 0.0; }
};

class RuleSet {
public:
    RuleSet() = default;

    // The shipped default rules.
    static RuleSet BuiltIn();

    void Add(const KeywordRule& rule);
    // Convenience for building a set in code / tests. `phrase` may carry the
    // '*' boundary markers.
    void AddTerm(MessageCategory category, const std::string& phrase,
                 double weight, MatchField field = MatchField::Any);

    const std::vector<KeywordRule>& Rules() const { return rules_; }
    std::size_t Size()  const { return rules_.size(); }
    bool        Empty() const { return rules_.empty(); }
    void        Clear() { rules_.clear(); }

    // Append another set's rules (later rules simply add weight, so a user
    // file layered over the built-ins reinforces rather than replaces).
    void Merge(const RuleSet& other);

    // ---- Text format -------------------------------------------------------
    // One rule per line:
    //
    //     category | weight | field | phrase
    //
    // '#' starts a comment; blank lines are ignored; `field` may be omitted
    // (defaults to "any") and so may `weight` (defaults to 1.0). Unparsable
    // lines are collected in `outErrors` (when given) and skipped, so one bad
    // user line never costs the whole file.
    static RuleSet Parse(const std::string& text,
                         std::vector<std::string>* outErrors = nullptr);
    std::string Serialize() const;

    bool LoadFile(const std::string& path, std::vector<std::string>* outErrors = nullptr);
    bool SaveFile(const std::string& path) const;

private:
    std::vector<KeywordRule> rules_;
};

// Turn a phrase from the file format into a rule term plus its boundary flags
// (exposed for tests): "*pharmac*" -> ("pharmac", openStart, openEnd).
void ParseRulePhrase(const std::string& phrase, std::string& outTerm,
                     bool& outOpenStart, bool& outOpenEnd);

} // namespace EmailCleaner
