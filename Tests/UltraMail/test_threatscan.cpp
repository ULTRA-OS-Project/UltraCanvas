// Tests/UltraMail/test_threatscan.cpp
// Exercises the content scan: link extraction out of HTML and plain text, and
// each rule that can raise a message to Suspicious or Scam — a link whose text
// names one site while its href goes to another, a brand claimed by a sender
// that does not own the domain, userinfo hiding the real host, an IP-literal
// target, an executable attachment — plus the equally important negative
// cases, where an ordinary newsletter and an ordinary personal mail stay out
// of the way.
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
