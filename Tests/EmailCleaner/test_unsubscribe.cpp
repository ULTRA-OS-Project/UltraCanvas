// Tests/EmailCleaner/test_unsubscribe.cpp
// Reading List-Unsubscribe / List-Unsubscribe-Post, and the judgement about
// when taking the offer is a good idea.
// Version: 0.2.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "EmailCleanerUnsubscribe.h"

using namespace EmailCleaner;

// ---- Parsing ---------------------------------------------------------------

TEST(Unsubscribe_ParsesBothFormsFromOneHeader) {
    const UnsubscribeInfo info = ParseListUnsubscribe(
        "<mailto:bye@list.example>, <https://list.example/u?token=abc>");
    REQUIRE_EQ(info.mailto, std::string("bye@list.example"));
    REQUIRE_EQ(info.httpUrl, std::string("https://list.example/u?token=abc"));
    REQUIRE(info.Any());
}

TEST(Unsubscribe_ParsesMailtoSubject) {
    const UnsubscribeInfo info = ParseListUnsubscribe(
        "<mailto:leave@list.example?subject=unsubscribe-12345>");
    REQUIRE_EQ(info.mailto, std::string("leave@list.example"));
    REQUIRE_EQ(info.mailtoSubject, std::string("unsubscribe-12345"));
    REQUIRE(info.httpUrl.empty());
}

TEST(Unsubscribe_CommaInsideAMailtoQueryIsNotASeparator) {
    // A subject with a comma in it must not split the entry in half — the
    // angle brackets are the real delimiters.
    const UnsubscribeInfo info = ParseListUnsubscribe(
        "<mailto:x@y.example?subject=remove,please>, <https://y.example/u>");
    REQUIRE_EQ(info.mailto, std::string("x@y.example"));
    REQUIRE_EQ(info.mailtoSubject, std::string("remove,please"));
    REQUIRE_EQ(info.httpUrl, std::string("https://y.example/u"));
}

TEST(Unsubscribe_ToleratesMissingBracketsAndOddSpacing) {
    const UnsubscribeInfo bare = ParseListUnsubscribe("mailto:bye@list.example");
    REQUIRE_EQ(bare.mailto, std::string("bye@list.example"));

    const UnsubscribeInfo spaced = ParseListUnsubscribe(
        "  < mailto:Bye@List.Example >  ,  < https://list.example/u >  ");
    REQUIRE_EQ(spaced.mailto, std::string("bye@list.example"));   // lowercased
    REQUIRE_EQ(spaced.httpUrl, std::string("https://list.example/u"));
}

TEST(Unsubscribe_IgnoresUnknownSchemesAndEmptyHeaders) {
    const UnsubscribeInfo weird = ParseListUnsubscribe("<ftp://nope.example/u>");
    REQUIRE(!weird.Any());

    const UnsubscribeInfo empty = ParseListUnsubscribe("");
    REQUIRE(!empty.Any());
    REQUIRE(ChooseMethod(empty) == UnsubscribeMethod::None);
}

TEST(Unsubscribe_FirstMailtoWins) {
    const UnsubscribeInfo info = ParseListUnsubscribe(
        "<mailto:first@list.example>, <mailto:second@list.example>");
    REQUIRE_EQ(info.mailto, std::string("first@list.example"));
}

TEST(Unsubscribe_PostHeaderGrantsOneClick) {
    REQUIRE(ParseListUnsubscribePost("List-Unsubscribe=One-Click"));
    REQUIRE(ParseListUnsubscribePost("  list-unsubscribe=one-click  "));
    REQUIRE(!ParseListUnsubscribePost(""));
    REQUIRE(!ParseListUnsubscribePost("something-else=yes"));
}

// ---- Method choice ---------------------------------------------------------

TEST(Unsubscribe_OneClickPreferredWhenGranted) {
    UnsubscribeInfo info;
    info.httpUrl = "https://list.example/u";
    info.mailto  = "bye@list.example";
    info.oneClick = true;
    REQUIRE(ChooseMethod(info) == UnsubscribeMethod::OneClickPost);
}

