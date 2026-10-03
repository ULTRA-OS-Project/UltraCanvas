// Tests/UltraMail/test_threatscan.cpp
// Exercises the content scan: link extraction out of HTML and plain text, and
// each rule that can raise a message to Suspicious or Scam — a link whose text
// names one site while its href goes to another, a brand claimed by a sender
// that does not own the domain, userinfo hiding the real host, an IP-literal
// target, an executable attachment — plus the equally important negative
// cases, where an ordinary newsletter and an ordinary personal mail stay out
// of the way.
// Version: 0.4.0 - banks and exchanges claimed from elsewhere; ordinary words
//                  and mailbox addresses are not claims
// Version: 0.3.0 - PlainLinkAt
// Version: 0.2.0 - borrowed brand pictures (a fake "It's a Match!"), image hosts
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "UltraMailThreatScan.h"

#include <string>

using namespace UltraMail;

namespace {

bool HasFinding(const ThreatReport& r, const std::string& code) {
    for (const auto& f : r.findings) if (f.code == code) return true;
    return false;
}

ScanInput Html(const std::string& from, const std::string& body) {
    ScanInput in;
    in.fromAddr   = from;
    in.body       = body;
    in.bodyIsHtml = true;
    return in;
}

} // namespace

// ---------------------------------------------------------------------------
// Link extraction
// ---------------------------------------------------------------------------
TEST(extracts_anchor_targets_and_their_text) {
    const std::string html =
        "<p>Hello</p><a href=\"https://example.com/a\">Click <b>here</b></a>"
        "<a href='http://other.test/b'>other.test</a>"
        "<form action=\"https://forms.test/post\"><input></form>";
    const auto links = ExtractLinks(html, true);
    REQUIRE_EQ(links.size(), (size_t)3);
    REQUIRE_EQ(links[0].host, std::string("example.com"));
    REQUIRE_EQ(links[0].text, std::string("Click  here"));
    REQUIRE_EQ(links[1].host, std::string("other.test"));
    REQUIRE_EQ(links[2].host, std::string("forms.test"));
}

TEST(extracts_bare_urls_from_plain_text) {
    const auto links = ExtractLinks(
        "See https://example.com/page, or www.other.test for more.", false);
    REQUIRE_EQ(links.size(), (size_t)2);
    REQUIRE_EQ(links[0].host, std::string("example.com"));   // trailing comma dropped
    REQUIRE_EQ(links[1].host, std::string("www.other.test"));
}

TEST(decodes_entities_in_hrefs) {
    const auto links = ExtractLinks(
        "<a href=\"https://example.com/p?a=1&amp;b=2\">x</a>", true);
    REQUIRE_EQ(links.size(), (size_t)1);
    REQUIRE(links[0].href.find("&b=2") != std::string::npos);
}

// ---------------------------------------------------------------------------
// The rules that mean "scam"
// ---------------------------------------------------------------------------
TEST(a_link_that_lies_about_its_target_is_a_scam) {
    ScanInput in = Html("service@secure-billing.example",
        "<a href=\"http://203.0.113.9/login\">www.paypal.com</a>");
    const ThreatReport r = ScanMessage(in);
    REQUIRE(HasFinding(r, "link-target-mismatch"));
    REQUIRE(r.level == ThreatLevel::Scam);
    REQUIRE(!r.Summary().empty());
}

TEST(a_sender_claiming_a_brand_it_does_not_own_is_flagged) {
    ScanInput in = Html("security-alert@random-mailer.example",
        "<a href=\"https://random-mailer.example/verify\">Verify now</a>");
    in.fromName = "Apple ID Support";
    const ThreatReport r = ScanMessage(in);
    REQUIRE(HasFinding(r, "brand-impersonation"));
    REQUIRE(r.level == ThreatLevel::Suspicious || r.level == ThreatLevel::Scam);
}

