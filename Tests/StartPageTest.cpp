// Tests/StartPageTest.cpp
// UltraCanvasStartPage without a window: the parts are laid out top to
// bottom in the order they are added, the preferred height grows by what
// each part takes, the recent entries become buttons that hand back the
// whole entry, the action is as wide as the column, a section sits at the
// margin at the height asked for, and the status line takes the theme's
// error colour for an error and the muted one otherwise. Headless.
// Version: 1.0.0
// Author: UltraCanvas Framework
#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasStartPage.h"
#include "UltraCanvasTheme.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace UltraCanvas;

static int g_checks = 0;
static int g_failures = 0;

static void Check(bool condition, const std::string& what) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

template <typename T>
static std::vector<std::shared_ptr<T>> ChildrenOf(const std::shared_ptr<UltraCanvasContainer>& page) {
    std::vector<std::shared_ptr<T>> found;
    for (const auto& child : page->GetChildren()) {
        if (auto typed = std::dynamic_pointer_cast<T>(child)) found.push_back(typed);
    }
    return found;
}

static std::shared_ptr<UltraCanvasLabel> LabelWithText(const std::shared_ptr<UltraCanvasContainer>& page, const std::string& text) {
    for (const auto& label : ChildrenOf<UltraCanvasLabel>(page)) {
        if (label->GetText() == text) return label;
    }
    return nullptr;
}

int main() {
    std::printf("An empty page\n");
    auto page = std::make_shared<UltraCanvasStartPage>("start", 620);
    page->SetTitle("UltraPress");
    page->SetHint("No site is open yet.");
    Check(page->Width() == 620 && page->ContentWidth() == 620 - 2 * UltraCanvasStartPage::Margin(),
          "the page is as wide as asked, the column two margins narrower");
    const int empty = page->PreferredHeight();
    Check(empty > 2 * UltraCanvasStartPage::Margin() && static_cast<int>(page->GetHeight()) == empty,
          "the title and the hint alone have a height, and the page is that tall");
    Check(LabelWithText(page, "UltraPress") != nullptr && LabelWithText(page, "No site is open yet.") != nullptr,
          "the title and the hint are labels on the page");
    Check(LabelWithText(page, "UltraPress")->GetX() == UltraCanvasStartPage::Margin() &&
              LabelWithText(page, "No site is open yet.")->GetY() > LabelWithText(page, "UltraPress")->GetY(),
          "the title at the margin, the hint under it");

    std::printf("The recent entries\n");
    page->SetRecent("Opened last", {}, [](const std::string&) {});
    Check(page->PreferredHeight() == empty && ChildrenOf<UltraCanvasButton>(page).empty(),
          "an empty list adds nothing, not even its heading");
    std::vector<std::string> entries;
    for (int i = 0; i < 7; ++i) entries.push_back("/home/erika/sites/site" + std::to_string(i));
    std::string picked;
    page->SetRecent("Opened last", entries, [&picked](const std::string& entry) { picked = entry; });
    const auto recent = ChildrenOf<UltraCanvasButton>(page);
    Check(recent.size() == 5, "five of seven entries become buttons");
    Check(LabelWithText(page, "Opened last") != nullptr, "under their heading");
    Check(recent.size() == 5 && recent[0]->GetTooltip() == entries[0] &&
              recent[0]->GetText().find("site0") == 0 && recent[0]->GetText().find(entries[0]) != std::string::npos,
          "a button shows the last path part and the whole entry, the whole entry as its tooltip");
    Check(recent.size() == 5 && static_cast<int>(recent[0]->GetWidth()) == page->ContentWidth() &&
              recent[4]->GetY() > recent[0]->GetY(),
          "the buttons are as wide as the column, one under the other");
    if (recent.size() == 5 && recent[3]->onClick) recent[3]->onClick();
    Check(picked == entries[3], "a click hands back the whole entry: " + picked);
    const int afterRecent = page->PreferredHeight();
    Check(afterRecent > empty, "the page grew for them");

    std::printf("The action\n");
    bool opened = false;
    auto action = page->AddAction("Open a site folder\xE2\x80\xA6", [&opened]() { opened = true; },
                                  "A folder with a site.json in it.");
    Check(action != nullptr && static_cast<int>(action->GetWidth()) == page->ContentWidth() &&
              action->GetX() == UltraCanvasStartPage::Margin() && action->GetY() >= afterRecent - UltraCanvasStartPage::Margin(),
          "the action is a button as wide as the column, under the recent entries");
    Check(action != nullptr && action->GetStyle().fontWeight == FontWeight::Bold, "the primary action is bold");
    if (action && action->onClick) action->onClick();
    Check(opened, "and its click reaches the callback");
    Check(LabelWithText(page, "A folder with a site.json in it.") != nullptr, "the note is a label under it");
    const int afterAction = page->PreferredHeight();
    Check(afterAction > afterRecent, "the page grew for the action and its note");
    auto plain = page->AddAction("Something else", []() {}, "", false);
    Check(plain != nullptr && plain->GetStyle().fontWeight != FontWeight::Bold &&
              page->PreferredHeight() - afterAction < afterAction - afterRecent,
          "a secondary action without a note is not bold and takes less room");
    const int beforeSection = page->PreferredHeight();

    std::printf("A section\n");
    auto section = page->AddSection("A new site", 90);
    Check(section != nullptr && section->GetX() == UltraCanvasStartPage::Margin() &&
              static_cast<int>(section->GetWidth()) == page->ContentWidth() && static_cast<int>(section->GetHeight()) == 90,
          "the section is a container at the margin, as wide as the column, as tall as asked");
    Check(LabelWithText(page, "A new site") != nullptr && section != nullptr &&
              LabelWithText(page, "A new site")->GetY() < section->GetY(),
          "its heading sits above it");
    Check(page->PreferredHeight() >= beforeSection + 90 + 24, "the page grew by the section and its heading");
    section->AddChild(CreateLabel("field", 0, 0, 100, 20, "Name"));
    Check(section->GetChildren().size() == 1, "the application's fields go into it");

    std::printf("The status line\n");
    const int beforeStatus = page->PreferredHeight();
    auto status = page->AddStatusLine();
    Check(status != nullptr && status->GetText().empty() && page->PreferredHeight() > beforeStatus,
          "the status line is an empty label that takes room");
    page->Say("The folder could not be read.", true);
    Check(status != nullptr && status->GetText() == "The folder could not be read." &&
              status->GetStyle().textColor == UltraCanvasTheme::Current().roles.danger && !status->GetFollowTheme(),
          "an error is said in the theme's error colour");
    page->Say("Creating the site\xE2\x80\xA6", false);
    Check(status != nullptr && status->GetText() == "Creating the site\xE2\x80\xA6" && status->GetFollowTheme(),
          "a plain message follows the theme again");
    Check(static_cast<int>(page->GetHeight()) == page->PreferredHeight(), "the page is always exactly its preferred height");

    std::printf("Said before any status line\n");
    auto late = std::make_shared<UltraCanvasStartPage>("late", 400);
    const int before = late->PreferredHeight();
    late->Say("Too early", true);
    Check(late->PreferredHeight() > before && ChildrenOf<UltraCanvasLabel>(late).size() == 3,
          "Say without a status line adds one");

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
