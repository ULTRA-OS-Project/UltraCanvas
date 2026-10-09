// Tests/UltraMail/test_threatscan.cpp
// Exercises the content scan: link extraction out of HTML and plain text, and
// each rule that can raise a message to Suspicious or Scam — a link whose text
// names one site while its href goes to another, a brand claimed by a sender
// that does not own the domain, userinfo hiding the real host, an IP-literal
// target, an executable attachment — plus the equally important negative
// cases, where an ordinary newsletter and an ordinary personal mail stay out
// of the way.
// Version: 0.7.0 - romance scams (the letters of one reader's inbox), the
//                  "abandoned baggage" advance-fee letter, answers asked for at
//                  another address, cryptocurrency
// Version: 0.6.0 - mail authentication: Authentication-Results parsing, DKIM /
//                  DMARC alignment, the topmost header, genuine mail with
//                  tracking links, forged and look-alike senders still caught
// Version: 0.5.0 - mail addresses in plain text (merged with main's 0.4.0)
// Version: 0.4.0 - banks and exchanges claimed from elsewhere; ordinary words
//                  and mailbox addresses are not claims
// Version: 0.3.0 - PlainLinkAt
// Version: 0.2.0 - borrowed brand pictures (a fake "It's a Match!"), image hosts
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "UltraMailSenderBrands.h"
#include "UltraMailThreatScan.h"

#include <string>
#include <vector>

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
    REQUIRE_EQ(links[0].text, std::string("Click here"));   // as the reader sees it
    REQUIRE_EQ(links[1].host, std::string("other.test"));
    REQUIRE_EQ(links[2].host, std::string("forms.test"));
}

// A link's text is read as it shows: formatting inside a word does not split
// it, so an address dressed up with <b> is still the address it claims to be,
// and every entity is decoded.
TEST(anchor_text_reads_as_it_shows) {
    const auto links = ExtractLinks(
        "<a href=\"https://evil.test/x\">www.pay<b>pal</b>.com</a>"
        "<a href=\"https://example.com/\">Caf&eacute;&nbsp;&#8211;&nbsp;Men&uuml;</a>", true);
    REQUIRE_EQ(links.size(), (size_t)2);
    REQUIRE_EQ(links[0].text, std::string("www.paypal.com"));
    REQUIRE_EQ(links[1].text, std::string("Caf\xC3\xA9 \xE2\x80\x93 Men\xC3\xBC"));
}