TEST(a_brand_claimed_from_a_free_mailbox_is_a_scam) {
    ScanInput in = Html("paypal.security.team@gmail.com",
        "<a href=\"https://pay-verify.example/login\">Restore access</a>");
    in.fromName = "PayPal Security";
    const ThreatReport r = ScanMessage(in);
    REQUIRE(HasFinding(r, "brand-impersonation"));
    REQUIRE(r.level == ThreatLevel::Scam);
}

TEST(a_bank_or_exchange_claimed_from_elsewhere_is_flagged) {
    ScanInput in = Html("security@coinbase-verify.example",
        "<a href=\"https://coinbase-verify.example/restore\">Restore access</a>");
    in.fromName = "Coinbase Security";
    REQUIRE(HasFinding(ScanMessage(in), "brand-impersonation"));

    ScanInput bank = Html("alerts@secure-mail.example",
        "<a href=\"https://secure-mail.example/x\">Review activity</a>");
    bank.fromName = "Chase Bank Alerts";
    REQUIRE(HasFinding(ScanMessage(bank), "brand-impersonation"));
}

TEST(an_ordinary_word_or_mailbox_address_is_not_impersonation) {
    // A hotel's "booking" and a friend whose display name is their own
    // Outlook address are not claims to be Booking.com or Microsoft.
    ScanInput hotel = Html("reservations@hotel-am-see.example",
        "<a href=\"https://hotel-am-see.example/\">Your stay</a>");
    hotel.subject = "Your booking is confirmed";
    REQUIRE(!HasFinding(ScanMessage(hotel), "brand-impersonation"));

    ScanInput friendMail;
    friendMail.fromAddr = "jane@outlook.com";
    friendMail.fromName = "jane@outlook.com";
    friendMail.subject  = "Weekend";
    friendMail.body     = "See you on Saturday!";
    REQUIRE(!HasFinding(ScanMessage(friendMail), "brand-impersonation"));
}

TEST(userinfo_hiding_the_real_host_is_caught) {
    const ThreatReport r = ScanMessage(Html("x@example.test",
        "<a href=\"http://paypal.com@203.0.113.9/signin\">Sign in</a>"));
    REQUIRE(HasFinding(r, "link-userinfo"));
    REQUIRE(r.level == ThreatLevel::Scam);
}

TEST(a_brand_name_worn_in_front_of_a_foreign_domain_is_caught) {
    const ThreatReport r = ScanMessage(Html("noreply@delivery-update.example",
        "<a href=\"https://apple-id-verify.delivery-update.example/x\">Continue</a>"));
    REQUIRE(HasFinding(r, "link-brand-lookalike"));
}

TEST(an_executable_attachment_is_called_what_it_is) {
    // A program dressed as an invoice is the attack itself.
    ScanInput disguised = Html("someone@example.test", "<p>Invoice attached</p>");
    disguised.attachmentNames = { "Invoice_2026.pdf.exe" };
    const ThreatReport r = ScanMessage(disguised);
    REQUIRE(HasFinding(r, "attachment-disguised-executable"));
    REQUIRE(r.level == ThreatLevel::Scam);

    // A program sent as a program is worth a second look, not an accusation —
    // colleagues do send the odd installer.
    ScanInput plain = Html("someone@example.test", "<p>Here is the build</p>");
    plain.attachmentNames = { "setup.exe" };
    const ThreatReport p = ScanMessage(plain);
    REQUIRE(HasFinding(p, "attachment-executable"));
    REQUIRE(p.level == ThreatLevel::Suspicious);
}

TEST(credential_language_plus_an_off_domain_link_is_suspicious) {
    ScanInput in = Html("billing@shop.example",
        "<p>Your account will be suspended unless you act.</p>"
        "<a href=\"https://unrelated-host.example/act\">Update your details</a>");
    const ThreatReport r = ScanMessage(in);
    REQUIRE(HasFinding(r, "credential-request"));
    REQUIRE(r.Suspicious());
}

