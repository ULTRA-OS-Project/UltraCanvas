// Tests/UltraMail/test_signature.cpp
// The account signature: stored with the account (and kept when the account
// is saved again), and put into new messages, replies and forwards - plain
// text below a "-- " line, HTML as formatting that makes the draft a
// formatted one. Headless.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "UltraMailComposer.h"
#include "UltraMailLocalStore.h"
#include "UltraMailRichComposer.h"
#include "UltraMailSignature.h"

#include "UltraCanvasRichDocument.h"

#include <string>
#include <vector>

using namespace UltraMail;
using UltraCanvas::RichBlockType;
using UltraCanvas::UCRichDocument;

namespace {

std::string BlockText(const UCRichDocument& doc, size_t index) {
    return UCRichDocument::ConcatenateRunText(doc.blocks[index].runs);
}

Signature TextSignature(const std::string& text) {
    Signature s;
    s.kind = SignatureKind::Text;
    s.text = text;
    return s;
}

Signature HtmlSignature(const std::string& html) {
    Signature s;
    s.kind = SignatureKind::Html;
    s.html = html;
    return s;
}

SourceMessage PlainSource() {
    SourceMessage s;
    s.messageId = "<orig@example.com>";
    s.fromName = "Anna"; s.fromAddr = "anna@example.com";
    s.to = {"erika@example.com"};
    s.subject = "Notes";
    s.body = "Hi Erika,\n\nHere are the notes.";
    s.date = "Tue, 14 Jan 2026 14:02:00 +0000";
    return s;
}

// A 40x40 PNG's first bytes as a data: URI: enough to be recognised and
// sized (a picture of 2 pixels or less is dropped as a tracking pixel).
std::string LogoDataUri() {
    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A,
                                0, 0, 0, 13, 'I', 'H', 'D', 'R'};
    for (uint32_t v : {40u, 40u})
        for (int shift : {24, 16, 8, 0}) png.push_back(static_cast<uint8_t>(v >> shift));
    png.insert(png.end(), {8, 2, 0, 0, 0, 0, 0, 0, 0});
    static const char* kAlphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out = "data:image/png;base64,";
    for (size_t i = 0; i < png.size(); i += 3) {
        const uint32_t n = (uint32_t(png[i]) << 16)
                         | (i + 1 < png.size() ? uint32_t(png[i + 1]) << 8 : 0)
                         | (i + 2 < png.size() ? uint32_t(png[i + 2]) : 0);
        out += kAlphabet[(n >> 18) & 63];
        out += kAlphabet[(n >> 12) & 63];
        out += i + 1 < png.size() ? kAlphabet[(n >> 6) & 63] : '=';
        out += i + 2 < png.size() ? kAlphabet[n & 63] : '=';
    }
    return out;
}

} // namespace

TEST(signature_kind_roundtrips_through_its_string) {
    REQUIRE(SignatureKindFromString(ToString(SignatureKind::Off)) == SignatureKind::Off);
    REQUIRE(SignatureKindFromString(ToString(SignatureKind::Text)) == SignatureKind::Text);
    REQUIRE(SignatureKindFromString(ToString(SignatureKind::Html)) == SignatureKind::Html);
    REQUIRE(SignatureKindFromString("bogus") == SignatureKind::Off);
}

TEST(signature_is_active_only_with_content_of_its_kind) {
    Signature s;
    REQUIRE(!s.IsActive());
    s.text = "Erika";                      // text kept, but the kind is None
    REQUIRE(!s.IsActive());
    s.kind = SignatureKind::Text;
    REQUIRE(s.IsActive());
    s.kind = SignatureKind::Html;          // no HTML written yet
    REQUIRE(!s.IsActive());
    s.html = "<p>Erika</p>";
    REQUIRE(s.IsActive());
    REQUIRE(!TextSignature(" \n\t").IsActive());
}

TEST(signature_is_stored_with_the_account_and_survives_an_upsert) {
    LocalStore store;
    REQUIRE(store.Open("umtest-signature", ":memory:").success);
    Account a; a.accountId = "erika"; a.email = "erika@example.com";
    a.shortName = "erika"; a.displayName = "Erika";
    REQUIRE(store.UpsertAccount(a).success);

    std::vector<Account> accounts;
    REQUIRE(store.ListAccounts(accounts).success);
    REQUIRE_EQ(accounts.size(), std::size_t(1));
    REQUIRE(accounts[0].signature == Signature{});   // none, on replies too

    Signature sig = HtmlSignature("<p><b>Erika</b> Example</p>");
    sig.text = "Erika Example";
    sig.onReplies = false;
    REQUIRE(store.SetAccountSignature("erika", sig).success);
    REQUIRE(store.ListAccounts(accounts).success);
    REQUIRE(accounts[0].signature == sig);

    // Saving the account again - its servers edited, or the address added a
    // second time - keeps the signature.
    Account again; again.accountId = "erika"; again.email = "erika@example.com";
    again.shortName = "erika"; again.displayName = "Erika E.";
    REQUIRE(store.UpsertAccount(again).success);
    REQUIRE(store.ListAccounts(accounts).success);
    REQUIRE_EQ(accounts[0].displayName, std::string("Erika E."));
    REQUIRE(accounts[0].signature == sig);
}

