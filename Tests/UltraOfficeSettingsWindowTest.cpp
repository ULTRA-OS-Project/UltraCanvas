// Tests/UltraOfficeSettingsWindowTest.cpp
// UltraOffice's view group and Settings window, in its real window:
//   - each kind of tab has its view group at the toolbar's right end, which
//     stays whichever command tab is chosen; in a narrow window the command
//     tab's tools go behind their chevron and the view group stays whole;
//   - the document's fits: page width, whole page, and the text width - the
//     page is the chosen share of the view's width, and stays so when the
//     window is resized; the share and the fit are saved;
//   - the Settings window opens on each of its pages; a value changed there
//     is saved, applies to the open document at once (the author name, the
//     zoom, the gridlines), reads back when the window is opened again, and a
//     new tab starts with it.
//
// Opens a window: skips without a display, fails instead when
// ULTRACANVAS_TEST_DISPLAY_REQUIRED is set (build.yml's framework window
// tests). The settings folder is a temporary one (XDG_CONFIG_HOME).
//
// Usage: UltraOfficeSettingsWindowTest
// Exit code is the number of failed checks.

#include "DisplayTestSupport.h"
#include "UltraOfficeSettingsDialog.h"
#include "UltraOfficeWindow.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasRichTextEdit.h"
#include "UltraCanvasSettingsPageKit.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasSpreadsheet.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasToolbar.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace UltraCanvas;
using UltraOffice::OfficeKind;
namespace fs = std::filesystem;

namespace {

int failures = 0;

void Check(bool ok, const std::string& what) {
    if (!ok) ++failures;
    std::printf("%s: %s\n", ok ? "  ok" : "FAIL", what.c_str());
}

template <typename T>
T* Find(UltraCanvasContainer* in, const std::string& id) {
    return in ? dynamic_cast<T*>(in->FindChildById(id)) : nullptr;
}

// The window's own loop, a turn at a time, until `done` or `ms` have passed.
void PumpUntil(UltraCanvasApplication& app, UltraCanvasWindow& window, const std::function<bool()>& done, int ms = 2000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (!done() && std::chrono::steady_clock::now() < deadline) {
        app.RunOnce();
        window.UpdateAndRender();
    }
    window.UpdateAndRender();
    window.UpdateAndRender();
}

// The page's width on screen, and the width a fit measures against (the
// editor's less its padding and the scrollbar it always keeps room for).
float PageOnScreen(const UltraCanvasRichTextEdit& edit) {
    return edit.GetEffectivePageSetup().widthPt * (96.0f / 72.0f) * edit.GetZoom();
}
float ViewWidth(const UltraCanvasRichTextEdit& edit) {
    return edit.GetWidth() - 2.0f * edit.GetStyle().padding - edit.GetStyle().scrollbarWidth;
}

// The view group sits at the right end of the toolbar, its tools all on it.
void CheckViewGroup(UltraCanvasWindow& window, const std::string& kind, const std::string& barId,
                    const std::vector<std::string>& tools) {
    auto* bar = Find<UltraCanvasToolbar>(&window, barId);
    Check(bar && bar->IsVisible(), kind + ": the view group is there (" + barId + ")");
    if (!bar) return;
    const Rect2Df at = bar->GetBoundsInWindow();
    Check(at.width > 100.0f && std::fabs(at.x + at.width - static_cast<float>(window.GetWidth())) < 4.0f,
          kind + ": at the toolbar's right end (" + std::to_string(at.x) + " + " + std::to_string(at.width) + " of " +
              std::to_string(window.GetWidth()) + ")");
    bool all = true;
    std::string order;
    float lastX = -1.0f;
    for (const std::string& id : tools) {
        auto* tool = Find<UltraCanvasUIElement>(bar, id);
        const bool shown = tool && tool->IsVisible() && tool->GetWidth() > 0.0f;
        all = all && shown && tool->GetBoundsInWindow().x > lastX;
        if (tool) lastX = tool->GetBoundsInWindow().x;
        order += (order.empty() ? "" : " | ") + id;
    }
    Check(all, kind + ": every tool shown, in order: " + order);
}


// Every command tab of the tab open (document `docId`) shows all its tools
// beside the view group at this width: nothing behind the chevron.
void CheckCommandTabsFit(UltraCanvasApplication& app, UltraCanvasWindow& window, int docId, const std::string& kind) {
    const std::string id = "office-doc-" + std::to_string(docId);
    auto* slot = Find<UltraCanvasContainer>(&window, id + "-command-bar");
    for (int i = 0;; ++i) {
        auto* tab = Find<UltraCanvasButton>(&window, id + "-tab-" + std::to_string(i));
        if (!tab || !tab->onToggle) break;
        tab->onToggle(true);
        PumpUntil(app, window, [] { return false; }, 300);
        UltraCanvasToolbar* bar = nullptr;
        if (slot)
            for (auto& child : slot->GetChildren())
                if ((bar = dynamic_cast<UltraCanvasToolbar*>(child.get()))) break;
        std::string behind, widths;
        float used = 0.0f;
        if (bar) {
            for (const std::string& item : bar->GetOverflowItemIds()) behind += " " + item;
            for (auto& item : bar->GetItems()) {
                widths += " " + item->GetIdentifier() + "=" + std::to_string(static_cast<int>(item->GetWidth()));
                used += item->GetWidth();
            }
        }
        Check(bar && bar->GetOverflowItemIds().empty(),
              kind + ", command tab " + tab->GetText() + ": every tool on the bar at " + std::to_string(window.GetWidth()) +
                  " px" + (behind.empty() ? std::string() : " (behind the chevron:" + behind + "; bar " +
                                                                std::to_string(bar ? bar->GetWidth() : 0.0f) + " wide, items" +
                                                                widths + ")"));
    }
    if (auto* first = Find<UltraCanvasButton>(&window, id + "-tab-0"); first && first->onToggle) first->onToggle(true);
    PumpUntil(app, window, [] { return false; }, 300);
}
} // namespace

