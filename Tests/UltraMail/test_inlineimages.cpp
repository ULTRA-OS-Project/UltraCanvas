// Tests/UltraMail/test_inlineimages.cpp
// Embedded (cid: / Content-Location / data:) vs remote images in HTML mail.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "test_framework.h"

#include "UltraMailInlineImages.h"

#include <string>
#include <vector>

using namespace UltraMail;

namespace {
// multipart/related: an HTML part and a PNG referenced by Content-ID, plus a
// part addressed by Content-Location.
const char* kRelated =
    "From: a@x.example\r\n"
    "Subject: pictures\r\n"
    "MIME-Version: 1.0\r\n"
    "Content-Type: multipart/related; boundary=\"B\"\r\n"
    "\r\n"
    "--B\r\n"
    "Content-Type: text/html; charset=utf-8\r\n"
    "\r\n"
    "<p><img src=\"cid:logo@x.example\"><img src=\"https://x.example/pixel.gif\"></p>\r\n"
    "--B\r\n"
    "Content-Type: image/png\r\n"
    "Content-ID: <logo@x.example>\r\n"
    "Content-Transfer-Encoding: base64\r\n"
    "\r\n"
    "iVBORw0KGgo=\r\n"
    "--B\r\n"
    "Content-Type: image/gif\r\n"
    "Content-Location: http://x.example/banner.gif\r\n"
    "Content-Transfer-Encoding: base64\r\n"
    "\r\n"
    "R0lGODlh\r\n"
    "--B--\r\n";
}

TEST(inline_images_collects_content_id_and_location) {
    InlineImages images = CollectInlineImages(kRelated);
    REQUIRE_EQ(images.byContentId.count("logo@x.example"), (size_t)1);
    const std::vector<uint8_t> png = ResolveEmbeddedImage("cid:logo@x.example", images);
    REQUIRE_EQ(png.size(), (size_t)8);
    REQUIRE(png[1] == 'P' && png[2] == 'N' && png[3] == 'G');
    // Content-ID matched regardless of case, and percent-encoded.
    REQUIRE_EQ(ResolveEmbeddedImage("cid:LOGO%40x.example", images).size(), (size_t)8);
    REQUIRE(ResolveEmbeddedImage("cid:missing@x.example", images).empty());
    // A part's Content-Location is embedded even though it looks remote.
    REQUIRE(ClassifyImageSource("http://x.example/banner.gif", images) == ImageSource::Embedded);
    REQUIRE_EQ(ResolveEmbeddedImage("http://x.example/banner.gif", images).size(), (size_t)6);
}

TEST(inline_images_classifies_sources) {
    InlineImages none;
    REQUIRE(ClassifyImageSource("cid:a@b", none) == ImageSource::Embedded);
    REQUIRE(ClassifyImageSource(" data:image/png;base64,AAAA", none) == ImageSource::Embedded);
    REQUIRE(ClassifyImageSource("https://t.example/open.gif?id=1", none) == ImageSource::Remote);
    REQUIRE(ClassifyImageSource("HTTP://t.example/a.png", none) == ImageSource::Remote);
    REQUIRE(ClassifyImageSource("//cdn.example/a.png", none) == ImageSource::Remote);
    REQUIRE(ClassifyImageSource("file:///etc/passwd", none) == ImageSource::Other);
    REQUIRE(ClassifyImageSource("javascript:alert(1)", none) == ImageSource::Other);
    REQUIRE(ClassifyImageSource("images/a.png", none) == ImageSource::Other);
}

TEST(inline_images_decodes_data_uris) {
    InlineImages none;
    const std::vector<uint8_t> b64 = ResolveEmbeddedImage("data:image/gif;base64,R0lG ODlh", none);
    REQUIRE_EQ(std::string(b64.begin(), b64.end()), std::string("GIF89a"));
    const std::vector<uint8_t> plain = ResolveEmbeddedImage("data:image/svg+xml,%3Csvg%2F%3E", none);
    REQUIRE_EQ(std::string(plain.begin(), plain.end()), std::string("<svg/>"));
    REQUIRE(ResolveEmbeddedImage("data:nocomma", none).empty());
}
