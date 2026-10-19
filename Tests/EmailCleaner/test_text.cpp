// Tests/EmailCleaner/test_text.cpp
// The shared normalisation pipeline, tested directly — both message text and
// rule terms go through it, so its edge cases matter twice.
// Version: 0.2.0 - HTML through HTML::ExtractPlainText (no StripHtml of its own)
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "EmailCleanerText.h"

using namespace EmailCleaner;

// HTML goes through the HTMLReader module (HTML::ExtractPlainText) inside
// NormalizeForMatching; these pin what the matcher sees of it.
TEST(NormalizeForMatching_DropsScriptAndStyleContents) {
    REQUIRE_EQ(NormalizeForMatching("<style>p{color:red}</style>hello"
                                    "<script>var x = 1;</script>world"),
               std::string("hello world"));
}

TEST(NormalizeForMatching_InlineMarkupKeepsAWordWhole) {
    // Camouflage: formatting splits the word, a block does not hide it.
    REQUIRE_EQ(NormalizeForMatching("<b>via</b>gra"), std::string("viagra"));
    REQUIRE_EQ(NormalizeForMatching("<span>vi</span><i>ag</i>ra"), std::string("viagra"));
    REQUIRE_EQ(NormalizeForMatching("<p>free</p><p>money</p>"), std::string("free money"));
}

TEST(NormalizeForMatching_DecodesEntities) {
    REQUIRE_EQ(NormalizeForMatching("<p>a&nbsp;b</p>"), std::string("a b"));
    REQUIRE_EQ(NormalizeForMatching("<p>Tom &amp; Jerry</p>"), std::string("tom & jerry"));
    REQUIRE_EQ(NormalizeForMatching("<p>d&eacute;j&agrave; &#8364;</p>"),
               std::string("d\xC3\xA9j\xC3\xA0 \xE2\x82\xAC"));
    // An entity nobody defines is left as written rather than mangled.
    REQUIRE_EQ(NormalizeForMatching("<p>x&zzz;</p>"), std::string("x&zzz;"));
    // Dashes and the ellipsis read as their ASCII spelling, so a rule written
    // with them matches either way.
    REQUIRE_EQ(NormalizeForMatching("<p>act now &mdash; today&hellip;</p>"),
               NormalizeForMatching("act now - today..."));
}

TEST(NormalizeForMatching_SurvivesMalformedMarkup) {
    // An unterminated tag must not read past the end or throw.
    REQUIRE(NormalizeForMatching("text <b").find("text") != std::string::npos);
    REQUIRE(NormalizeForMatching("<").empty());
    REQUIRE(NormalizeForMatching("").empty());
    // An unterminated <script> drops the remainder rather than emitting it.
    REQUIRE(NormalizeForMatching("safe<script>alert(1)").find("alert") == std::string::npos);
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
    // are markup to the HTML reader (see the note in EmailCleanerText.h).
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