TEST(no_signature_leaves_the_draft_alone) {
    Draft d = Composer::NewMessage("Erika", "erika@example.com");
    REQUIRE(!ApplySignature(d, Signature{}, DraftPurpose::NewMessage));
    REQUIRE(d.body.empty());
    REQUIRE(d.richBody == nullptr);
}

TEST(text_signature_goes_below_the_separator_in_a_new_message) {
    Draft d = Composer::NewMessage("Erika", "erika@example.com");
    REQUIRE(ApplySignature(d, TextSignature("Erika Example\r\nACME Ltd.\n\n"),
                           DraftPurpose::NewMessage));
    // An empty line to write on, a blank line, the separator, the signature.
    REQUIRE_EQ(d.body, std::string("\n\n-- \nErika Example\nACME Ltd.\n"));
    REQUIRE(d.richBody == nullptr);
    REQUIRE(!d.bodyIsHtml);
}

TEST(text_signature_with_its_own_separator_is_not_doubled) {
    Draft d = Composer::NewMessage("Erika", "erika@example.com");
    REQUIRE(ApplySignature(d, TextSignature("--\nErika"), DraftPurpose::NewMessage));
    REQUIRE_EQ(d.body, std::string("\n\n-- \nErika\n"));
}

TEST(text_signature_sits_between_the_answer_and_the_quote) {
    Draft d = Composer::Reply(PlainSource(), "Erika", "erika@example.com", false);
    REQUIRE(ApplySignature(d, TextSignature("Erika"), DraftPurpose::ReplyOrForward));
    REQUIRE(d.body.rfind("\n\n-- \nErika\n\nOn Tue, 14 Jan 2026", 0) == 0);
    REQUIRE(d.body.find("> Here are the notes.") != std::string::npos);
}

TEST(signature_can_be_kept_off_replies_and_forwards) {
    Signature sig = TextSignature("Erika");
    sig.onReplies = false;
    Draft reply = Composer::Reply(PlainSource(), "Erika", "erika@example.com", false);
    const std::string before = reply.body;
    REQUIRE(!ApplySignature(reply, sig, DraftPurpose::ReplyOrForward));
    REQUIRE_EQ(reply.body, before);
    Draft fresh = Composer::NewMessage("Erika", "erika@example.com");
    REQUIRE(ApplySignature(fresh, sig, DraftPurpose::NewMessage));
}

TEST(text_signature_in_a_formatted_reply_is_a_paragraph_above_the_quote) {
    SourceMessage src = PlainSource();
    src.bodyHtml = "<p>Hi <b>Erika</b></p>";
    Draft d = Composer::Reply(src, "Erika", "erika@example.com", false);
    REQUIRE(MakeRichReply(d, src));
    REQUIRE(ApplySignature(d, TextSignature("Erika\nACME"), DraftPurpose::ReplyOrForward));
    const UCRichDocument& doc = *d.richBody;
    REQUIRE(doc.blocks[0].runs.empty());                       // the answer
    REQUIRE_EQ(BlockText(doc, 1), std::string("-- \nErika\nACME"));  // three lines
    REQUIRE_EQ(doc.blocks[1].runs.size(), std::size_t(3));
    REQUIRE(doc.blocks[1].runs[1].lineBreakBefore);
    REQUIRE_EQ(doc.blocks[1].quoteLevel, 0);
    REQUIRE(BlockText(doc, 2).find("wrote:") != std::string::npos);
}

TEST(html_signature_makes_a_new_message_formatted) {
    Draft d = Composer::NewMessage("Erika", "erika@example.com");
    REQUIRE(ApplySignature(d, HtmlSignature("<p><b>Erika</b> Example</p>"
                                            "<p><a href=\"https://acme.example\">ACME</a></p>"),
                           DraftPurpose::NewMessage));
    REQUIRE(d.richBody != nullptr);
    REQUIRE(d.bodyIsHtml);
    const UCRichDocument& doc = *d.richBody;
    REQUIRE(doc.blocks.size() >= 3);
    REQUIRE(doc.blocks[0].runs.empty());                  // written on
    REQUIRE_EQ(BlockText(doc, 1), std::string("Erika Example"));
    REQUIRE(doc.blocks[1].runs[0].bold);
    REQUIRE(doc.blocks[1].spaceBeforePt >= 12.0f);
    REQUIRE_EQ(doc.blocks[2].runs[0].linkTarget, std::string("https://acme.example"));

    // Sent as HTML with a plain-text version.
    RenderRichBody(d);
    REQUIRE(d.body.find("<b>Erika</b>") != std::string::npos
            || d.body.find("<strong>Erika</strong>") != std::string::npos);
    REQUIRE(d.textBody.find("Erika Example") != std::string::npos);
}

