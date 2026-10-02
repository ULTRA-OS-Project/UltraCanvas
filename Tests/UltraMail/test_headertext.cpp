// Tests/UltraMail/test_headertext.cpp
// DisplayHeader: header text as the message list and the reading pane show it.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "UltraMailHeaderText.h"

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