TEST(Unsubscribe_OneClickWithoutAUrlFallsBackToMail) {
    // The Post header alone grants nothing — there has to be somewhere to post.
    UnsubscribeInfo info;
    info.mailto = "bye@list.example";
    info.oneClick = true;
    REQUIRE(ChooseMethod(info) == UnsubscribeMethod::MailTo);
}

TEST(Unsubscribe_MailtoBeatsABareLink) {
    // Both are on offer but only one can be completed without a person.
    UnsubscribeInfo info;
    info.mailto  = "bye@list.example";
    info.httpUrl = "https://list.example/u";
    REQUIRE(ChooseMethod(info) == UnsubscribeMethod::MailTo);
}

TEST(Unsubscribe_BareLinkIsTheLastResort) {
    UnsubscribeInfo info;
    info.httpUrl = "https://list.example/u";
    REQUIRE(ChooseMethod(info) == UnsubscribeMethod::HttpLink);
}

// ---- The judgement ---------------------------------------------------------

TEST(Unsubscribe_RecommendedForOptInBulkMail) {
    UnsubscribeInfo info;
    info.mailto = "bye@museum.example";
    REQUIRE(AdviseUnsubscribe(MessageCategory::Newsletter, info) ==
            UnsubscribeAdvice::Recommended);
    REQUIRE(AdviseUnsubscribe(MessageCategory::Notification, info) ==
            UnsubscribeAdvice::Recommended);
}

TEST(Unsubscribe_RefusedForEveryUnwantedFamily) {
    // This is the point of the module: unsubscribing from spam confirms the
    // address is live. A perfectly well-formed offer does not change that.
    UnsubscribeInfo perfect;
    perfect.httpUrl  = "https://spammer.example/u?id=12345";
    perfect.mailto   = "bye@spammer.example";
    perfect.oneClick = true;

    for (MessageCategory category : AllCategories()) {
        if (!IsUnwanted(category)) continue;
        REQUIRE(AdviseUnsubscribe(category, perfect) == UnsubscribeAdvice::RefuseSpam);
    }
}

TEST(Unsubscribe_PhishingWithAWorkingLinkIsStillRefused) {
    // The dangerous case: the better the offer looks, the more tempting the
    // wrong answer is.
    UnsubscribeInfo info;
    info.httpUrl  = "https://totally-legit.example/unsubscribe";
    info.oneClick = true;
    REQUIRE(AdviseUnsubscribe(MessageCategory::PhishingScam, info) ==
            UnsubscribeAdvice::RefuseSpam);

    const std::string text = DescribeAdvice(UnsubscribeAdvice::RefuseSpam,
                                            MessageCategory::PhishingScam);
    REQUIRE(text.find("confirms") != std::string::npos);
    REQUIRE(text.find("Block") != std::string::npos);
}

TEST(Unsubscribe_LinkOnlyNeedsAHuman) {
    UnsubscribeInfo info;
    info.httpUrl = "https://museum.example/u";
    REQUIRE(AdviseUnsubscribe(MessageCategory::Newsletter, info) ==
            UnsubscribeAdvice::NeedsBrowser);
}

TEST(Unsubscribe_NoOfferIsReportedAsSuch) {
    UnsubscribeInfo none;
    REQUIRE(AdviseUnsubscribe(MessageCategory::Newsletter, none) ==
            UnsubscribeAdvice::NoOffer);
    REQUIRE(AdviseUnsubscribe(MessageCategory::Personal, none) ==
            UnsubscribeAdvice::NoOffer);

    const std::string text = DescribeAdvice(UnsubscribeAdvice::NoOffer,
                                            MessageCategory::Newsletter);
    REQUIRE(text.find("no unsubscribe") != std::string::npos);
}

TEST(Unsubscribe_AdviceStringsRoundTrip) {
    REQUIRE_EQ(ToString(UnsubscribeMethod::OneClickPost), std::string("one-click"));
    REQUIRE_EQ(ToString(UnsubscribeMethod::MailTo), std::string("email"));
    REQUIRE_EQ(ToString(UnsubscribeMethod::HttpLink), std::string("link"));
    REQUIRE_EQ(ToString(UnsubscribeMethod::None), std::string("none"));
    REQUIRE_EQ(ToString(UnsubscribeAdvice::RefuseSpam), std::string("refuse-spam"));
}