TEST(header_signals_are_read) {
    ScanInput in = Html("someone@example.test", "<p>hello</p>");
    in.spamFlag = "YES";
    REQUIRE(ScanMessage(in).Suspicious());

    ScanInput auth = Html("someone@example.test", "<p>hello</p>");
    auth.authResults = "mx.test; spf=fail smtp.mailfrom=example.test; dmarc=fail";
    REQUIRE(HasFinding(ScanMessage(auth), "auth-failure"));

    ScanInput reply = Html("noreply@shop.example", "<p>hello</p>");
    reply.replyTo = "collector@elsewhere.example";
    REQUIRE(HasFinding(ScanMessage(reply), "reply-to-mismatch"));
}

// ---------------------------------------------------------------------------
// The rules that mean "advertisement" — and the cases that must stay quiet
// ---------------------------------------------------------------------------
TEST(bulk_markers_make_an_advertisement_not_a_threat) {
    ScanInput in = Html("news@shop.example",
        "<a href=\"https://shop.example/offer\">See the offer</a>");
    in.listUnsubscribe = "<https://shop.example/unsubscribe>";
    const ThreatReport r = ScanMessage(in);
    REQUIRE(r.bulk);
    REQUIRE(r.level == ThreatLevel::Advertisement);
    REQUIRE(r.findings.empty());
}

TEST(an_ordinary_personal_message_raises_nothing) {
    ScanInput in;
    in.fromAddr = "anna@example.com";
    in.fromName = "Anna Schmidt";
    in.subject  = "Lunch on Thursday?";
    in.body     = "Hi! Are you free on Thursday? See https://example.com/menu";
    const ThreatReport r = ScanMessage(in);
    REQUIRE(r.level == ThreatLevel::Clean);
    REQUIRE(r.findings.empty());
}

TEST(a_genuine_brand_newsletter_is_not_impersonation) {
    ScanInput in = Html("notification@facebookmail.com",
        "<a href=\"https://www.facebook.com/n/x\">Facebook</a>");
    in.fromName        = "Facebook";
    in.subject         = "You have 3 notifications";
    in.listUnsubscribe = "<https://facebook.com/unsubscribe>";
    const ThreatReport r = ScanMessage(in);
    REQUIRE(!HasFinding(r, "brand-impersonation"));
    REQUIRE(!HasFinding(r, "link-brand-mismatch"));
    REQUIRE(r.level == ThreatLevel::Advertisement);
}

TEST(a_tracking_link_on_the_senders_own_domain_is_not_a_mismatch) {
    const ThreatReport r = ScanMessage(Html("news@shop.example",
        "<a href=\"https://click.shop.example/r/123\">shop.example</a>"));
    REQUIRE(!HasFinding(r, "link-target-mismatch"));
    REQUIRE(r.level == ThreatLevel::Clean);
}

// ---------------------------------------------------------------------------
// Raw messages and levels
// ---------------------------------------------------------------------------
TEST(scanning_a_raw_message_reads_headers_and_body) {
    const std::string raw =
        "From: \"PayPal Service\" <service@paypa1-secure.example>\r\n"
        "To: erika@example.com\r\n"
        "Subject: Your account has been locked\r\n"
        "Reply-To: collector@elsewhere.example\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "\r\n"
        "<html><body><p>Your account has been suspended.</p>"
        "<a href=\"http://198.51.100.7/login\">www.paypal.com</a></body></html>\r\n";
    const ThreatReport r = ScanRawMessage(raw);
    REQUIRE(r.level == ThreatLevel::Scam);
    REQUIRE(HasFinding(r, "link-target-mismatch"));
    REQUIRE(HasFinding(r, "reply-to-mismatch"));
    REQUIRE(r.Summary().find("paypal.com") != std::string::npos);
}

TEST(an_empty_or_unparseable_message_is_unscanned_not_clean) {
    REQUIRE(ScanRawMessage("").level == ThreatLevel::Unscanned);
}

TEST(threat_level_names_round_trip) {
    for (ThreatLevel level : { ThreatLevel::Unscanned, ThreatLevel::Clean,
                               ThreatLevel::Advertisement, ThreatLevel::Suspicious,
                               ThreatLevel::Scam })
        REQUIRE(ThreatLevelFromString(ToString(level)) == level);
    REQUIRE(ThreatLevelFromString("nonsense") == ThreatLevel::Unscanned);
}

