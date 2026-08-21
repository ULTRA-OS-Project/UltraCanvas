// Tests/UltraSocial/test_composer.cpp
// Compose-once/adapt-per-network: code-point counting, truncation, media
// trimming, caption limits, and validation.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "UltraSocialComposer.h"

#include <fstream>

using namespace UltraSocial;

namespace {

SocialCapabilities SmallCaps() {
    SocialCapabilities caps;
    caps.maxTextChars       = 20;
    caps.maxImages          = 2;
    caps.maxAltTextChars    = 10;
    caps.supportsVisibility = true;
    return caps;
}

} // namespace

TEST(composer_counts_code_points) {
    REQUIRE_EQ(CountTextPoints(""), 0);
    REQUIRE_EQ(CountTextPoints("hello"), 5);
    REQUIRE_EQ(CountTextPoints("h\xC3\xA9llo"), 5);           // é = 2 bytes
    REQUIRE_EQ(CountTextPoints("\xE2\x82\xAC\xE2\x82\xAC"), 2); // €€
    REQUIRE_EQ(CountTextPoints("\xF0\x9F\x98\x80"), 1);       // 😀 = 4 bytes
}

TEST(composer_truncates_at_word_boundary) {
    std::string t = TruncateToPoints("The quick brown fox jumps", 15);
    REQUIRE_EQ(t, std::string{"The quick\xE2\x80\xA6"});
    REQUIRE(CountTextPoints(t) <= 15);
    // Fits already -> unchanged.
    REQUIRE_EQ(TruncateToPoints("short", 15), std::string{"short"});
}

TEST(composer_truncates_multibyte_safely) {
    // 10 euro signs, limit 5 -> 4 kept + ellipsis, never a split sequence.
    std::string euros;
    for (int i = 0; i < 10; ++i) euros += "\xE2\x82\xAC";
    std::string t = TruncateToPoints(euros, 5);
    REQUIRE_EQ(CountTextPoints(t), 5);
    REQUIRE_EQ(t.substr(t.size() - 3), std::string{"\xE2\x80\xA6"});
}

TEST(composer_adapts_over_limit_draft) {
    PostDraft draft;
    draft.text = "This text is clearly longer than twenty code points";
    draft.media = {{"a.png", "", "alt"}, {"b.png", "", ""}, {"c.png", "", ""}};
    draft.visibility = "unlisted";

    std::vector<std::string> warnings;
    AdaptedPost post = AdaptDraft(draft, SocialNetwork::Mastodon, SmallCaps(),
                                  warnings);
    REQUIRE(CountTextPoints(post.text) <= 20);
    REQUIRE_EQ(post.media.size(), std::size_t{2});
    REQUIRE_EQ(post.visibility, std::string{"unlisted"});
    REQUIRE_EQ(warnings.size(), std::size_t{2});   // text + media trims
}

TEST(composer_adapt_clears_unsupported_visibility) {
    PostDraft draft;
    draft.text = "hi";
    SocialCapabilities caps;
    caps.maxTextChars = 100;
    std::vector<std::string> warnings;
    AdaptedPost post = AdaptDraft(draft, SocialNetwork::Bluesky, caps, warnings);
    REQUIRE(post.visibility.empty());
    REQUIRE(warnings.empty());
}

TEST(composer_caption_limit_applies_with_media) {
    SocialCapabilities caps;
    caps.maxTextChars         = 100;
    caps.maxMediaCaptionChars = 10;
    caps.maxImages            = 1;

    PostDraft draft;
    draft.text = "twelve chars!";           // 13 points, over the caption cap
    draft.media = {{"a.png", "", ""}};
    std::vector<std::string> warnings;
    AdaptedPost post = AdaptDraft(draft, SocialNetwork::Telegram, caps, warnings);
    REQUIRE(CountTextPoints(post.text) <= 10);
    REQUIRE_EQ(warnings.size(), std::size_t{1});

    // Without media the full text limit applies.
    draft.media.clear();
    post = AdaptDraft(draft, SocialNetwork::Telegram, caps, warnings);
    REQUIRE_EQ(post.text, draft.text);
}

TEST(composer_validation_finds_problems) {
    SocialCapabilities caps = SmallCaps();
    caps.maxImageBytes = 4;

    AdaptedPost post;
    post.text = "ok";
    REQUIRE(ValidateAdaptedPost(post, caps).empty());

    // Empty post.
    AdaptedPost empty;
    REQUIRE(!ValidateAdaptedPost(empty, caps).empty());

    // Missing media file.
    post.media = {{"/nonexistent/definitely-missing.png", "", ""}};
    REQUIRE(!ValidateAdaptedPost(post, caps).empty());

    // Oversized media file (5 bytes > 4-byte cap).
    std::string big = "tests-oversized-media.bin";
    { std::ofstream os(big, std::ios::binary); os << "12345"; }
    post.media = {{big, "", ""}};
    auto problems = ValidateAdaptedPost(post, caps);
    REQUIRE_EQ(problems.size(), std::size_t{1});
    std::remove(big.c_str());
}
