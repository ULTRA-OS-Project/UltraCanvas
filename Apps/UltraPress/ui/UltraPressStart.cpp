// Apps/UltraPress/ui/UltraPressStart.cpp
// See UltraPressStart.h.
// Version: 0.8.1
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraPressStart.h"
#include "UltraPressTheme.h"

#include "UltraPressModel.h"
#include "UltraPressPublish.h"

#include "UltraCanvasFileLoader.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasTheme.h"

#include <filesystem>

#ifndef ULTRAPRESS_VERSION
#error "ULTRAPRESS_VERSION comes from the first line of Docs/UltraPress/CHANGELOG.md through CMake; never a literal here"
#endif

using namespace UltraCanvas;

namespace UltraPress {

namespace {

const int kWidth = 620;
const int kHeight = 470;
const int kMargin = 24;
const int kRow = 34;
const char* kFont = "Ubuntu";

// Whether a folder holds a site: the one file every site has.
bool HoldsSite(const std::string& dir) {
    std::error_code ec;
    return std::filesystem::is_regular_file(PathFromUtf8(dir) / "site.json", ec);
}

// The site's name from its folder's name, for a new site: "my-shop" is "My shop".
std::string NameFromFolder(const std::string& dir) {
    std::string name = PathToUtf8(PathFromUtf8(dir).filename());
    if (name.empty()) return "My site";
    for (char& c : name) if (c == '-' || c == '_') c = ' ';
    if (name[0] >= 'a' && name[0] <= 'z') name[0] = static_cast<char>(name[0] - 'a' + 'A');
    return name;
}

} // namespace

std::shared_ptr<UltraCanvasWindow> StartWindow::Build(const std::string& hint, const std::string& suggestion) {
    suggestion_ = suggestion;
    const ThemeRoles& roles = UltraCanvasTheme::Current().roles;

    WindowConfig config;
    config.title = "UltraPress " ULTRAPRESS_VERSION;
    config.width = kWidth;
    config.height = kHeight;
    config.backgroundColor = roles.panel;
    window_ = CreateWindow(config);
    if (!window_) return nullptr;

    int y = kMargin;
    auto title = CreateLabel("StartTitle", kMargin, y, kWidth - 2 * kMargin, 32, "UltraPress");
    title->SetFont(kFont, 22, FontWeight::Bold);
    FollowTheme(title, LabelVariant::Heading);
    window_->AddChild(title);
    y += 40;

    // Why this window and not a site: a user who typed a folder should know
    // whether it was wrong or simply new.
    // Three lines' room: a folder's full path, and what was wrong with it.
    auto why = CreateLabel("StartHint", kMargin, y, kWidth - 2 * kMargin, 70, hint);
    why->SetFont(kFont, 14);
    why->SetWrap(TextWrap::WrapWord);
    FollowTheme(why, LabelVariant::Body);
    window_->AddChild(why);
    y += 84;

    // ---- an existing site ----
    auto open = CreateButton("StartOpen", kMargin, y, 300, kRow, "Open a site folder\xE2\x80\xA6");
    open->SetFont(kFont, 14, FontWeight::Bold);
    open->SetBorder(0, Colors::Transparent);
    open->SetCornerRadius(6);
    FollowTheme(open, ButtonVariant::Primary);
    open->onClick = [this]() { OpenExisting(); };
    window_->AddChild(open);
    y += kRow + 8;
    auto openNote = CreateLabel("StartOpenNote", kMargin, y, kWidth - 2 * kMargin, 22,
                                "A folder with a site.json - one made by UltraPress, or by `ultrapress new`.");
    openNote->SetFont(kFont, 12);
    FollowTheme(openNote, LabelVariant::Muted);
    window_->AddChild(openNote);
    y += 44;

    // ---- a new site ----
    auto newTitle = CreateLabel("StartNewTitle", kMargin, y, kWidth - 2 * kMargin, 24, "A new site");
    newTitle->SetFont(kFont, 15, FontWeight::Bold);
    FollowTheme(newTitle, LabelVariant::Heading);
    window_->AddChild(newTitle);
    y += 30;
    auto nameCaption = CreateLabel("StartNameCaption", kMargin, y + 7, 90, 22, "Site name");
    nameCaption->SetFont(kFont, 13);
    FollowTheme(nameCaption, LabelVariant::Body);
    window_->AddChild(nameCaption);
    name_ = CreateTextInput("StartName", kMargin + 96, y, kWidth - 2 * kMargin - 96, kRow);
    name_->SetFontSize(14);
    name_->SetFollowTheme(true);
    name_->SetText(suggestion.empty() ? std::string("My site") : NameFromFolder(suggestion));
    window_->AddChild(name_);
    y += kRow + 10;
    auto create = CreateButton("StartCreate", kMargin, y, 300, kRow, "Create it in a folder\xE2\x80\xA6");
    create->SetFont(kFont, 14);
    create->SetBorder(1, Colors::Transparent);
    create->SetCornerRadius(6);
    FollowTheme(create, ButtonVariant::Standard);
    create->onClick = [this]() { CreateNew(); };
    window_->AddChild(create);
    y += kRow + 8;
    auto createNote = CreateLabel("StartCreateNote", kMargin, y, kWidth - 2 * kMargin, 40,
                                  "The starter site - a front page, an about page, a contact form - goes into the "
                                  "folder you choose; an empty or a new folder, so nothing there is overwritten.");
    createNote->SetFont(kFont, 12);
    createNote->SetWrap(TextWrap::WrapWord);
    FollowTheme(createNote, LabelVariant::Muted);
    window_->AddChild(createNote);

    status_ = CreateLabel("StartStatus", kMargin, kHeight - kMargin - 44, kWidth - 2 * kMargin, 44, "");
    status_->SetFont(kFont, 13);
    status_->SetWrap(TextWrap::WrapWord);
    FollowTheme(status_, LabelVariant::Muted);
    window_->AddChild(status_);
    return window_;
}

void StartWindow::Close() {
    if (window_) window_->PerformClose();
}

void StartWindow::Say(const std::string& text, bool error) {
    if (!status_) return;
    if (error) {
        status_->SetFollowTheme(false);
        status_->SetTextColor(UltraCanvasTheme::Current().roles.danger);
    } else {
        FollowTheme(status_, LabelVariant::Muted);
    }
    status_->SetText(text);
}

void StartWindow::OpenExisting() {
    FileDialogOptions opts;
    opts.SetTitle("Open a site folder").SetRegisterAsRecent(false).SetParentWindow(window_.get());
    if (!suggestion_.empty()) opts.SetInitialDirectory(suggestion_);
    UltraCanvasFileLoader::SelectFolderDialog(opts, [this](DialogResult result, const std::string& dir) {
        if (result != DialogResult::OK || dir.empty()) return;
        if (!HoldsSite(dir)) {
            // Said here, with the way on: the form below makes a site there.
            Say("\"" + dir + "\" holds no site (no site.json). A new site can be created in it below.", true);
            suggestion_ = dir;
            if (name_) name_->SetText(NameFromFolder(dir));
            return;
        }
        HandOver(dir);
    });
}

void StartWindow::CreateNew() {
    FileDialogOptions opts;
    opts.SetTitle("The folder for the new site").SetRegisterAsRecent(false).SetParentWindow(window_.get());
    if (!suggestion_.empty()) opts.SetInitialDirectory(suggestion_);
    UltraCanvasFileLoader::SelectFolderDialog(opts, [this](DialogResult result, const std::string& dir) {
        if (result != DialogResult::OK || dir.empty()) return;
        if (HoldsSite(dir)) {
            Say("\"" + dir + "\" already holds a site; open it instead.", true);
            return;
        }
        std::string name = name_ ? name_->GetText() : std::string();
        if (name.empty()) name = NameFromFolder(dir);
        Say("Creating the site\xE2\x80\xA6", false);
        const Site site = CreateStarterSite(name, "https://example.org");
        std::vector<std::string> errors;
        if (!SaveSite(dir, site, errors)) {
            Say(errors.empty() ? "The site could not be written to " + dir : errors.front(), true);
            return;
        }
        HandOver(dir);
    });
}

void StartWindow::HandOver(const std::string& siteDir) {
    const std::string error = onSiteChosen ? onSiteChosen(siteDir) : std::string("nobody opens the site");
    if (!error.empty()) {
        Say(error, true);
        return;
    }
    // Only now: the editor's window is open, so closing this one does not
    // leave the application without a window.
    Close();
}

} // namespace UltraPress