// ---------------------------------------------------------------------------
// Sender domain vs. button domain (the reading pane's phishing warning)
// ---------------------------------------------------------------------------
TEST(domain_mismatch_names_the_sender_and_the_button_domain) {
    const auto m = FindDomainMismatch(Html("info@riscoscloverleaf.com",
        "<p>Your mailbox has a temporary restriction.</p>"
        "<a href=\"https://track.mailer.example/u\">unsubscribe</a>"
        "<a href=\"https://secure-mail.verify-now.top/login\">Confirm Now</a>"));
    REQUIRE(m.found);
    REQUIRE(m.isButton);
    REQUIRE_EQ(m.senderDomain, std::string("riscoscloverleaf.com"));
    REQUIRE_EQ(m.linkDomain, std::string("secure-mail.verify-now.top"));
    REQUIRE_EQ(m.linkText, std::string("Confirm Now"));
}

TEST(domain_mismatch_is_not_found_when_links_stay_on_the_senders_domain) {
    const auto m = FindDomainMismatch(Html("news@shop.example.com",
        "<a href=\"https://www.example.com/offer\">See the offer</a>"
        "<a href=\"https://example.com/unsubscribe\">unsubscribe</a>"));
    REQUIRE(!m.found);
}

TEST(domain_mismatch_falls_back_to_a_bare_link) {
    ScanInput in;
    in.fromAddr = "billing@example.com";
    in.body     = "Pay here: https://pay.elsewhere.test/x";
    const auto m = FindDomainMismatch(in);
    REQUIRE(m.found);
    REQUIRE(!m.isButton);
    REQUIRE_EQ(m.linkDomain, std::string("pay.elsewhere.test"));
}

// ---------------------------------------------------------------------------
// Advance-fee fraud ("Nigeria connection" / 419)
// ---------------------------------------------------------------------------
TEST(advance_fee_story_is_a_scam) {
    ScanInput in;
    in.fromAddr = "barrister.james@mailbox.example";
    in.subject  = "Urgent: next of kin";
    in.body     = "Dear Friend, my late client died in a car accident and left "
                  "US$ 15,500,000.00 in a dormant account. You will receive 40% as "
                  "my partner; you only need to pay the taxes and the clearance fee.";
    const ThreatReport r = ScanMessage(in);
    REQUIRE(HasFinding(r, "advance-fee-fraud"));
    REQUIRE(r.level == ThreatLevel::Scam);
}

TEST(advance_fee_in_words_millions_is_recognised) {
    ScanInput in;
    in.fromAddr = "someone@example.test";
    in.body     = "The inheritance of 10.5 million united states dollars awaits you.";
    REQUIRE(HasFinding(ScanMessage(in), "advance-fee-fraud"));
}

TEST(a_large_number_without_the_story_is_not_advance_fee) {
    ScanInput in;
    in.fromAddr = "news@example.test";
    in.body     = "The city announced a budget of $12,000,000 for new schools.";
    REQUIRE(!HasFinding(ScanMessage(in), "advance-fee-fraud"));
    ScanInput story;
    story.fromAddr = "aunt@example.test";
    story.body     = "Grandpa passed away last week; the funeral is on Friday.";
    REQUIRE(!HasFinding(ScanMessage(story), "advance-fee-fraud"));
}

