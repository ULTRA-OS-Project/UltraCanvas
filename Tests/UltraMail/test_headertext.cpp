// Tests/UltraMail/test_headertext.cpp
// DisplayHeader: header text as the message list and the reading pane show it.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "UltraMailHeaderText.h"
#include "UltraMailComposer.h"
#include "UltraMailRichComposer.h"

#include <string>

using namespace UltraMail;

TEST(display_header_decodes_html_entities_senders_put_in_names) {
    // Lexware writes the recipient's name as HTML.
    REQUIRE_EQ(DisplayHeader("\"Stefan Fr&ouml;hling\" <accounting@example.org>"),
               std::string("\"Stefan Fr\xC3\xB6hling\" <accounting@example.org>"));
    REQUIRE_EQ(DisplayHeader("M&uuml;ller &amp; S&ouml;hne &#8211; Rechnung &#x20AC;"),
               std::string("M\xC3\xBCller & S\xC3\xB6hne \xE2\x80\x93 Rechnung \xE2\x82\xAC"));
}

TEST(display_header_keeps_plain_ampersands_and_unknown_entities) {
    REQUIRE_EQ(DisplayHeader("AT&T bill"), std::string("AT&T bill"));
    REQUIRE_EQ(DisplayHeader("Tom & Jerry; again"), std::string("Tom & Jerry; again"));
    REQUIRE_EQ(DisplayHeader("&notanentity;"), std::string("&notanentity;"));
}

TEST(display_header_still_decodes_encoded_words) {
    REQUIRE_EQ(DisplayHeader("=?UTF-8?Q?Gr=C3=BC=C3=9Fe?="), std::string("Gr\xC3\xBC\xC3\x9F" "e"));
}

// A reply or forward is built from the reading pane's decoded fields
// (MessagePreview::Show), so the quote reads "Fröhling", not "Fr&ouml;hling" -
// in plain text and in the HTML a formatted reply sends.
TEST(reply_and_forward_quote_the_decoded_names) {
    const std::string name = "Stefan Fr\xC3\xB6hling";
    SourceMessage src;
    src.fromName = DisplayHeader("Stefan Fr&ouml;hling");
    src.fromAddr = "stefan@example.org";
    src.to       = {DisplayHeader("\"Stefan Fr&ouml;hling\" <accounting@example.org>")};
    src.subject  = DisplayHeader("Rechnung f&uuml;r M&auml;rz");
    src.body     = "Hallo";
    src.date     = "2 Oct 2026";

    const Draft reply = Composer::Reply(src, "Me", "me@example.org", /*replyAll=*/true);
    REQUIRE(reply.body.find(name + " wrote:") != std::string::npos);
    REQUIRE_EQ(reply.subject, std::string("Re: Rechnung f\xC3\xBCr M\xC3\xA4rz"));
    bool toDecoded = false;
    for (const auto& a : reply.to) if (a.find(name) != std::string::npos) toDecoded = true;
    REQUIRE(toDecoded);

    const Draft fwd = Composer::Forward(src, "Me", "me@example.org");
    REQUIRE(fwd.body.find("From: " + name + " <stefan@example.org>") != std::string::npos);
    REQUIRE(fwd.body.find("&ouml;") == std::string::npos);

    src.bodyHtml = "<p>Hallo</p>";
    Draft rich = Composer::Reply(src, "Me", "me@example.org", /*replyAll=*/false);
    REQUIRE(MakeRichReply(rich, src));
    RenderRichBody(rich);
    REQUIRE(rich.textBody.find(name + " wrote:") != std::string::npos);
    REQUIRE(rich.body.find("&amp;ouml;") == std::string::npos);
    REQUIRE(rich.body.find("&ouml;") == std::string::npos);
}