int main() {
    if (!std::getenv("DISPLAY") || !*std::getenv("DISPLAY")) {
        if (DisplayTest::DisplayRequired()) {
            std::printf("FAIL: no DISPLAY, and ULTRACANVAS_TEST_DISPLAY_REQUIRED is set\n");
            return 1;
        }
        std::printf("SKIP: no DISPLAY\n");
        return 0;
    }
    std::error_code ec;
    const fs::path settingsFolder = fs::temp_directory_path() / "uc-ultraoffice-settings-window-test";
    fs::remove_all(settingsFolder, ec);
    fs::create_directories(settingsFolder, ec);
    setenv("XDG_CONFIG_HOME", PathToUtf8(settingsFolder).c_str(), 1);   // path-string-ok: test's own ASCII temp folder
    setenv("ULTRACANVAS_HOST_APPEARANCE", "light", 1);

    UltraCanvasApplication app;
    if (!app.Initialize("UltraOfficeSettingsWindowTest")) {
        std::printf("%s: the application could not be initialised\n", DisplayTest::DisplayRequired() ? "FAIL" : "SKIP");
        return DisplayTest::DisplayRequired() ? 1 : 0;
    }
    auto window = std::make_shared<UltraOffice::UltraOfficeWindow>();
    WindowConfig config;
    config.title = "UltraOffice";
    config.width = 1280;
    config.height = 820;
    config.resizable = true;
    if (!window->Create(config)) {
        std::printf("%s: the window could not be created\n", DisplayTest::DisplayRequired() ? "FAIL" : "SKIP");
        return DisplayTest::DisplayRequired() ? 1 : 0;
    }
    // The command tabs and their bars: the toolbar layout (Settings, Tools:
    // toolbar at the top); the panels' layout, the default, is checked at
    // the end and in UltraOfficePanelsWindowTest.
    window->Settings().SetToolLayout(UltraOffice::ToolLayout::Toolbar);
    window->Initialize({});
    window->Show();
    // A short timer, so a turn of the loop never waits long for the display.
    const TimerId tick = app.StartTimer(40, true, [](TimerId) {});
    PumpUntil(app, *window, [] { return false; }, 300);

    std::printf("=== The view group of each kind ===\n");
    window->NewDocument(OfficeKind::Document);
    PumpUntil(app, *window, [] { return false; }, 300);
    if (auto* strip = Find<UltraCanvasTabbedContainer>(window.get(), "office-tabs")) {
        Check(strip->GetActiveTab() != 0, "a new document's tab is the active one");
        if (strip->onNewTabRequest) strip->onNewTabRequest();
        PumpUntil(app, *window, [] { return false; }, 100);
        Check(strip->GetActiveTab() == 0, "the tab strip's + goes to the Start tab");
        strip->SetActiveTab(strip->GetTabCount() - 1);
        PumpUntil(app, *window, [] { return false; }, 100);
    } else {
        Check(false, "the tab strip is found");
    }
    CheckViewGroup(*window, "document", "office-document-view",
                   {"doc-navigator", "doc-pageview", "doc-fitwidth", "doc-fitpage", "doc-fitshare", "doc-share", "doc-zoomout",
                    "doc-zoom", "doc-zoomin"});
    Check(!Find<UltraCanvasButton>(window.get(), "office-doc-1-tab-4"), "the document has no View tab any more");
    // Another command tab: the view group stays where it is.
    if (auto* insert = Find<UltraCanvasButton>(window.get(), "office-doc-1-tab-1"); insert && insert->onToggle) {
        insert->onToggle(true);
        PumpUntil(app, *window, [] { return false; }, 300);
        CheckViewGroup(*window, "document, Insert tab", "office-document-view", {"doc-navigator", "doc-zoomin"});
        if (auto* home = Find<UltraCanvasButton>(window.get(), "office-doc-1-tab-0"); home && home->onToggle) home->onToggle(true);
    } else {
        Check(false, "the Insert tab's button");
    }

    auto* paneOne = Find<UltraCanvasContainer>(window.get(), "office-doc-1");
    auto* edit = Find<UltraCanvasRichTextEdit>(paneOne, "office-document");
    Check(edit != nullptr, "the document's editor");

    std::printf("\n=== The fits ===\n");
    auto press = [&](const std::string& id) {
        auto* tool = Find<UltraCanvasButton>(window.get(), id);
        if (!tool || !tool->onToggle) { Check(false, "the tool " + id); return; }
        tool->onToggle(true);
        PumpUntil(app, *window, [] { return false; }, 300);
    };
    using Fit = UltraCanvasRichTextEdit::ZoomFit;
    if (edit) {
        press("doc-fitpage");
        Check(edit->GetZoomFit() == Fit::WholePage, "Whole page fits the whole page");
        Check(window->Settings().GetDocumentFit() == UltraOffice::DocumentFit::WholePage, "...and is remembered");
        press("doc-fitwidth");
        Check(edit->GetZoomFit() == Fit::PageWidth, "Page width fits the width");
        const float pageWidthZoom = edit->GetZoom();
        press("doc-fitshare");
        Check(edit->GetZoomFit() == Fit::WidthShare, "Text width is the width share's fit");
        Check(std::fabs(PageOnScreen(*edit) - 0.6f * ViewWidth(*edit)) < 2.0f,
              "...the page is 60 % of the view's width (" + std::to_string(PageOnScreen(*edit)) + " of " +
                  std::to_string(ViewWidth(*edit)) + ")");
        Check(edit->GetZoom() < pageWidthZoom, "...smaller than page width");
        auto* fitShare = Find<UltraCanvasButton>(window.get(), "doc-fitshare");
        auto* fitWidth = Find<UltraCanvasButton>(window.get(), "doc-fitwidth");
        Check(fitShare && fitShare->IsPressed() && fitWidth && !fitWidth->IsPressed(), "...its tool is the one pressed");

        // The list beside it: 40 %.
        auto* share = Find<UltraCanvasDropdown>(window.get(), "doc-share");
        Check(share && share->GetItemCount() == 7, "the share's list: 40 to 90 % and Other...");
        if (share) {
            share->SetSelectedIndex(0, true);
            PumpUntil(app, *window, [] { return false; }, 300);
            Check(std::fabs(PageOnScreen(*edit) - 0.4f * ViewWidth(*edit)) < 2.0f,
                  "40 % chosen: the page is 40 % of the width (" + std::to_string(PageOnScreen(*edit)) + ")");
            Check(std::fabs(window->Settings().GetWidthShare() - 0.4f) < 1e-3f &&
                      window->Settings().GetDocumentFit() == UltraOffice::DocumentFit::WidthShare,
                  "...saved: the share and the fit");
        }

        std::printf("\n=== A narrow window ===\n");
        window->SetWindowSize(820, 700);
        PumpUntil(app, *window, [&] { return window->GetWidth() == 820; });
        Check(window->GetWidth() == 820, "the window is 820 wide");
        CheckViewGroup(*window, "document, narrow", "office-document-view",
                       {"doc-navigator", "doc-pageview", "doc-fitwidth", "doc-fitpage", "doc-fitshare", "doc-share",
                        "doc-zoomout", "doc-zoom", "doc-zoomin"});
        auto* home = Find<UltraCanvasToolbar>(window.get(), "office-document-home");
        const size_t narrowBehind = home ? home->GetOverflowItemIds().size() : 0;
        Check(home && narrowBehind > 0,
              "the Home tools that do not fit are behind the chevron (" +
                  std::to_string(home ? home->GetOverflowItemIds().size() : 0) + ")");
        Check(std::fabs(PageOnScreen(*edit) - 0.4f * ViewWidth(*edit)) < 2.0f,
              "the page is still 40 % of the narrower view (" + std::to_string(PageOnScreen(*edit)) + " of " +
                  std::to_string(ViewWidth(*edit)) + ")");
        window->SetWindowSize(1280, 820);
        PumpUntil(app, *window, [&] { return window->GetWidth() == 1280; });
        std::string behind;
        for (const std::string& id : home ? home->GetOverflowItemIds() : std::vector<std::string>()) behind += " " + id;
        auto* viewBar = Find<UltraCanvasToolbar>(window.get(), "office-document-view");
        Check(home && home->GetOverflowItemIds().size() < narrowBehind,
              "wide again: more Home tools back on the bar (behind the chevron:" + behind + "; the view group " +
                  std::to_string(viewBar ? viewBar->GetWidth() : 0.0f) + " wide)");
        CheckCommandTabsFit(app, *window, 1, "document");
    }

    window->NewDocument(OfficeKind::Spreadsheet);
    PumpUntil(app, *window, [] { return false; }, 300);
    CheckViewGroup(*window, "spreadsheet", "office-sheet-view",
                   {"sheet-freeze", "sheet-unfreeze", "sheet-gridlines", "sheet-zoomout", "sheet-zoom", "sheet-zoomin"});
    CheckCommandTabsFit(app, *window, 2, "spreadsheet");
    window->NewDocument(OfficeKind::Presentation);
    PumpUntil(app, *window, [] { return false; }, 300);
    CheckViewGroup(*window, "presentation", "office-slides-view",
                   {"slides-view-normal", "slides-view-sorter", "slides-view-notes", "slides-fit", "slides-zoomout",
                    "slides-zoom", "slides-zoomin"});
    CheckCommandTabsFit(app, *window, 3, "presentation");

    std::printf("\n=== The Settings window ===\n");
    window->ShowSettings();
    PumpUntil(app, *window, [] { return false; }, 300);
    auto* dialog = UltraOffice::GetSettingsDialog();
    Check(dialog && dialog->window, "File > Settings opens it");
    if (dialog && dialog->window) {
        const std::vector<std::string> pages = {UltraOffice::kSettingsGeneral, UltraOffice::kSettingsSaving,
                                                UltraOffice::kSettingsDocuments, UltraOffice::kSettingsSpreadsheets,
                                                UltraOffice::kSettingsPresentations};
        for (const std::string& page : pages) {
            dialog->SelectPage(page);
            dialog->window->UpdateAndRender();
            Check(dialog->shownPage == page && dialog->pages.count(page) && dialog->pages[page]->IsVisible(), "its page " + page);
        }
        auto* author = Find<UltraCanvasTextInput>(dialog->window.get(), "office-set-author");
        auto* gridlines = Find<UltraCanvasCheckbox>(dialog->window.get(), "office-set-gridlines");
        auto* fit = Find<UltraCanvasDropdown>(dialog->window.get(), "office-set-fit");
        auto* recovery = Find<UltraCanvasCheckbox>(dialog->window.get(), "office-set-recovery");
        Check(author && gridlines && fit && recovery, "its controls");
        Check(fit && fit->GetSelectedIndex() == 2, "the zoom reads Text width, as chosen in the toolbar");
        if (author && gridlines && fit) {
            // Typed: the text input reports each change.
            author->SetText("Grace Hopper");
            if (author->onTextChanged) author->onTextChanged(author->GetText());
            if (author->onFocusLost) author->onFocusLost();
            gridlines->SetChecked(false);
            fit->SetSelectedIndex(1, true);   // Whole page
        }
        PumpUntil(app, *window, [] { return false; }, 300);
        Check(edit && edit->GetCommentAuthor() == "Grace Hopper", "the open document signs with the new name at once");
        Check(edit && edit->GetZoomFit() == Fit::WholePage, "...and takes the zoom at once");
        auto* sheet = Find<UltraCanvasSpreadsheet>(window.get(), "office-spreadsheet");
        Check(sheet && !sheet->GetShowGridlines(), "the open spreadsheet hides its gridlines at once");

        dialog->window->Close();
        PumpUntil(app, *window, [] { return UltraOffice::GetSettingsDialog() == nullptr; });
        Check(UltraOffice::GetSettingsDialog() == nullptr, "Close closes it");

        // On disk.
        UltraCanvasAppSettings stored = UltraCanvasAppSettings::AtFile(window->Settings().Store().File());
        stored.Load();
        const UltraOffice::OfficeSettings onDisk(stored);
        Check(onDisk.GetAuthorName() == "Grace Hopper" && !onDisk.GetShowGridlines() &&
                  onDisk.GetDocumentFit() == UltraOffice::DocumentFit::WholePage,
              "saved in " + PathToUtf8(PathFromUtf8(stored.File()).filename()));

        // Opened again: the values read back.
        window->ShowSettings();
        dialog = UltraOffice::GetSettingsDialog();
        auto* authorAgain = dialog ? Find<UltraCanvasTextInput>(dialog->window.get(), "office-set-author") : nullptr;
        auto* gridlinesAgain = dialog ? Find<UltraCanvasCheckbox>(dialog->window.get(), "office-set-gridlines") : nullptr;
        auto* fitAgain = dialog ? Find<UltraCanvasDropdown>(dialog->window.get(), "office-set-fit") : nullptr;
        Check(authorAgain && authorAgain->GetText() == "Grace Hopper", "opened again: the name is there");
        Check(gridlinesAgain && !gridlinesAgain->IsChecked(), "...the gridlines off");
        Check(fitAgain && fitAgain->GetSelectedIndex() == 1, "...the zoom Whole page");
        if (dialog) dialog->window->Close();
        PumpUntil(app, *window, [] { return UltraOffice::GetSettingsDialog() == nullptr; });
    }

    std::printf("\n=== A new tab takes the settings ===\n");
    window->NewDocument(OfficeKind::Document);
    PumpUntil(app, *window, [] { return false; }, 300);
    auto* paneFour = Find<UltraCanvasContainer>(window.get(), "office-doc-4");
    auto* editFour = Find<UltraCanvasRichTextEdit>(paneFour, "office-document");
    Check(editFour && editFour->GetZoomFit() == Fit::WholePage, "a new document opens at Whole page");
    Check(editFour && editFour->GetCommentAuthor() == "Grace Hopper", "...and signs with the name");
    window->NewDocument(OfficeKind::Spreadsheet);
    PumpUntil(app, *window, [] { return false; }, 300);
    auto* paneFive = Find<UltraCanvasContainer>(window.get(), "office-doc-5");
    auto* sheetFive = Find<UltraCanvasSpreadsheet>(paneFive, "office-spreadsheet");
    Check(sheetFive && !sheetFive->GetShowGridlines(), "a new spreadsheet has no gridlines");

    std::printf("\n=== The view group with the tool panels ===\n");
    window->Settings().SetToolLayout(UltraOffice::ToolLayout::Panels);
    window->ApplySettings();
    PumpUntil(app, *window, [] { return false; }, 300);
    if (auto* strip = Find<UltraCanvasTabbedContainer>(window.get(), "office-tabs")) {
        const std::vector<std::pair<std::string, std::vector<std::string>>> views = {
            {"office-document-view", {"doc-navigator", "doc-pageview", "doc-zoomin"}},
            {"office-sheet-view", {"sheet-freeze", "sheet-zoomin"}},
            {"office-slides-view", {"slides-view-normal", "slides-zoomin"}}};
        for (int tab = 1; tab <= 3; ++tab) {
            strip->SetActiveTab(tab);
            PumpUntil(app, *window, [] { return false; }, 300);
            const auto& view = views[static_cast<size_t>(tab - 1)];
            CheckViewGroup(*window, "panels, tab " + std::to_string(tab), view.first, view.second);
            Check(Find<UltraCanvasContainer>(window.get(), "office-doc-" + std::to_string(tab) + "-panels") != nullptr,
                  "panels, tab " + std::to_string(tab) + ": the tool panels at the left");
        }
    }

    app.StopTimer(tick);
    window->Close();
    fs::remove_all(settingsFolder, ec);
    std::printf("\n%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures;
}