// ---------------------------------------------------------------------------
// A brand's own pictures over links somewhere else (a fake "It's a Match!")
// ---------------------------------------------------------------------------
namespace {
// The shape of the real phishing mail: Tinder's name and its pictures from
// gotinder.com, sent from an unrelated domain, every link to a third one.
ScanInput FakeTinderMatch() {
    ScanInput in;
    in.fromName = "Tinder";
    in.fromAddr = "cmcgavnn@amega.com";
    in.subject  = "It's a Match!";
    in.body =
        "<table><tr><td><img src=\"https://marketing-images.gotinder.com/3d7c/0.jpg\" "
        "width=\"39\" height=\"45\" alt=\"Tinder Logo\"></td></tr>"
        "<tr><td><div>Someone matched with you on Tinder!</div></td></tr>"
        "<tr><td><a href=\"http://vakantiehuiseichenbach.nl/splashedlb.php?utm_source=x\">"
        "<span>FIND OUT WHO</span></a></td></tr>"
        "<tr><td><a href=\"http://vakantiehuiseichenbach.nl/splashedlb.php?utm_source=x\">"
        "<img src=\"https://marketing-images.gotinder.com/f91f/0.png\" alt=\"Tinder Logo\"></a>"
        " This email was sent by Tinder. <a href=\"http://vakantiehuiseichenbach.nl/x\">"
        "Privacy Policy</a></td></tr></table>";
    in.bodyIsHtml = true;
    return in;
}
} // namespace

TEST(a_fake_match_mail_with_borrowed_pictures_is_a_scam) {
    const ThreatReport r = ScanMessage(FakeTinderMatch());
    REQUIRE(HasFinding(r, "brand-impersonation"));
    REQUIRE(HasFinding(r, "borrowed-brand-pictures"));
    REQUIRE(r.level == ThreatLevel::Scam);
}

TEST(borrowed_pictures_need_no_brand_table) {
    ScanInput in = FakeTinderMatch();
    in.fromName = "Glimmerdate";                // a name no table knows
    in.subject  = "New message on Glimmerdate";
    for (std::string::size_type p; (p = in.body.find("gotinder")) != std::string::npos;)
        in.body.replace(p, 8, "glimmerdate");
    const ThreatReport r = ScanMessage(in);
    REQUIRE(HasFinding(r, "borrowed-brand-pictures"));
    REQUIRE(r.level >= ThreatLevel::Suspicious);
}

TEST(a_brand_mailing_its_own_pictures_and_links_is_not_borrowing) {
    ScanInput in = FakeTinderMatch();
    in.fromAddr = "no-reply@gotinder.com";
    for (std::string::size_type p; (p = in.body.find("vakantiehuiseichenbach.nl")) != std::string::npos;)
        in.body.replace(p, 25, "tinder.com");
    const ThreatReport r = ScanMessage(in);
    REQUIRE(!HasFinding(r, "borrowed-brand-pictures"));
    REQUIRE(!HasFinding(r, "brand-impersonation"));
}

TEST(pictures_from_a_cdn_the_name_does_not_name_are_not_borrowing) {
    ScanInput in;
    in.fromName = "Garden Club";
    in.fromAddr = "news@gardenclub.example";
    in.body = "<img src=\"https://cdn.mailservice.example/a.png\">"
              "<a href=\"https://gardenclub.example/events\">Events</a>";
    in.bodyIsHtml = true;
    REQUIRE(!HasFinding(ScanMessage(in), "borrowed-brand-pictures"));
}

TEST(image_hosts_are_read_from_src_and_background) {
    const auto hosts = ExtractImageHosts(
        "<img src=\"https://a.example/x.png\"><td background='http://b.example/y.jpg'>"
        "<div style=\"background:url(https://c.example/z.png)\"><img src=\"cid:part1\">");
    REQUIRE(hosts.size() == 3);
}

TEST(plain_link_at_finds_the_url_under_a_position) {
    const std::string text = "See https://example.com/a?b=1. Or www.test.org, not this.";
    const std::size_t url = text.find("https://");
    REQUIRE_EQ(PlainLinkAt(text, url), std::string("https://example.com/a?b=1"));
    REQUIRE_EQ(PlainLinkAt(text, url + 10), std::string("https://example.com/a?b=1"));
    REQUIRE(PlainLinkAt(text, text.find("1.") + 1).empty());   // the full stop after it
    REQUIRE(PlainLinkAt(text, 0).empty());                     // "See"
    REQUIRE_EQ(PlainLinkAt(text, text.find("test")), std::string("www.test.org"));
    REQUIRE(PlainLinkAt(text, text.find("not")).empty());
    REQUIRE(PlainLinkAt(text, text.size() + 5).empty());
}
