// Tests/EmailCleaner/test_rules.cpp
// The keyword rule set: the built-in table, the text format and boundaries.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "EmailCleanerRules.h"
#include "EmailCleanerText.h"

#include <map>

using namespace EmailCleaner;

TEST(BuiltInRules_CoverEveryTargetedFamily) {
    const RuleSet rules = RuleSet::BuiltIn();
    REQUIRE(rules.Size() > 100);

    std::map<MessageCategory, int> perCategory;
    for (const KeywordRule& rule : rules.Rules()) perCategory[rule.category] += 1;

    // The families the app exists to detect must all be represented.
    REQUIRE(perCategory[MessageCategory::ProductSpam] > 10);
    REQUIRE(perCategory[MessageCategory::AdultContent] > 10);
    REQUIRE(perCategory[MessageCategory::DatingScam] > 10);
    REQUIRE(perCategory[MessageCategory::PhishingScam] > 5);
    REQUIRE(perCategory[MessageCategory::FinancialScam] > 5);
    REQUIRE(perCategory[MessageCategory::MalwareRisk] > 0);
    REQUIRE(perCategory[MessageCategory::Newsletter] > 0);
    REQUIRE(perCategory[MessageCategory::Notification] > 0);
}

TEST(BuiltInRules_AreNormalisedAndWeighted) {
    // Bind the set to a named value: Rules() returns a reference into it, and
    // iterating a temporary's reference would dangle.
    const RuleSet builtIn = RuleSet::BuiltIn();
    for (const KeywordRule& rule : builtIn.Rules()) {
        REQUIRE(!rule.term.empty());
        REQUIRE(rule.weight > 0.0);
        // Terms are stored lowercase so they can match normalised text.
        for (char c : rule.term)
            REQUIRE(!(c >= 'A' && c <= 'Z'));
        // The '*' markers become flags, they never survive in the term.
        REQUIRE(rule.term.find('*') == std::string::npos);
    }
}

TEST(ParseRulePhrase_ExtractsBoundaryMarkers) {
    std::string term;
    bool openStart = false, openEnd = false;

    ParseRulePhrase("*pharmac*", term, openStart, openEnd);
    REQUIRE_EQ(term, std::string("pharmac"));
    REQUIRE(openStart);
    REQUIRE(openEnd);

    ParseRulePhrase("  Buy Now  ", term, openStart, openEnd);
    REQUIRE_EQ(term, std::string("buy now"));
    REQUIRE(!openStart);
    REQUIRE(!openEnd);

    // A term is stored already normalised, so it can be compared against
    // normalised message text: '@' folds to 'a' on both sides.
    ParseRulePhrase("newsletter@*", term, openStart, openEnd);
    REQUIRE_EQ(term, NormalizeForMatching("newsletter@"));
    REQUIRE(!openStart);
    REQUIRE(openEnd);

    // And the leet spellings spam actually uses fold too.
    ParseRulePhrase("V1AGRA", term, openStart, openEnd);
    REQUIRE_EQ(term, std::string("viagra"));
}

TEST(ParseRuleFile_FullAndShortForms) {
    const std::string text =
        "# a comment line\n"
        "\n"
        "product-spam | 2.5 | subject | Half Price\n"
        "adult | 3 | any | *webcam*\n"
        "dating-scam | 1.5 | lonely hearts\n"      // no field column
        "phishing | verify your card\n"            // no weight, no field
        "  \n";

    std::vector<std::string> errors;
    const RuleSet rules = RuleSet::Parse(text, &errors);
    REQUIRE(errors.empty());
    REQUIRE_EQ(rules.Size(), static_cast<std::size_t>(4));

    const KeywordRule& first = rules.Rules()[0];
    REQUIRE(first.category == MessageCategory::ProductSpam);
    REQUIRE_EQ(first.weight, 2.5);
    REQUIRE(first.field == MatchField::Subject);
    REQUIRE_EQ(first.term, std::string("half price"));

    const KeywordRule& second = rules.Rules()[1];
    REQUIRE(second.openStart);
    REQUIRE(second.openEnd);

    const KeywordRule& third = rules.Rules()[2];
    REQUIRE(third.field == MatchField::Any);
    REQUIRE_EQ(third.weight, 1.5);
    REQUIRE_EQ(third.term, std::string("lonely hearts"));

    const KeywordRule& fourth = rules.Rules()[3];
    REQUIRE_EQ(fourth.weight, 1.0);
    REQUIRE_EQ(fourth.term, std::string("verify your card"));
}

TEST(ParseRuleFile_BadLinesAreReportedNotFatal) {
    const std::string text =
        "product-spam | 2 | any | cheap pills\n"
        "not-a-category | 2 | any | whatever\n"
        "adult\n"                                  // too few columns
        "phishing | 2 | any |   \n"                // empty phrase
        "adult | 3 | any | xxx\n";

    std::vector<std::string> errors;
    const RuleSet rules = RuleSet::Parse(text, &errors);
    REQUIRE_EQ(rules.Size(), static_cast<std::size_t>(2));
    REQUIRE_EQ(errors.size(), static_cast<std::size_t>(3));
}

TEST(RuleSet_SerializeRoundTrips) {
    RuleSet original;
    original.AddTerm(MessageCategory::DatingScam, "*lonely singles*", 4.0, MatchField::Body);
    original.AddTerm(MessageCategory::ProductSpam, "buy now", 1.5, MatchField::Subject);

    const RuleSet reparsed = RuleSet::Parse(original.Serialize());
    REQUIRE_EQ(reparsed.Size(), original.Size());
    for (std::size_t i = 0; i < original.Size(); ++i) {
        const KeywordRule& a = original.Rules()[i];
        const KeywordRule& b = reparsed.Rules()[i];
        REQUIRE(a.category == b.category);
        REQUIRE(a.field == b.field);
        REQUIRE_EQ(a.term, b.term);
        REQUIRE_EQ(a.weight, b.weight);
        REQUIRE(a.openStart == b.openStart);
        REQUIRE(a.openEnd == b.openEnd);
    }
}

TEST(RuleSet_MergeLayersUserRulesOnTop) {
    RuleSet base;
    base.AddTerm(MessageCategory::ProductSpam, "cheap pills", 2.0);
    RuleSet user;
    user.AddTerm(MessageCategory::ProductSpam, "special deal", 1.0);

    base.Merge(user);
    REQUIRE_EQ(base.Size(), static_cast<std::size_t>(2));
    REQUIRE_EQ(base.Rules()[1].term, std::string("special deal"));
}

TEST(RuleSet_RejectsEmptyOrZeroWeightRules) {
    RuleSet set;
    KeywordRule empty;
    empty.term = "";
    set.Add(empty);

    KeywordRule zero;
    zero.term = "something";
    zero.weight = 0.0;
    set.Add(zero);

    REQUIRE(set.Empty());
}
