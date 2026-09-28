// Tests/UltraMail/test_unsubscribe.cpp
// List-Unsubscribe / List-Unsubscribe-Post parsing (RFC 2369, RFC 8058).
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "test_framework.h"

#include "UltraMailUnsubscribe.h"

#include <string>

using namespace UltraMail;

TEST(unsubscribe_one_click_https_and_mailto) {
    UnsubscribeInfo u = ParseListUnsubscribe(
        "<mailto:leave@news.example.com?subject=unsubscribe%20me&body=bye>, "
        "<https://news.example.com/u?id=42>",
        "List-Unsubscribe=One-Click");
    REQUIRE(u.Any());
    REQUIRE_EQ(u.oneClickUrl, std::string("https://news.example.com/u?id=42"));
    REQUIRE_EQ(u.webUrl, std::string("https://news.example.com/u?id=42"));
    REQUIRE_EQ(u.mailtoAddress, std::string("leave@news.example.com"));
    REQUIRE_EQ(u.mailtoSubject, std::string("unsubscribe me"));
    REQUIRE_EQ(u.mailtoBody, std::string("bye"));
}

TEST(unsubscribe_without_post_header_is_not_one_click) {
    UnsubscribeInfo u = ParseListUnsubscribe("<https://x.example/u>", "");
    REQUIRE(u.oneClickUrl.empty());
    REQUIRE_EQ(u.webUrl, std::string("https://x.example/u"));
    // Plain http never takes the one-click POST.
    u = ParseListUnsubscribe("<http://x.example/u>", "List-Unsubscribe=One-Click");
    REQUIRE(u.oneClickUrl.empty());
    REQUIRE_EQ(u.webUrl, std::string("http://x.example/u"));
}

TEST(unsubscribe_mailto_only_and_empty) {
    UnsubscribeInfo u = ParseListUnsubscribe("<mailto:list-off@x.example>", "");
    REQUIRE_EQ(u.mailtoAddress, std::string("list-off@x.example"));
    REQUIRE(u.webUrl.empty());
    REQUIRE(u.Any());
    REQUIRE(!ParseListUnsubscribe("", "").Any());
    REQUIRE(!ParseListUnsubscribe("not a list header", "").Any());
}

TEST(unsubscribe_read_from_raw_message_with_folding) {
    const std::string raw =
        "From: News <news@x.example>\r\n"
        "To: erika@example.com\r\n"
        "Subject: Weekly\r\n"
        "List-Unsubscribe: <https://x.example/unsub?\r\n"
        " t=abc>, <mailto:off@x.example>\r\n"
        "List-Unsubscribe-Post: List-Unsubscribe=One-Click\r\n"
        "\r\n"
        "Hello\r\n";
    UnsubscribeInfo u = ReadUnsubscribe(raw);
    REQUIRE_EQ(u.oneClickUrl, std::string("https://x.example/unsub?t=abc"));
    REQUIRE_EQ(u.mailtoAddress, std::string("off@x.example"));
    REQUIRE(!ReadUnsubscribe("").Any());
}
