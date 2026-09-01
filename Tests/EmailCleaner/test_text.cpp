// Tests/EmailCleaner/test_text.cpp
// The shared normalisation pipeline, tested directly — both message text and
// rule terms go through it, so its edge cases matter twice.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "EmailCleanerText.h"

using namespace EmailCleaner;

TEST(StripHtml_DropsScriptAndStyleContents) {
    const std::string out = StripHtml("<style>p{color:red}</style>hello"
                                      "<script>var x = 1;</script>world");
    REQUIRE(out.find("color") == std::string::npos);
    REQUIRE(out.find("var x") == std::string::npos);
    REQUIRE(out.find("hello") != std::string::npos);
    REQUIRE(out.find("world") != std::string::npos);
}

TEST(StripHtml_DecodesEntities) {
    REQUIRE_EQ(StripHtml("a&nbsp;b"), std::string("a b"));
    REQUIRE_EQ(StripHtml("Tom &amp; Jerry"), std::string("Tom & Jerry"));
    // An entity we do not know is left as written rather than mangled.
    REQUIRE_EQ(StripHtml("100&euro;"), std::string("100&euro;"));
}

TEST(StripHtml_SurvivesMalformedMarkup) {
    // An unterminated tag must not read past the end or throw.
    REQUIRE(StripHtml("text <b").find("text") != std::string::npos);
    REQUIRE(StripHtml("<").empty());
    REQUIRE(StripHtml("").empty());
    // An unterminated <script> drops the remainder rather than emitting it.
    REQUIRE(StripHtml("safe<script>alert(1)").find("alert") == std::string::npos);
}

TEST(CollapseObfuscation_NeedsALongEnoughRun) {
    REQUIRE_EQ(CollapseObfuscation("v.i.a.g.r.a"), std::string("viagra"));
    REQUIRE_EQ(CollapseObfuscation("s.e.x"), std::string("sex"));
    // Two letters is not a run: initials and hyphenated words are untouched.
    REQUIRE_EQ(CollapseObfuscation("e-mail"), std::string("e-mail"));
    REQUIRE_EQ(CollapseObfuscation("J.R. Tolkien"), std::string("J.R. Tolkien"));
    // Mixed separators do not chain together.
    REQUIRE_EQ(CollapseObfuscation("a.b-c"), std::string("a.b-c"));
}

TEST(CollapseObfuscation_SpacesNeedFiveLetters) {
    REQUIRE_EQ(CollapseObfuscation("v i a g r a"), std::string("viagra"));
    REQUIRE_EQ(CollapseObfuscation("a b c d"), std::string("a b c d"));
}

TEST(NormalizeForMatching_IsIdempotent) {
    // A term normalised twice must not drift, or a rule file that has been
    // saved and reloaded would stop matching.
    const char* samples[] = {
        "V1AGRA", "no-reply@shop.example", "<b>via</b>gra", "100% FREE",
        "Lonely   Singles", "18+", "e-mail me"
    };
    for (const char* sample : samples) {
        const std::string once = NormalizeForMatching(sample);
        REQUIRE_EQ(NormalizeForMatching(once), once);
    }
}

TEST(NormalizeForMatching_FoldsRuleTermsAndTextTheSameWay) {
    // This is the property the rule set depends on: a term written the human
    // way must equal the normalised form of the text it is meant to match.
    REQUIRE_EQ(NormalizeForMatching("no-reply@"), NormalizeForMatching("NO-REPLY@"));
    // The classifier matches a Sender rule against "<display name> <address>",
    // which is what ParseAddress produces — no angle brackets, because those
    // are markup to StripHtml (see the note in EmailCleanerText.h).
    const std::string term = NormalizeForMatching("no-reply@");
    const std::string text = NormalizeForMatching("Shop no-reply@shop.example");
    REQUIRE(text.find(term) != std::string::npos);

    const std::string leetTerm = NormalizeForMatching("viagra");
    REQUIRE(NormalizeForMatching("V1AGRA").find(leetTerm) != std::string::npos);
    REQUIRE(NormalizeForMatching("v.i.a.g.r.a").find(leetTerm) != std::string::npos);
}

TEST(NormalizeForMatching_TrimsAndCollapsesWhitespace) {
    REQUIRE_EQ(NormalizeForMatching("   spaced   out   "), std::string("spaced out"));
    REQUIRE_EQ(NormalizeForMatching(""), std::string(""));
    REQUIRE_EQ(NormalizeForMatching("\n\t "), std::string(""));
}