TEST(html_signature_turns_a_plain_reply_into_a_formatted_one) {
    Draft d = Composer::Reply(PlainSource(), "Erika", "erika@example.com", false);
    REQUIRE(ApplySignature(d, HtmlSignature("<p><i>Erika</i></p>"), DraftPurpose::ReplyOrForward));
    REQUIRE(d.richBody != nullptr);
    const UCRichDocument& doc = *d.richBody;
    REQUIRE(doc.blocks[0].runs.empty());
    REQUIRE_EQ(BlockText(doc, 1), std::string("Erika"));
    REQUIRE(doc.blocks[1].runs[0].italic);
    // The attribution, then the original as a quote: "> " became the level.
    REQUIRE(BlockText(doc, 2).find("Anna wrote:") != std::string::npos);
    REQUIRE_EQ(doc.blocks[2].quoteLevel, 0);
    REQUIRE_EQ(BlockText(doc, 3), std::string("Hi Erika,"));
    REQUIRE_EQ(doc.blocks[3].quoteLevel, 1);
    REQUIRE_EQ(BlockText(doc, 4), std::string("Here are the notes."));
    REQUIRE_EQ(doc.blocks[4].quoteLevel, 1);
}

TEST(html_signature_pictures_go_into_the_draft_media) {
    Draft d = Composer::NewMessage("Erika", "erika@example.com");
    const std::string html = "<p><img src=\"" + LogoDataUri() + "\" alt=\"logo\"> ACME</p>";
    REQUIRE(ApplySignature(d, HtmlSignature(html), DraftPurpose::NewMessage));
    const UCRichDocument& doc = *d.richBody;
    REQUIRE_EQ(doc.media.size(), std::size_t(1));
    bool found = false;
    for (const auto& block : doc.blocks) {
        if (block.mediaIndex == 0) found = true;
        for (const auto& run : block.runs) if (run.mediaIndex == 0) found = true;
    }
    REQUIRE(found);
    // Sent as a cid: part, like any picture in a formatted message.
    RenderRichBody(d);
    REQUIRE_EQ(d.inlineParts.size(), std::size_t(1));
    REQUIRE(d.body.find("cid:") != std::string::npos);
}

TEST(plain_body_becomes_paragraphs_and_quote_levels) {
    auto doc = PlainBodyToRichDocument("\n\nOn Monday, Anna wrote:\n> Hi,\n> line two\n>\n>> older\n");
    REQUIRE_EQ(doc->blocks.size(), std::size_t(4));
    REQUIRE(doc->blocks[0].runs.empty());
    REQUIRE_EQ(BlockText(*doc, 1), std::string("On Monday, Anna wrote:"));
    REQUIRE_EQ(BlockText(*doc, 2), std::string("Hi,\nline two"));
    REQUIRE(doc->blocks[2].runs[1].lineBreakBefore);
    REQUIRE_EQ(doc->blocks[2].quoteLevel, 1);
    REQUIRE_EQ(BlockText(*doc, 3), std::string("older"));
    REQUIRE_EQ(doc->blocks[3].quoteLevel, 2);
    REQUIRE_EQ(PlainBodyToRichDocument("")->blocks.size(), std::size_t(1));
}

TEST(signature_html_roundtrips_through_the_editor_document) {
    auto doc = SignatureDocument("<p><b>Erika</b></p>");
    REQUIRE_EQ(BlockText(*doc, 0), std::string("Erika"));
    const std::string html = SignatureHtmlFrom(*doc);
    REQUIRE(!html.empty());
    auto again = SignatureDocument(html);
    REQUIRE(again->blocks[0].runs[0].bold);
    // An editor left empty stores no HTML at all.
    REQUIRE(SignatureDocument("")->blocks.size() == 1);
    REQUIRE(SignatureHtmlFrom(*SignatureDocument("")).empty());
    REQUIRE(SignatureHtmlFrom(*SignatureDocument("<p>  </p>")).empty());
}

TEST(signature_is_described_by_its_first_line) {
    REQUIRE_EQ(DescribeSignature(Signature{}), std::string("None"));
    REQUIRE_EQ(DescribeSignature(TextSignature("\n-- \nErika Example\nACME")),
               std::string("Plain text \xC2\xB7 Erika Example"));
    REQUIRE_EQ(DescribeSignature(HtmlSignature("<p><b>Erika</b></p>")),
               std::string("HTML \xC2\xB7 Erika"));
    const std::string longLine(60, 'x');
    const std::string described = DescribeSignature(TextSignature(longLine));
    REQUIRE_EQ(described, "Plain text \xC2\xB7 " + std::string(40, 'x') + "\xE2\x80\xA6");
}