// The links come from the parsed page: a link written inside an HTML comment
// or a script is not one the reader can click, an <area> and a <form> are,
// and a mail's "button" (a table inside the link) reads as its words.
TEST(links_come_from_the_parsed_page) {
    const auto links = ExtractLinks(
        "<!-- <a href=\"https://hidden.test/\">old</a> -->"
        "<script>var s = '<a href=\"https://script.test/\">x</a>';</script>"
        "<A HREF='https://upper.test/a?x=1&amp;y=2'>Upper</A>"
        "<map><area shape=rect href=\"https://area.test/\"></map>"
        "<a href=\"https://button.test/\"><table><tr><td>Pay</td><td>now</td></tr></table></a>"
        "<a name=\"anchor-only\">no target</a>"
        "<form method=post action=\"https://form.test/post\"></form>", true);
    REQUIRE_EQ(links.size(), (size_t)4);
    REQUIRE_EQ(links[0].host, std::string("upper.test"));
    REQUIRE_EQ(links[0].href, std::string("https://upper.test/a?x=1&y=2"));
    REQUIRE_EQ(links[0].text, std::string("Upper"));
    REQUIRE_EQ(links[1].host, std::string("area.test"));
    REQUIRE_EQ(links[2].host, std::string("button.test"));
    REQUIRE_EQ(links[2].text, std::string("Pay now"));
    REQUIRE_EQ(links[3].host, std::string("form.test"));
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

// The same lie with the address dressed up in formatting: the old tag
// stripper read "www.pay<b>pal</b>.com" as "www.pay pal .com", which names no
// site, and the link passed.
TEST(a_lying_link_text_split_by_formatting_is_still_a_scam) {
    ScanInput in = Html("service@secure-billing.example",
        "<a href=\"http://203.0.113.9/login\">www.pay<b>pal</b>.<span>com</span></a>");
    const ThreatReport r = ScanMessage(in);
    REQUIRE(HasFinding(r, "link-target-mismatch"));
    REQUIRE(r.level == ThreatLevel::Scam);
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

TEST(plain_text_mail_addresses_are_mailto_links) {
    const std::string text = "Write to support@shop.example, or mailto:sales@shop.example?subject=Hi."
                             " Not an address: a@b, x@y.1, @handle. See https://user@site.example/p";
    REQUIRE_EQ(PlainLinkAt(text, text.find("support")), std::string("mailto:support@shop.example"));
    REQUIRE_EQ(PlainLinkAt(text, text.find("shop.example,")), std::string("mailto:support@shop.example"));
    REQUIRE(PlainLinkAt(text, text.find(", or")).empty());   // the comma after it
    REQUIRE_EQ(PlainLinkAt(text, text.find("mailto:") + 3),
               std::string("mailto:sales@shop.example?subject=Hi"));
    REQUIRE(PlainLinkAt(text, text.find("a@b")).empty());
    REQUIRE(PlainLinkAt(text, text.find("x@y.1")).empty());
    REQUIRE(PlainLinkAt(text, text.find("@handle") + 1).empty());
    // An address inside a web address is part of that web address.
    REQUIRE_EQ(PlainLinkAt(text, text.find("site.example")), std::string("https://user@site.example/p"));

    const auto links = ExtractLinks(text, false);
    REQUIRE(links.size() == 3);
    REQUIRE_EQ(links[0].href, std::string("mailto:support@shop.example"));
    REQUIRE(links[0].host.empty());
    REQUIRE_EQ(links[2].host, std::string("site.example"));
}

TEST(a_mail_address_in_plain_text_raises_no_link_finding) {
    ScanInput in;
    in.fromAddr = "news@gardenclub.example";
    in.body = "Questions? Write to helpdesk@other-service.example any time.";
    in.bodyIsHtml = false;
    REQUIRE(ScanMessage(in).level == ThreatLevel::Clean);
}

// ---------------------------------------------------------------------------
// Mail authentication
// ---------------------------------------------------------------------------
TEST(authentication_results_are_parsed) {
    const AuthResults a = ParseAuthenticationResults(
        "mx.google.com;\r\n"
        "       dkim=pass header.i=@paypal.com header.s=pp-dkim1 header.b=Ab1+;\r\n"
        "       dkim=fail (bad signature; really) header.d=esp-mailer.net;\r\n"
        "       spf=pass (google.com: domain of bounce@mail.paypal.com designates "
        "1.2.3.4 as permitted sender (nested)) smtp.mailfrom=bounce@mail.paypal.com;\r\n"
        "       dmarc=pass (p=REJECT sp=REJECT dis=NONE) header.from=PayPal.com");
    REQUIRE_EQ(a.authservId, std::string("mx.google.com"));
    REQUIRE_EQ(a.dmarc, std::string("pass"));
    REQUIRE_EQ(a.dmarcFrom, std::string("paypal.com"));
    REQUIRE_EQ(a.spf, std::string("pass"));
    REQUIRE_EQ(a.spfDomain, std::string("mail.paypal.com"));
    REQUIRE_EQ(a.dkim.size(), static_cast<std::size_t>(2));
    REQUIRE_EQ(a.dkim[0].first, std::string("pass"));
    REQUIRE_EQ(a.dkim[0].second, std::string("paypal.com"));
    REQUIRE_EQ(a.dkim[1].first, std::string("fail"));
    REQUIRE_EQ(a.dkim[1].second, std::string("esp-mailer.net"));

    // A quoted value may hold a ';'; a method may carry a version.
    const AuthResults b = ParseAuthenticationResults(
        "mail.example.org 1; dkim/1=pass reason=\"good; fine\" header.d=example.com; spf=none");
    REQUIRE_EQ(b.authservId, std::string("mail.example.org"));
    REQUIRE_EQ(b.dkim.size(), static_cast<std::size_t>(1));
    REQUIRE_EQ(b.dkim[0].second, std::string("example.com"));
    REQUIRE_EQ(b.spf, std::string("none"));
    REQUIRE(b.dmarc.empty());

    REQUIRE(ParseAuthenticationResults("mx.example.org; none").dkim.empty());
    REQUIRE(ParseAuthenticationResults("").authservId.empty());
}

TEST(only_the_senders_own_domain_proves_the_sender) {
    std::string how;
    // The mail service's signature says nothing about the From address.
    REQUIRE(VerifiedSenderDomain(
        ParseAuthenticationResults("mx.test; dkim=pass header.d=sendgrid.net"),
        "shop.example").empty());
    REQUIRE_EQ(VerifiedSenderDomain(
        ParseAuthenticationResults("mx.test; dkim=pass header.d=mail.shop.example"),
        "news.shop.example", &how), std::string("news.shop.example"));
    REQUIRE_EQ(how, std::string("DKIM signature"));
    REQUIRE_EQ(VerifiedSenderDomain(
        ParseAuthenticationResults("mx.test; spf=pass; dmarc=pass header.from=shop.example"),
        "shop.example", &how), std::string("shop.example"));
    REQUIRE_EQ(how, std::string("DMARC"));
    REQUIRE_EQ(VerifiedSenderDomain(
        ParseAuthenticationResults(
            "mx.test; dkim=pass header.d=shop.example; dmarc=pass header.from=shop.example"),
        "shop.example", &how), std::string("shop.example"));
    REQUIRE_EQ(how, std::string("DKIM signature and DMARC"));
    // DMARC for another domain, or a DMARC failure, proves nothing.
    REQUIRE(VerifiedSenderDomain(
        ParseAuthenticationResults("mx.test; dmarc=pass header.from=other.example"),
        "shop.example").empty());
    REQUIRE(VerifiedSenderDomain(
        ParseAuthenticationResults(
            "mx.test; dkim=pass header.d=shop.example; dmarc=fail header.from=shop.example"),
        "shop.example").empty());
}

TEST(the_receiving_servers_header_is_the_topmost) {
    const std::string raw =
        "Authentication-Results: mx.real.example;\r\n"
        "\tdmarc=fail (p=REJECT) header.from=paypal.com\r\n"
        "Received: from somewhere\r\n"
        "Authentication-Results: forged.example; dkim=pass header.d=paypal.com;\r\n"
        "  dmarc=pass header.from=paypal.com\r\n"
        "From: PayPal <service@paypal.com>\r\n"
        "Subject: Your account\r\n"
        "Content-Type: text/plain\r\n"
        "\r\n"
        "Authentication-Results: in.the.body; dmarc=pass\r\n";
    REQUIRE_EQ(TopHeaderValue(raw, "authentication-results"),
               std::string("mx.real.example; dmarc=fail (p=REJECT) header.from=paypal.com"));
    REQUIRE(TopHeaderValue(raw, "X-Missing").empty());
    const ThreatReport r = ScanRawMessage(raw);
    REQUIRE(HasFinding(r, "auth-failure"));
    REQUIRE(r.verifiedDomain.empty());
}

TEST(forwarded_mail_and_a_foreign_signature_are_not_forgery) {
    // A forwarder fails SPF; the sender's own signature still passes.
    ScanInput fwd = Html("someone@example.org", "<p>hello</p>");
    fwd.authResults = "mx.test; spf=fail smtp.mailfrom=lists.example.net; "
                      "dkim=pass header.d=example.org";
    ThreatReport r = ScanMessage(fwd);
    REQUIRE(!HasFinding(r, "auth-failure"));
    REQUIRE_EQ(r.verifiedDomain, std::string("example.org"));

    // A second signature, by the mail service, fails; the sender's passes.
    ScanInput two = Html("someone@example.org", "<p>hello</p>");
    two.authResults = "mx.test; dkim=fail header.d=esp.example; dkim=pass header.d=example.org";
    REQUIRE(!HasFinding(ScanMessage(two), "auth-failure"));

    // Nothing passes and SPF fails: as before, possibly forged.
    ScanInput bad = Html("someone@example.org", "<p>hello</p>");
    bad.authResults = "mx.test; spf=fail smtp.mailfrom=example.org; dkim=fail header.d=example.org";
    REQUIRE(HasFinding(ScanMessage(bad), "auth-failure"));
}

TEST(a_proven_senders_tracking_links_are_not_a_scam) {
    // A newsletter: the text names the shop's own site, the link goes through
    // its mail service's click tracker; replies go to its help desk; links to
    // many sites.
    const std::string body =
        "<p>Our autumn sale has started.</p>"
        "<a href=\"https://click.esp-tracker.example/ls/abc\">www.shop.example/sale</a>"
        "<a href=\"https://a.example/1\">A</a><a href=\"https://b.example/1\">B</a>"
        "<a href=\"https://c.example/1\">C</a><a href=\"https://d.example/1\">D</a>";
    ScanInput plain = Html("news@shop.example", body);
    plain.replyTo = "support@helpdesk.example";
    const ThreatReport unproven = ScanMessage(plain);
    REQUIRE(HasFinding(unproven, "link-target-mismatch"));
    REQUIRE(unproven.Suspicious());

    ScanInput signedIn = plain;
    signedIn.authResults = "mx.test; dkim=pass header.d=shop.example; "
                           "dmarc=pass header.from=shop.example";
    const ThreatReport proven = ScanMessage(signedIn);
    REQUIRE(!HasFinding(proven, "link-target-mismatch"));
    REQUIRE(!HasFinding(proven, "reply-to-mismatch"));
    REQUIRE(!HasFinding(proven, "many-foreign-domains"));
    REQUIRE(!proven.Suspicious());
    REQUIRE_EQ(proven.verifiedDomain, std::string("shop.example"));
    REQUIRE_EQ(proven.verifiedBy, std::string("DKIM signature and DMARC"));
}

TEST(a_proven_lookalike_domain_is_still_caught) {
    // A fraudster can sign for a domain of their own: proven, but not PayPal.
    ScanInput in = Html("PayPal <service@paypa1-alerts.example>",
        "<p>Your account will be suspended.</p>"
        "<a href=\"https://collector.example/x\">www.paypal.com</a>");
    in.fromName = "PayPal";
    in.authResults = "mx.test; dkim=pass header.d=paypa1-alerts.example; "
                     "dmarc=pass header.from=paypa1-alerts.example";
    const ThreatReport r = ScanMessage(in);
    REQUIRE_EQ(r.verifiedDomain, std::string("paypa1-alerts.example"));
    REQUIRE(HasFinding(r, "link-target-mismatch"));
    REQUIRE(HasFinding(r, "brand-impersonation"));
    REQUIRE(HasFinding(r, "credential-request"));
    REQUIRE(r.level == ThreatLevel::Scam);
}

TEST(the_proven_brand_may_ask_to_update_details) {
    const std::string body =
        "<p>Your account will be suspended unless you update your payment details.</p>"
        "<a href=\"https://click.paypal-mailer.example/r/1\">Log in</a>";
    ScanInput spoofed = Html("service@paypal.com", body);
    REQUIRE(HasFinding(ScanMessage(spoofed), "credential-request"));

    ScanInput genuine = spoofed;
    genuine.authResults = "mx.test; dkim=pass header.d=paypal.com; dmarc=pass header.from=paypal.com";
    const ThreatReport r = ScanMessage(genuine);
    REQUIRE(!HasFinding(r, "credential-request"));
    REQUIRE(!r.Suspicious());
    REQUIRE_EQ(r.verifiedDomain, std::string("paypal.com"));
}

namespace {
const AuthCheck* CheckNamed(const std::vector<AuthCheck>& checks, const std::string& label) {
    for (const auto& c : checks) if (c.label == label) return &c;
    return nullptr;
}
bool Says(const AuthCheck* c, const std::string& text) {
    return c && c->tooltip.find(text) != std::string::npos;
}
} // namespace

TEST(the_sender_checks_become_labels_with_their_details) {
    const auto checks = DescribeAuthentication(ParseAuthenticationResults(
        "mx.google.com; dkim=pass header.d=paypal.com; dkim=pass header.d=esp-mailer.net; "
        "spf=fail smtp.mailfrom=bounce@lists.example.net; dmarc=pass header.from=paypal.com"),
        "paypal.com");
    REQUIRE_EQ(checks.size(), static_cast<std::size_t>(3));
    REQUIRE_EQ(checks[0].label, std::string("DMARC"));   // the strongest first
    REQUIRE_EQ(checks[1].label, std::string("DKIM"));
    REQUIRE_EQ(checks[2].label, std::string("SPF"));
    REQUIRE(checks[0].state == AuthCheckState::Passed);
    REQUIRE(checks[1].state == AuthCheckState::Passed);
    REQUIRE(checks[2].state == AuthCheckState::Failed);
    REQUIRE(Says(&checks[0], "The From address is genuine"));
    REQUIRE(Says(&checks[1], "Signature of paypal.com: passed"));
    REQUIRE(Says(&checks[1], "esp-mailer.net (a mail service) sent it"));
    REQUIRE(Says(&checks[2], "lists.example.net is the envelope sender"));
    for (const auto& c : checks) REQUIRE(Says(&c, "Checked by mx.google.com"));

    // Only a mail service signed: passed, but not the sender's own.
    const auto esp = DescribeAuthentication(ParseAuthenticationResults(
        "mx.test; dkim=pass header.d=sendgrid.net"), "shop.example");
    REQUIRE(Says(CheckNamed(esp, "DKIM"), "No signature is shop.example's own"));

    // A forged From: DMARC fails.
    const auto forged = DescribeAuthentication(ParseAuthenticationResults(
        "mx.test; dmarc=fail header.from=paypal.com"), "paypal.com");
    REQUIRE(CheckNamed(forged, "DMARC")->state == AuthCheckState::Failed);
    REQUIRE(Says(CheckNamed(forged, "DMARC"), "likely forged"));

    // Nothing recorded: one grey label, not a warning.
    const auto none = DescribeAuthentication(AuthResults{}, "example.org");
    REQUIRE_EQ(none.size(), static_cast<std::size_t>(1));
    REQUIRE_EQ(none[0].label, std::string("Not checked"));
    REQUIRE(none[0].state == AuthCheckState::Neutral);
}

TEST(a_signed_message_is_recognised_but_not_vouched_for) {
    const std::string smime =
        "From: Erika <erika@example.org>\r\n"
        "Content-Type: multipart/signed; protocol=\"application/pkcs7-signature\";\r\n"
        "  micalg=sha-256; boundary=\"b1\"\r\n"
        "\r\n--b1\r\n\r\nhello\r\n--b1--\r\n";
    REQUIRE_EQ(MessageSignatureKind(smime), std::string("S/MIME"));
    const auto checks = DescribeMessageAuthentication(smime);
    REQUIRE_EQ(checks.size(), static_cast<std::size_t>(2));   // Not checked + S/MIME
    REQUIRE(CheckNamed(checks, "S/MIME")->state == AuthCheckState::Neutral);
    REQUIRE(Says(CheckNamed(checks, "S/MIME"), "does not check such signatures yet"));

    REQUIRE_EQ(MessageSignatureKind(
        "Content-Type: multipart/signed; protocol=\"application/pgp-signature\"\r\n\r\n"),
        std::string("OpenPGP"));
    REQUIRE_EQ(MessageSignatureKind(
        "Content-Type: application/pkcs7-mime; smime-type=signed-data\r\n\r\n"),
        std::string("S/MIME"));
    REQUIRE(MessageSignatureKind("Content-Type: text/plain\r\n\r\nhi").empty());
}

// ---------------------------------------------------------------------------
// Romance scams - the letters below are real ones, sent to one reader
// between 2011 and 2019, as they arrived (names and addresses as sent).
// ---------------------------------------------------------------------------
namespace {

ScanInput Letter(const std::string& from, const std::string& subject,
                 const std::string& body, std::vector<std::string> attachments = {}) {
    ScanInput in;
    in.fromAddr = from;
    in.subject  = subject;
    in.body     = body;
    in.attachmentNames = std::move(attachments);
    return in;
}

bool IsRomanceScam(const ScanInput& in) {
    const ThreatReport r = ScanMessage(in);
    return HasFinding(r, "romance-scam") && r.level == ThreatLevel::Scam;
}

} // namespace

TEST(romance_horoscope_nurse_from_russia_is_a_scam) {
    const ScanInput in = Letter("anna.k.tova@gmail.com", "Where are you my dear?",
        "Good day, my friend! This morning in the horoscope for a laugh read the forecast "
        "for today.\nThere I was promised a romantic acquaintance, which will be of great "
        "importance for me for a long time to come. Well, I guess it's fate, not otherwise. "
        "But somehow that's all no one comes to me first, in the Elevator of charming "
        "strangers is not, on the foot no one comes, which hour no one asked.\nWell, I'm a "
        "pushy girl. In short, I go to a Dating site, and find a profile of a person who "
        "believes in love.\nWell, maybe in horoscopes not only nonsense write, suddenly I "
        "have a chance?:)) Well and here...I write to You ,what else..?\nAnd now a little "
        "bit about me. Small (164),thin (size 40), modest in appearance, but very "
        "temperamental inside.\nMy name is Anna, I'm 31 years old, I work as a nurse, "
        "divorced. I live in Russia. I'll send you two pictures.I hope you like them. "
        "\"I shall await your earliest response.\"",
        { "IMG_942.jpg" });
    REQUIRE(IsRomanceScam(in));
    const ThreatReport r = ScanMessage(in);
    REQUIRE(r.Has("romance-scam"));
    REQUIRE(r.Summary().find("IMG_942.jpg") != std::string::npos);
    REQUIRE(r.Summary().find("free mailbox (gmail.com)") != std::string::npos);
}

TEST(romance_are_you_the_real_deal_is_a_scam) {
    REQUIRE(IsRomanceScam(Letter("Delroy Kneeskern <delroyknkl2w@hotmail.com>", "Hello",
        "Hello there I am attracted regarding your posting.\n Are you really the real "
        "deal? I'm just sick of researching untrue humans here.\nI won't be offering you "
        "to any sort of web websites that require you to signup with your bank card.\n"
        "You happen to be genuine then answer back to me and I will send you my personal "
        "details so we could possibly get it started. Waiting fo u",
        { "ggaqptbrwkq.jpg" })));
}

TEST(romance_screen_name_guilt_trip_is_caught) {
    const std::string body =
        "Bonjour...\nWow, it is so good to see you again\nDon't upset Shui98 or make her "
        "bored.. Please reply as soon as possible!";
    // The text alone is worth a second look ...
    const ThreatReport bare = ScanMessage(Letter("", "", body));
    REQUIRE(bare.Has("romance-scam"));
    REQUIRE(bare.level >= ThreatLevel::Suspicious);
    // ... and with the photo, from a free mailbox, it is the scam itself.
    REQUIRE(IsRomanceScam(Letter("shui98@gmail.com", "Hi", body, { "me.jpg" })));
}

TEST(romance_marina_from_the_site_is_a_scam) {
    REQUIRE(IsRomanceScam(Letter("Marishka <marishklana@gmail.com>",
        "Marina interkontakt site",
        "Hi my new friend Stefan !\n\nHow are you? It is nice to find you here among the "
        "millions of men...\nMarina is writing to you))) I am on the site in search of "
        "real love\nand my future husband! Hopefully, you look for the same and that is\n"
        "why I suggest to get to know each other better! What do you think?\nHonesty, "
        "trust and kindness are the main features of my character so\ndo not hesitate to "
        "write me at    marishklana@gmail.com    .\nI am sure lucky star is on our "
        "side)))\n\n\nLooking forward to your soonest reply with  your life story and\n"
        "photos! And please no playing fool!!! Just serious intentions!\n\n\n\nTruly "
        "yours,\nMarina",
        { "me smile.jpg" })));
}

TEST(romance_yana_from_a_lookalike_domain_is_a_scam) {
    REQUIRE(IsRomanceScam(Letter(
        "Yana <redesignedb@uncollatednessi.faceebookinbox.biz>", "How is it going?",
        "good Day! how are you?everything is good?\nMy name is Yana or simple Yanya. I am "
        "single lady and look for love.. So this is a reason why I am conatcting you:) I "
        "have information that you are single too and look for true love and "
        "relationships. Does it true?\nWill tell you little about me now: My age is 30 "
        "years,I live in Russia,and I am health care worker. Single never married and "
        "have no kids. I am optimistic person and have many different hobbies and "
        "interests. My main dream is to find right man. So thats some information about "
        "me. I will be happy if you will reply me and tell more about you and your life. "
        "Hope to find your email soon. bye.",
        { "62ji.jpg" })));
}

TEST(romance_thread_asking_for_laptop_money_is_a_scam) {
    // The latest answer of a long thread, its quotes below it as they arrive.
    REQUIRE(IsRomanceScam(Letter("anita sam <anitasanton1000@gmail.com>",
        "Re: this is anita from tg",
        "I was in Odessa because I wanted to attend a friend wedding and I went back and I "
        "am taking care of my old mother\nI am in Luhansk state right now\nThis is where am "
        "from and yes am a hot girl but am not an escort or a prostitute but I respect "
        "myself so much\nI can't sell my body for money only because my state is not "
        "good!!\nPlease understand me\nI am for real and I am serious\nI am honest to you "
        "too\nHope to hear from you soon\n\n"
        "> I am for real and everything about me on the site is real about me\n"
        "> I sent you my scan passport and that is me and that is the real me\n"
        "> I want to fixed myself with a cheap laptop and understand me and such money "
        "is big for webcam ... please I don't like when some one play with my feelings\n"
        "> Thank you so much my dear and also for being there for me ... Kisssssss\n"
        "> send me the money let me get a new laptop and a cheap laptop and we see each "
        "other on Skype\n")));
}

TEST(romance_rent_and_escort_review_is_a_scam) {
    REQUIRE(IsRomanceScam(Letter("", "",
        "I am 19 years young, attractive and brunette. During my time in school, I was on "
        "the cheerleading team. I have to pay my rent payment in less than a week. I can "
        "to provide you with whatever you want and the only thing I want is some "
        "assistance with paying my housing obligations. I guarantee to give your desires "
        "in any way possible. I am unable to answer any explicit questions, but one of my "
        "clients posted a review for me and put it online.\n\nI assure you are not going "
        "to need to create an account or do a significant typing, except when you dial my "
        "phone number into your phone. The only thing you are required to do is go to "
        "female escorts button and look through my review. ... if you look through my "
        "pictures you will see that I am definitely worth it.\n\nwww.smashallnight.us")));
}

TEST(romance_hookup_verification_site_is_a_scam) {
    REQUIRE(IsRomanceScam(Letter("\"Jennifer Smith\" <jennifersth790@hotmail.com>",
        "Re: want to f.....",
        "heyyy again,\nyou sound pretty cool and i\xE2\x80\x99m definitely interested in "
        "meeting up with you.... i was busy today and just come back home.so,it would be "
        "awesome to talk now and fix a good time to meet... i really just want to fuck if "
        "you know what i mean.. :).. to get my number just go "
        "http://doxydaters.com/members/hot/jen87/ and login there .. my number will be "
        "right on the first page.. they never charge you or anything, they just verify to "
        "make sure you don't have a criminal history like you aren\xE2\x80\x99t a rapist "
        "or anything, you know a girl can never be too careful..btw,I lost my cell phone "
        "today,so call to my home phone instead of cell phone...waiting for your call.",
        { "Attachment.jpg" })));
}

TEST(romance_destiny_letter_from_i_ua_is_a_scam) {
    REQUIRE(IsRomanceScam(Letter("Ira <Elixir_of_Love@i.ua>", "Ira",
        "Sunny & Cheerful Greetings to you,My Dear Stefan,\n\nI  sincerely believe that "
        "true happiness is destiny and when\na chance meeting makes your heart beat faster "
        "then I believe\nthat  you've  found  a  union  that  is  worth nurturing and\n"
        "exploring  to  make  it unique and everlasting... I have an\nabundant  of  love  "
        "to  share  and  give to you. I'm drawn to you,it is fate or a cosmic force;it\nis "
        " strong and yet loving,calling us to be together ... Join me dear,and\nlet's "
        "start our journey together!!!\n\nRegards,Yours Only Elixir,<<Ira>>.",
        { "Elixir.jpg", "your little Kitty;).jpg" })));
}

TEST(romance_lawyer_in_tokyo_is_a_scam) {
    REQUIRE(IsRomanceScam(Letter("\"Michael Kidman-Bengoshi\" <mikl231@live.com>",
        "Hi!Im live in Tokyo,are you still looking for friend?Are you ok in earthquake?",
        "Hi!\nHow are you?\nWe emailed each other a long time ago!\nMada tomodachi "
        "sagashiteimasuka?Are you ok in earthquake?\nI am Michael\nI just came back to "
        "Tokyo from business trip to New-York and London!\nI am emailing you my 2 photos!\n"
        "Are you still using- info@filipinokisses.com?\nGive me your new email addresses!\n"
        "Im lawyer!\nI work and live in Tokyo!\nI am looking for friend to go for "
        "dinner,tea together,movies ...\nI have blue eyes,light brown hair,people say that "
        "handome\nI always do my best to be kind,sincere,goodhearted,yasashi and "
        "gentleman!\nMy dream is serious relationship and marriage in future,I believe in "
        "true love ... In addition I am rich-that can be helpful-I will pay for marriage "
        "and buy house,take care of everything!\nLets meet,have tea or dinner together 1st "
        "...\nEmail me!\nGive me your email addresses-your keitai and computer email "
        "addresses!\nSee you,\nBye\nMichael Ford",
        { "0002052fDwt.jpg", "000B052fDwt.jpg" })));
}

TEST(romance_photo_in_a_raw_message_counts) {
    const std::string raw =
        "From: Anna <anna.k.tova@gmail.com>\r\n"
        "Subject: Where are you my dear?\r\n"
        "MIME-Version: 1.0\r\n"
        "Content-Type: multipart/mixed; boundary=\"b1\"\r\n\r\n"
        "--b1\r\nContent-Type: text/plain; charset=utf-8\r\n\r\n"
        "I go to a Dating site and find a profile of a person who believes in love. "
        "My name is Anna, I'm 31 years old.\r\n"
        "--b1\r\nContent-Type: image/jpeg; name=\"IMG_942.jpg\"\r\n"
        "Content-Disposition: attachment; filename=\"IMG_942.jpg\"\r\n"
        "Content-Transfer-Encoding: base64\r\n\r\n/9j/4AAQSkZJRg==\r\n"
        "--b1--\r\n";
    const ThreatReport r = ScanRawMessage(raw);
    REQUIRE(r.Has("romance-scam"));
    REQUIRE(r.level == ThreatLevel::Scam);
    REQUIRE(HasFindingCode(r.Codes(), "romance-scam"));
}

TEST(a_partners_holiday_photos_are_not_a_romance_scam) {
    // Pet names, photos and "write back" - but nothing only a stranger writes.
    const ThreatReport r = ScanMessage(Letter("erika.example@gmail.com", "Our holiday",
        "Hi my dear, here are the photos from our week in Spain, I hope you like them. "
        "Write back soon! Kisses",
        { "beach.jpg", "dinner.jpg", "sunset.jpg" }));
    REQUIRE(!r.Has("romance-scam"));
    REQUIRE(r.level == ThreatLevel::Clean);
}

TEST(a_job_application_with_a_photo_is_not_a_romance_scam) {
    const ThreatReport r = ScanMessage(Letter("anna.applicant@gmail.com",
        "Application for the position of nurse",
        "Dear Sir or Madam, I saw your job posting. My name is Anna, I'm 31 years old and "
        "I work as a nurse; I am attracted to your clinic's caring atmosphere. Please find "
        "attached my CV and my photo. I await your earliest response.",
        { "Anna_CV.pdf", "photo.jpg" }));
    REQUIRE(!r.Has("romance-scam"));
}

TEST(a_reply_to_a_classified_ad_is_not_a_romance_scam) {
    const ThreatReport r = ScanMessage(Letter("john.buyer@gmail.com", "Your sofa",
        "Hello, I saw your ad for the sofa. My name is John. Is it still available? "
        "Please reply, I can pick it up on Saturday."));
    REQUIRE(!r.Has("romance-scam"));
}

TEST(a_dating_newsletter_is_not_a_romance_scam) {
    ScanInput in = Letter("news@datingtips.example", "Find true love this autumn",
        "Looking for love? Update your profile with your photos - members with photos get "
        "more messages. Write back to us any time.");
    in.listUnsubscribe = "<mailto:unsubscribe@datingtips.example>";
    REQUIRE(!ScanMessage(in).Has("romance-scam"));
}

// ---------------------------------------------------------------------------
// The "abandoned baggage" advance-fee letter, and answers asked for elsewhere
// ---------------------------------------------------------------------------
TEST(abandoned_baggage_advance_fee_is_a_scam) {
    const ThreatReport r = ScanMessage(Letter("Harrisburg Airport <info@airport-claims.example>",
        "Baggage & Laugages dispute",
        "Good day to you.\nI am Mr Lee. Byrne ,in-charge for or lost abandoned baggage,and "
        "laugages here in the Harrisburg International Airport service Pennsylvania USA. "
        "Due to a vital research here in our office,we found a baggage that contains the "
        "amount of $7.5 million united state dollars. This baggage has been here in our "
        "custody without no claim of any individual since two years now ... I am seriously "
        "looking for a trust worthy person with a kinder-ed heart of God ... you shall "
        "receive 50% out of the amount as your own share ... if you are willing to be "
        "honest and God fearing so that we can both handle this deal in one mind.\n"
        "Please find my contact email address for us to proceed : ( l.byrne96@yahoo.com ).\n"
        "Thank you for receiving my private email message.\nStay Blessed."));
    REQUIRE(r.Has("advance-fee-fraud"));
    REQUIRE(r.Has("reply-elsewhere"));
    REQUIRE(r.level == ThreatLevel::Scam);
}

TEST(the_senders_own_address_in_the_body_is_not_reply_elsewhere) {
    REQUIRE(!ScanMessage(Letter("Marishka <marishklana@gmail.com>", "Hi",
        "do not hesitate to write me at marishklana@gmail.com .")).Has("reply-elsewhere"));
    REQUIRE(!ScanMessage(Letter("anna@shop.example", "Order",
        "Questions? Contact me at support@shop.example.")).Has("reply-elsewhere"));
}

// ---------------------------------------------------------------------------
// Cryptocurrency
// ---------------------------------------------------------------------------
TEST(any_crypto_mail_gets_the_crypto_caution) {
    const ThreatReport r = ScanMessage(Letter("news@example.test", "Markets",
        "The bitcoin price rose by four percent today."));
    REQUIRE(r.Has("crypto-content"));
    REQUIRE(r.level == ThreatLevel::Clean);   // a word of caution, not a verdict
    REQUIRE(!ScanMessage(Letter("news@example.test", "Security update",
        "UltraCrypt now uses cryptography from libsodium.")).Has("crypto-content"));
}

TEST(a_proven_exchange_is_cautioned_but_not_flagged) {
    ScanInput in = Letter("Coinbase <no-reply@coinbase.com>", "Your weekly summary",
        "Your balance: 0.5 BTC. Coinbase will never ask for your recovery phrase.");
    in.authResults = "mx.example.net; dkim=pass header.d=coinbase.com; "
                     "dmarc=pass header.from=coinbase.com";
    const ThreatReport r = ScanMessage(in);
    REQUIRE(r.Has("crypto-content"));
    REQUIRE(!r.Has("crypto-investment-lure"));
    REQUIRE(!r.Has("crypto-wallet-secret"));
    REQUIRE(r.level == ThreatLevel::Clean);
}

TEST(asking_for_a_recovery_phrase_is_a_scam) {
    const ThreatReport r = ScanMessage(Letter("security@metamask-support.example",
        "Wallet suspended",
        "Your MetaMask wallet has been suspended. Please verify your 12-word recovery "
        "phrase to reactivate it."));
    REQUIRE(r.Has("crypto-wallet-secret"));
    REQUIRE(r.level == ThreatLevel::Scam);
    // Telling the reader never to share it is the opposite.
    REQUIRE(!ScanMessage(Letter("help@wallet.example", "Tip",
        "Keep your seed phrase offline. Never enter your seed phrase on a website."))
        .Has("crypto-wallet-secret"));
}

TEST(a_bitcoin_address_to_pay_into_is_a_scam) {
    const ThreatReport r = ScanMessage(Letter("hacker@mailbox.example", "Your device",
        "I recorded you through your camera. Pay 1500 USD in Bitcoin to this address: "
        "bc1qxy2kgdygjrsqtzq2n0yrf2493p83kkfjhx0wlh within 48 hours."));
    REQUIRE(r.Has("crypto-payment-demand"));
    REQUIRE(r.level == ThreatLevel::Scam);
    REQUIRE(ScanMessage(Letter("x@mailbox.example", "Invoice",
        "Send the BTC to 1BvBMSEYstWetqTFn5Au4m4GFg7xJaNVN2 today."))
        .Has("crypto-payment-demand"));
    // A long number or a word is not an address.
    REQUIRE(!ScanMessage(Letter("x@shop.example", "Bitcoin accepted",
        "We accept bitcoin. Order 1234567890123456789012345678 ships today."))
        .Has("crypto-payment-demand"));
}

TEST(promised_crypto_profit_is_a_scam) {
    const ThreatReport r = ScanMessage(Letter("anna.invest@gmail.com", "My secret",
        "My uncle showed me this trading platform: I make 30% daily returns trading "
        "bitcoin, guaranteed profit, no risk."));
    REQUIRE(r.Has("crypto-investment-lure"));
    REQUIRE(r.level == ThreatLevel::Scam);
}

TEST(the_fbi_atm_card_letter_is_a_scam) {
    const ThreatReport r = ScanMessage(Letter("FBI <director@fbi-atm-center.example>",
        "Attention Beneficiary",
        "Federal Bureau of Investigation\nAttention Beneficiary,\nNOTE: If you received "
        "this message in your SPAM / BULK folder ... its a legitimate email.\nWe the Federal "
        "Bureau of Investigation (FBI) recover some huge amount of money from Fraudsters "
        "... in conjunction with the International Monitory Funds (IMF) ... to share the "
        "huge amount of money among those that have been scam ... your name and address "
        "where selected randomly as one of the Scam Victims.\nThe National Central Bureau "
        "of Interpol enhanced by the United Nations ... Contract Sum, Lottery/Gambling, "
        "Inheritance and the likes. ... your payment totaling $2,900,000.00(Two Million "
        "Nine Hundred Thousand Dollars). will be released to you via a custom pin based "
        "ATM card ... contact the ATM Card Center via email for their requirement to "
        "proceed and procure your Approval of Payment Warrant ... which will cost you $250 "
        "Usd only ... including taxes, custom paper and clearance duty\n\nDr. Lord Ruben\n"
        "ATM Card Center Director\nPrivate Email: lordbenn@foxmail.com\n"));
    REQUIRE(r.Has("advance-fee-fraud"));
    REQUIRE(r.Has("reply-elsewhere"));
    REQUIRE(r.Has("government-impersonation"));   // "FBI" in the name and the domain
    REQUIRE(r.level == ThreatLevel::Scam);
}

TEST(everyday_words_near_crypto_and_romance_stay_quiet) {
    // Apple's AirDrop is not a crypto airdrop.
    REQUIRE(!ScanMessage(Letter("tips@apple.example", "Share faster",
        "Use AirDrop to share your photos with friends nearby.")).Has("crypto-content"));
    // A form's "Sex:" field is not an offer.
    REQUIRE(!ScanMessage(Letter("clinic.reception@gmail.com", "Your appointment",
        "My name is Dr. Weber. Please bring the form: Name, Sex, Date of birth. "
        "Write back if the time does not suit you."))
        .Has("romance-scam"));
    // A reset link's token is not a wallet address.
    REQUIRE(!ScanMessage(Letter("noreply@bitcoin-news.example", "Reset",
        "Reset your bitcoin news password: https://bitcoin-news.example/reset?token="
        "1BvBMSEYstWetqTFn5Au4m4GFg7xJaNVN2")).Has("crypto-payment-demand"));
}

TEST(the_next_of_kin_attorney_letter_is_a_scam) {
    const ThreatReport r = ScanMessage(Letter("Foga Bama <fogabama.esq@mailbox.example>",
        "Dear Friend",
        "Dear Friend,\n\nI am Foga Bama, personal attorney to Mr. John W. Froling ... On "
        "the 21st of October, 2007, my client, his wife and their only daughter, were "
        "involved in a car accident ... all occupants of the vehicle lost their lives. ... "
        "he left behind the sum of Ten million United States of American dollars (US$10 "
        "million) in a Bank. ... The Bank has issued me a notice to provide the next of kin "
        "or have his account confiscated ... unserviceable and dormant accounts. ... I seek "
        "your consent to present you as the next of kin to the deceased since you have the "
        "same last (surname) name. ... I wish to use part of my share to donate to "
        "charitable organisations and churches. ... we shall then discuss the sharing ratio "
        "and modalities for transfer.\n\nYour brother and friend,\nMr. Foga Bama, Esq."));
    REQUIRE(r.Has("advance-fee-fraud"));
    REQUIRE(r.level == ThreatLevel::Scam);
}

// ---------------------------------------------------------------------------
// Sender domains dressed up as a brand's
// ---------------------------------------------------------------------------
namespace {
std::string Imitated(const std::string& domain) {
    const DomainLookalike l = BrandImitatedByDomain(domain);
    return l.brand ? l.brand->id : std::string();
}
} // namespace

TEST(lookalike_sender_domains_are_recognised) {
    // Yana's letter: Facebook with a doubled "e", padded with "inbox".
    const DomainLookalike yana = BrandImitatedByDomain("uncollatednessi.faceebookinbox.biz");
    REQUIRE(yana.brand != nullptr);
    REQUIRE_EQ(yana.brand->id, std::string("facebook"));
    REQUIRE(yana.kind == LookalikeKind::Misspelt);
    REQUIRE_EQ(yana.worn, std::string("faceebook"));

    REQUIRE_EQ(Imitated("paypal-secure-login.com"), std::string("paypal"));
    REQUIRE(BrandImitatedByDomain("paypal-secure-login.com").kind == LookalikeKind::Name);
    REQUIRE_EQ(Imitated("appleidverify.com"), std::string("apple"));
    REQUIRE_EQ(Imitated("amaz0n-billing.com"), std::string("amazon"));
    REQUIRE_EQ(Imitated("paypa1.com"), std::string("paypal"));
    REQUIRE_EQ(Imitated("rnicrosoft-account.net"), std::string("microsoft"));
    REQUIRE_EQ(Imitated("faceboook.com"), std::string("facebook"));
    REQUIRE_EQ(Imitated("mail.linkedln-notify.com"), std::string("linkedin"));
    REQUIRE_EQ(Imitated("instagrarn.top"), std::string("instagram"));
    const DomainLookalike own = BrandImitatedByDomain("paypal.com.account-check.ru");
    REQUIRE(own.kind == LookalikeKind::OwnDomain);
    REQUIRE_EQ(own.worn, std::string("paypal.com"));
}

TEST(ordinary_domains_with_a_brand_word_are_not_lookalikes) {
    for (const char* d : { "zoomcare.com", "cdn.discordapp.com", "redditmail.com",
                           "myhermes.de", "dropboxmail.com", "spotify-news.example",
                           "netflix-online.example", "applewood-estates.com",
                           "amazonas-reisen.de", "pineapple-shop.example",
                           "hermes.uni-example.de", "telecom-services.example", "interact.example",
                           "canvas-studio.example", "goggles-shop.example", "revolt.example",
                           "paypal.xyz", "gmail.com", "facebookmail.com", "mail.paypal.de",
                           "amazonses.com", "paypal-community.com", "metamask.io" })
        REQUIRE_EQ(Imitated(d), std::string());
}

TEST(a_lookalike_sender_is_a_scam) {
    const ThreatReport r = ScanMessage(Letter("Yana <redesignedb@uncollatednessi.faceebookinbox.biz>",
        "How is it going?", "Hello, how are you?"));
    REQUIRE(r.Has("sender-domain-lookalike"));
    REQUIRE(r.level == ThreatLevel::Scam);
    REQUIRE(r.Summary().find("faceebook") != std::string::npos);
    REQUIRE(r.Summary().find("Facebook") != std::string::npos);
    REQUIRE(!ScanMessage(Letter("Facebook <notification@facebookmail.com>", "New login",
        "A new login to your account.")).Has("sender-domain-lookalike"));
}

// ---------------------------------------------------------------------------
// Government agencies and international organisations
// ---------------------------------------------------------------------------
TEST(an_agency_in_the_senders_name_from_elsewhere_is_a_scam) {
    ScanInput in = Letter("interpol.police@gmail.com", "Notice", "Please contact us.");
    in.fromName = "INTERPOL Police Department";
    const ThreatReport r = ScanMessage(in);
    REQUIRE(r.Has("government-impersonation"));
    REQUIRE(r.level == ThreatLevel::Scam);
    REQUIRE(r.Summary().find("Interpol") != std::string::npos);
}

TEST(a_letter_in_an_agencys_name_about_your_money_is_flagged) {
    const ThreatReport r = ScanMessage(Letter("payments.office@yahoo.com", "Your compensation",
        "We the International Monetary Fund have approved your compensation payment as a "
        "scam victim. Contact this office."));
    REQUIRE(r.Has("government-impersonation"));
    REQUIRE(r.level >= ThreatLevel::Suspicious);
}

TEST(real_agencies_and_news_about_them_are_not_impersonation) {
    for (const char* from : { "alerts@ic3.gov", "news@fbi.gov", "press@interpol.int",
                              "media@imf.org", "info@bka.de", "kontakt@bundespolizei.bund.de",
                              "noreply@hmrc.gov.uk", "office@justice.gouv.fr" }) {
        ScanInput in = Letter(from, "Your case number",
            "Dear beneficiary, the FBI and Interpol hereby inform you about your case.");
        REQUIRE(!ScanMessage(in).Has("government-impersonation"));
    }
    // A news item mentions the FBI without writing in its name.
    REQUIRE(!ScanMessage(Letter("friend@gmail.com", "Did you see this?",
        "The FBI director said today that romance scams cost a billion dollars."))
        .Has("government-impersonation"));
    // A newsletter from a domain of its own may write about anyone.
    ScanInput news = Letter("digest@news.example", "FBI warns of romance scams",
        "The FBI hereby warns: never send money to someone you met online.");
    news.listUnsubscribe = "<mailto:unsubscribe@news.example>";
    REQUIRE(!ScanMessage(news).Has("government-impersonation"));
    // "Cia." is a company, not the CIA.
    ScanInput company = Letter("vendas@souza.example", "Pedido", "Obrigado pelo pedido.");
    company.fromName = "Souza & Cia";
    REQUIRE(!ScanMessage(company).Has("government-impersonation"));
}

// ---------------------------------------------------------------------------
// Settings > Spam/scam warnings
// ---------------------------------------------------------------------------
TEST(a_kind_of_warning_switched_off_is_not_reported) {
    ScanInput in = Letter("anna.k.tova@gmail.com", "Where are you my dear?",
        "I go to a Dating site and find a profile of a person who believes in love. My "
        "name is Anna, I'm 31 years old. I hope you like my photos. Write back!",
        { "IMG_942.jpg" });
    REQUIRE(ScanMessage(in).Has("romance-scam"));
    in.options.romance = false;
    const ThreatReport off = ScanMessage(in);
    REQUIRE(!off.Has("romance-scam"));
    REQUIRE(off.level == ThreatLevel::Clean);         // its points went with it
    REQUIRE_EQ(off.score, 0);

    ScanInput crypto = Letter("news@example.test", "Markets", "Bitcoin rose today.");
    crypto.options.cryptoCaution = false;
    REQUIRE(!ScanMessage(crypto).Has("crypto-content"));
    crypto.options = ThreatScanOptions{};
    crypto.options.phishing = false;                  // another kind: no effect here
    REQUIRE(ScanMessage(crypto).Has("crypto-content"));
}

TEST(every_finding_belongs_to_a_switch) {
    ThreatScanOptions none;
    none.phishing = none.romance = none.advanceFee = none.government = false;
    none.cryptoScams = none.cryptoCaution = none.attachments = none.spamFlag = false;
    for (const char* code : { "spam-flag", "auth-failure", "brand-impersonation",
                              "sender-domain-lookalike", "borrowed-brand-pictures",
                              "link-userinfo", "link-ip-host", "link-punycode",
                              "link-nonascii-host", "link-shortener", "link-target-mismatch",
                              "link-brand-mismatch", "link-brand-lookalike",
                              "link-domain-lookalike",
                              "insecure-login-link", "many-foreign-domains",
                              "credential-request", "advance-fee-fraud", "reply-to-mismatch",
                              "reply-elsewhere", "attachment-disguised-executable",
                              "attachment-executable", "attachment-double-extension",
                              "romance-scam", "government-impersonation", "crypto-content",
                              "crypto-wallet-secret", "crypto-payment-demand",
                              "crypto-investment-lure" }) {
        REQUIRE(!FindingEnabled(none, code));
        REQUIRE(FindingEnabled(ThreatScanOptions{}, code));
    }
}

TEST(the_process_wide_options_reach_raw_scans) {
    const std::string raw =
        "From: news@example.test\r\nSubject: Markets\r\n"
        "Content-Type: text/plain\r\n\r\nBitcoin rose today.\r\n";
    REQUIRE(ScanRawMessage(raw).Has("crypto-content"));
    ThreatScanOptions quiet;
    quiet.cryptoCaution = false;
    SetThreatScanOptions(quiet);
    const bool reported = ScanRawMessage(raw).Has("crypto-content");
    SetThreatScanOptions(ThreatScanOptions{});       // back for the other tests
    REQUIRE(!reported);
    REQUIRE(GetThreatScanOptions() == ThreatScanOptions{});
}

// ---------------------------------------------------------------------------
// Look-alike letters of another script, and look-alike link targets
// ---------------------------------------------------------------------------
TEST(punycode_labels_are_decoded) {
    // "pаypal" with a Cyrillic "а" (U+0430), as it travels: xn--pypal-4ve.
    REQUIRE_EQ(DomainToUnicode("xn--pypal-4ve.com"), std::string("p\xD0\xB0ypal.com"));
    REQUIRE_EQ(DomainToUnicode("mail.xn--mnchen-3ya.de"), std::string("mail.m\xC3\xBCnchen.de"));
    REQUIRE_EQ(DomainToUnicode("example.com"), std::string("example.com"));
    REQUIRE_EQ(DomainToUnicode("xn--$$$.com"), std::string("xn--$$$.com"));   // kept as written
}

TEST(a_brand_in_lookalike_letters_is_a_homograph) {
    // A brand's own domain in Cyrillic letters, as punycode and as text.
    const DomainLookalike paypal = BrandImitatedByDomain("xn--pypal-4ve.com");
    REQUIRE(paypal.brand != nullptr);
    REQUIRE_EQ(paypal.brand->id, std::string("paypal"));
    REQUIRE(paypal.kind == LookalikeKind::Homograph);
    REQUIRE_EQ(paypal.letters, std::string("Cyrillic"));
    REQUIRE_EQ(paypal.unicode, std::string("p\xD0\xB0ypal.com"));
    REQUIRE_EQ(Imitated("\xD0\xB0pple.com"), std::string("apple"));          // "аpple.com"
    REQUIRE_EQ(Imitated("xn--pple-43d.com"), std::string("apple"));
    REQUIRE_EQ(Imitated("xn--facebok-fjg.net"), std::string("facebook"));      // "faceboоk" + "net"
    // The bare name under another suffix, in foreign letters, is a claim too.
    REQUIRE(BrandImitatedByDomain("p\xD0\xB0ypal.xyz").kind == LookalikeKind::Homograph);
    // Greek and accented letters.
    REQUIRE_EQ(Imitated("\xCE\xBFpenai.com"), std::string("openai"));       // Greek omicron
    REQUIRE_EQ(BrandImitatedByDomain("\xCE\xBFpenai.com").letters, std::string("Greek"));
    REQUIRE_EQ(Imitated("amaz\xC3\xB6n-login.com"), std::string("amazon")); // "amazön"
    // Real words in other scripts are not imitations.
    for (const char* d : { "xn--mnchen-3ya.de", "m\xC3\xBCnchen.de", "xn--80adxhks.xn--p1ai",
                           "\xE6\x9D\xB1\xE4\xBA\xAC.jp", "b\xC3\xBC" "cher.de" })
        REQUIRE_EQ(Imitated(d), std::string());
    // Nor is a mailbox provider's name, which is no brand.
    REQUIRE_EQ(Imitated("gm\xD0\xB0il.com"), std::string());
}

TEST(a_homograph_sender_is_a_scam) {
    const ThreatReport r = ScanMessage(Letter("PayPal <service@xn--pypal-4ve.com>",
        "Your account", "Please confirm your details."));
    REQUIRE(r.Has("sender-domain-lookalike"));
    REQUIRE(r.level == ThreatLevel::Scam);
    REQUIRE(r.Summary().find("Cyrillic") != std::string::npos);
    REQUIRE(r.Summary().find("p\xD0\xB0ypal.com") != std::string::npos);
}

TEST(a_link_to_a_lookalike_domain_is_flagged) {
    const ThreatReport r = ScanMessage(Html("news@club.example",
        "<p>Your friend tagged you.</p>"
        "<a href=\"https://faceebook-login.com/photo\">See the photo</a>"));
    REQUIRE(r.Has("link-domain-lookalike"));
    REQUIRE(r.level == ThreatLevel::Scam);
    REQUIRE(r.Summary().find("See the photo") != std::string::npos);
    REQUIRE(r.Summary().find("faceebook-login.com") != std::string::npos);
    REQUIRE(ScanMessage(Html("news@club.example",
        "<a href=\"https://xn--pypal-4ve.com/\">Pay</a>")).Has("link-domain-lookalike"));
    REQUIRE(ScanMessage(Html("news@club.example",
        "<a href=\"https://paypal-secure-login.com/\">Log in</a>")).Has("link-domain-lookalike"));
}

TEST(links_to_brands_and_ordinary_sites_are_not_lookalikes) {
    const ThreatReport r = ScanMessage(Html("news@club.example",
        "<a href=\"https://www.facebook.com/club\">Facebook</a>"
        "<a href=\"https://cdn.discordapp.com/x.png\">Discord</a>"
        "<a href=\"https://www.zoomcare.com/\">Book a visit</a>"
        "<a href=\"https://applewood-estates.com/\">Homes</a>"
        "<a href=\"https://www.redditmail.com/x\">Digest</a>"
        "<a href=\"https://www.m\xC3\xBCnchen.de/\">M\xC3\xBCnchen</a>"));
    REQUIRE(!r.Has("link-domain-lookalike"));
}
