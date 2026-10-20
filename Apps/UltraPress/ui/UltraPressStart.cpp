// Apps/UltraPress/ui/UltraPressStart.cpp
// See UltraPressStart.h.
// Version: 0.8.5 - BuildPage builds the page without a window
// Version: 0.8.4 - on the framework's UltraCanvasStartPage
// Version: 0.8.3 - a recent site's button names its whole path in a tooltip
// Version: 0.8.2 - the sites opened last, offered first
// Version: 0.8.1
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraPressStart.h"
#include "UltraPressTheme.h"

#include "UltraPressModel.h"
#include "UltraPressPublish.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasLabel.h"
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
const int kRow = 34;

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

std::shared_ptr<UltraCanvasWindow> StartWindow::Build(const std::string& hint, const std::string& suggestion,
                                                      const std::vector<std::string>& recent) {
    // The page first, the window at the height it needs.
    BuildPage(hint, suggestion, recent);
    WindowConfig config;
    config.title = "UltraPress " ULTRAPRESS_VERSION;
    config.width = page_->Width();
    config.height = page_->PreferredHeight();
    config.backgroundColor = UltraCanvasTheme::Current().roles.panel;
    window_ = CreateWindow(config);
    if (!window_) return nullptr;
    window_->AddChild(page_);
    return window_;
}

std::shared_ptr<UltraCanvasStartPage> StartWindow::BuildPage(const std::string& hint, const std::string& suggestion,
                                                             const std::vector<std::string>& recent) {
    suggestion_ = suggestion;
    page_ = std::make_shared<UltraCanvasStartPage>("Start", kWidth);
    page_->SetTitle("UltraPress");
    page_->SetHint(hint);
    page_->SetRecent("Opened last", recent, [this](const std::string& dir) {
        if (HoldsSite(dir)) {
            HandOver(dir);
            return;
        }
        // Gone since: said here, and dropped from the list for next time.
        Say("\"" + dir + "\" holds no site any more.", true);
        if (onSiteGone) onSiteGone(dir);
    });
    page_->AddAction("Open a site folder\xE2\x80\xA6", [this]() { OpenExisting(); },
                     "A folder with a site.json - one made by UltraPress, or by `ultrapress new`.");

    // ---- a new site: its name, and the folder it goes into ----
    auto form = page_->AddSection("A new site", kRow + 10 + kRow + 8 + 40);
    const int width = page_->ContentWidth();
    auto nameCaption = CreateLabel("StartNameCaption", 0, 7, 90, 22, "Site name");
    nameCaption->SetFontSize(13);
    FollowTheme(nameCaption, LabelVariant::Body);
    form->AddChild(nameCaption);
    name_ = CreateTextInput("StartName", 96, 0, width - 96, kRow);
    name_->SetFontSize(14);
    name_->SetFollowTheme(true);
    name_->SetText(suggestion.empty() ? std::string("My site") : NameFromFolder(suggestion));
    form->AddChild(name_);
    auto create = CreateButton("StartCreate", 0, kRow + 10, 300, kRow, "Create it in a folder\xE2\x80\xA6");
    create->SetFontSize(14);
    create->SetBorder(1, Colors::Transparent);
    create->SetCornerRadius(6);
    FollowTheme(create, ButtonVariant::Standard);
    create->onClick = [this]() { CreateNew(); };
    form->AddChild(create);
    auto createNote = CreateLabel("StartCreateNote", 0, kRow + 10 + kRow + 8, width, 40,
                                  "The starter site - a front page, an about page, a contact form - goes into the "
                                  "folder you choose; an empty or a new folder, so nothing there is overwritten.");
    createNote->SetFontSize(12);
    createNote->SetWrap(TextWrap::WrapWord);
    FollowTheme(createNote, LabelVariant::Muted);
    form->AddChild(createNote);
    page_->AddStatusLine();
    return page_;
}

void StartWindow::Close() {
    if (window_) window_->PerformClose();
}

std::string StartWindow::SiteName() const { return name_ ? name_->GetText() : std::string(); }

void StartWindow::Say(const std::string& text, bool error) {
    if (page_) page_->Say(text, error);
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
