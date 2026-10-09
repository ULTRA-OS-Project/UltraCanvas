// Tests/UltraMail/test_messagefilter.cpp
// "Show emails ▸" filters of the message list.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "test_framework.h"

#include "UltraMailMessageFilter.h"

#include <string>

using namespace UltraMail;

namespace {
MessageEnvelope Mail(const std::string& from, const std::string& subject) {
    MessageEnvelope m;
    m.fromAddr = from;
    m.subject = subject;
    return m;
}
} // namespace

TEST(filter_all_and_same_sender) {
    MessageFacts none;
    REQUIRE(FilterMatches({}, Mail("a@x.example", "hi"), none));
    MessageFilter same{MessageFilterKind::SameSender, "Anna@X.example"};
    REQUIRE(FilterMatches(same, Mail("anna@x.example", "hi"), none));   // case-insensitive
    REQUIRE(!FilterMatches(same, Mail("bob@x.example", "hi"), none));
    REQUIRE(!FilterMatches({MessageFilterKind::SameSender, ""}, Mail("", "hi"), none));
    REQUIRE_EQ(Describe(same), std::string("From Anna@X.example"));
}

TEST(filter_by_facts) {
    MessageFacts f;
    const MessageEnvelope m = Mail("a@x.example", "hello");
    REQUIRE(!FilterMatches({MessageFilterKind::Unread, ""}, m, f));
    f.unread = true;
    REQUIRE(FilterMatches({MessageFilterKind::Unread, ""}, m, f));
    REQUIRE(!FilterMatches({MessageFilterKind::NeedsAnswer, ""}, m, f));
    f.needsAnswer = true;
    REQUIRE(FilterMatches({MessageFilterKind::NeedsAnswer, ""}, m, f));
    REQUIRE(!FilterMatches({MessageFilterKind::Spam, ""}, m, f));
    f.spam = true;
    REQUIRE(FilterMatches({MessageFilterKind::Spam, ""}, m, f));
}

TEST(filter_social_media_by_brand) {
    MessageFacts f;
    const MessageEnvelope m = Mail("notify@brand.example", "New comment");
    REQUIRE(!FilterMatches({MessageFilterKind::SocialMedia, ""}, m, f));
    f.brand = BrandCategory::Social;
    REQUIRE(FilterMatches({MessageFilterKind::SocialMedia, ""}, m, f));
    f.brand = BrandCategory::Messaging;
    REQUIRE(FilterMatches({MessageFilterKind::SocialMedia, ""}, m, f));
    f.brand = BrandCategory::Shopping;
    REQUIRE(!FilterMatches({MessageFilterKind::SocialMedia, ""}, m, f));
}

TEST(filter_payments_by_brand_or_subject) {
    MessageFacts f;
    const MessageFilter pay{MessageFilterKind::Payments, ""};
    REQUIRE(!FilterMatches(pay, Mail("a@shop.example", "Your newsletter"), f));
    REQUIRE(FilterMatches(pay, Mail("a@shop.example", "Your Invoice #1234"), f));
    REQUIRE(FilterMatches(pay, Mail("a@shop.example", "Ihre Rechnung vom 12.09."), f));
    REQUIRE(FilterMatches(pay, Mail("a@shop.example", "Votre facture"), f));
    REQUIRE(FilterMatches(pay, Mail("a@shop.example", "Receipt for your order"), f));
    f.brand = BrandCategory::Payment;   // PayPal, Stripe, ...
    REQUIRE(FilterMatches(pay, Mail("service@paypal.example", "Security notice"), f));
    f.brand = BrandCategory::Banking;   // a bank or broker
    REQUIRE(FilterMatches(pay, Mail("alerts@bank.example", "Security notice"), f));
    f.brand = BrandCategory::Crypto;    // an exchange or wallet
    REQUIRE(FilterMatches(pay, Mail("no-reply@exchange.example", "Security notice"), f));
    f.brand = BrandCategory::Gaming;
    REQUIRE(!FilterMatches(pay, Mail("no-reply@games.example", "Security notice"), f));
}

TEST(filter_social_media_without_the_registry) {
    MessageFacts none;   // no brand known
    const MessageFilter social{MessageFilterKind::SocialMedia, ""};
    // A social network's domain, or a subdomain of it.
    REQUIRE(FilterMatches(social, Mail("notification@facebookmail.com", "Hi"), none));
    REQUIRE(FilterMatches(social, Mail("no-reply@mail.instagram.com", "Hi"), none));
    REQUIRE(FilterMatches(social, Mail("messages-noreply@linkedin.com", "Hi"), none));
    // Not a lookalike that merely ends in the same letters.
    REQUIRE(!FilterMatches(social, Mail("sales@notx.com", "Offer"), none));
    // A self-hosted fediverse server.
    REQUIRE(FilterMatches(social, Mail("notifications@mastodon.example.org", "Hi"), none));
    REQUIRE(FilterMatches(social, Mail("noreply@social.example.net", "Hi"), none));
    // Notification wording from anywhere.
    REQUIRE(FilterMatches(social, Mail("a@club.example", "Anna commented on your photo"), none));
    REQUIRE(FilterMatches(social, Mail("a@club.example", "Max hat deinen Beitrag kommentiert"), none));
    REQUIRE(FilterMatches(social, Mail("a@club.example", "You have a new follower"), none));
    // Ordinary mail stays out.
    REQUIRE(!FilterMatches(social, Mail("anna@example.com", "Lunch tomorrow?"), none));
    REQUIRE(!LooksLikeSocialMedia("", ""));
}
