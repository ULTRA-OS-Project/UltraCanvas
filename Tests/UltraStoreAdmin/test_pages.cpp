// Tests/UltraStoreAdmin/test_pages.cpp
// The frame of the pages the publish folder carries: the stylesheet
// inline, the mark from media/ with a dark variant, the landing page in
// it, the join and retired pages too, the marks copied beside the pages
// when the framework's media folder has them.
// Version: 0.1.1 - the join and retired pages
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "admin_helpers.h"

#include "Affiliates.h"
#include "PageFrame.h"

#include "UltraCanvasPathUtf8.h"

#include <filesystem>

using namespace UltraStoreAdmin;
using namespace admin_test;

TEST(the_frame_carries_the_stylesheet_and_the_mark) {
    const std::string page = FramePage("A & B", "Heading <1>", "<p>body</p>\n", "../../media/", "foot");
    REQUIRE(page.find("<title>A &amp; B - UltraStore</title>") != std::string::npos);
    REQUIRE(page.find("<h1>Heading &lt;1&gt;</h1>") != std::string::npos);
    REQUIRE(page.find("<main>") != std::string::npos && page.find("<p>body</p>") != std::string::npos);
    REQUIRE(page.find("<footer>foot</footer>") != std::string::npos);
    REQUIRE(page.find("../../media/ultraos-logo-light.png") != std::string::npos);
    REQUIRE(page.find("../../media/ultraos-logo-dark.png") != std::string::npos);
    REQUIRE(page.find("prefers-color-scheme: dark") != std::string::npos);
    REQUIRE(page.find("--accent:#2563eb") != std::string::npos);   // the store's colours, inline
    REQUIRE(page.find("<script") == std::string::npos);
    REQUIRE(page.find("http") == std::string::npos);                // nothing from anywhere else
}

TEST(the_landing_page_is_in_the_frame) {
    UltraStore::PromoCode code;
    code.code = "ANNA-20";
    code.kind = UltraStore::CodeKind::Affiliate;
    code.affiliate = "aff-anna";
    code.discountPercent = 20;
    const std::string page = CodeLandingPage(code, {HelloApp()}, "https://store.example/v1/");
    REQUIRE(page.find("<h1>Your UltraStore code</h1>") != std::string::npos);
    REQUIRE(page.find("<code>ANNA-20</code>") != std::string::npos);
    REQUIRE(page.find("class=\"button\" href=\"ultrastore://code/ANNA-20\"") != std::string::npos);
    REQUIRE(page.find("<footer>UltraStore, the ULTRA OS app store. https://store.example/v1</footer>") != std::string::npos);
    REQUIRE(page.find("../../media/ultraos-logo-light.png") != std::string::npos);
}

TEST(the_join_and_retired_pages_are_in_the_frame) {
    Affiliate anna;
    anna.id = "aff-anna";
    anna.name = "Anna & Co";
    anna.enabled = true;
    anna.contact = "anna@example.com";
    const std::string join = JoinPage(anna, "partners@store.example", "https://store.example/v1/");
    REQUIRE(join.find("<title>Become an UltraStore partner - UltraStore</title>") != std::string::npos);
    REQUIRE(join.find("<h1>Become an UltraStore partner</h1>") != std::string::npos);
    REQUIRE(join.find("<main>") != std::string::npos);
    REQUIRE(join.find("<code>aff-anna</code>") != std::string::npos);
    REQUIRE(join.find("<p class=\"muted\">Questions about the programme go to Anna &amp; Co: anna@example.com</p>") != std::string::npos);
    REQUIRE(join.find("<footer>UltraStore, the ULTRA OS app store. https://store.example/v1</footer>") != std::string::npos);
    // join/<affiliate>/ is two levels under the publish folder, like go/<CODE>/.
    REQUIRE(join.find("../../media/ultraos-logo-light.png") != std::string::npos);
    REQUIRE(join.find("<script") == std::string::npos);
    anna.enabled = false;
    const std::string closed = JoinPage(anna, "", "https://store.example/v1");
    REQUIRE(closed.find("<h1>This invitation is closed</h1>") != std::string::npos);
    REQUIRE(closed.find("../../media/ultraos-logo-dark.png") != std::string::npos);
    const std::string retired = RetiredCodePage("anna-20", "https://store.example/v1");
    REQUIRE(retired.find("<h1>This code is no longer valid</h1>") != std::string::npos);
    REQUIRE(retired.find("<code>ANNA-20</code>") != std::string::npos);
    REQUIRE(retired.find("<footer>UltraStore, the ULTRA OS app store. https://store.example/v1</footer>") != std::string::npos);
    REQUIRE(retired.find("../../media/ultraos-logo-light.png") != std::string::npos);
}

TEST(the_marks_are_copied_beside_the_pages_when_the_framework_has_them) {
    const std::string dir = TempDir("marks");
    REQUIRE(CopyPageMarks(dir).Ok());
    // With the framework's media folder in reach the two pictures are
    // there; without it nothing is, and the pages show the alt text.
    namespace fs = std::filesystem;
    const bool light = fs::is_regular_file(UltraCanvas::PathFromUtf8(Join(dir, "media/ultraos-logo-light.png")));
    const bool dark = fs::is_regular_file(UltraCanvas::PathFromUtf8(Join(dir, "media/ultraos-logo-dark.png")));
    REQUIRE(light == dark);
    REQUIRE(CopyPageMarks(dir).Ok());   // a second time changes nothing
    RemoveDir(dir);
}
