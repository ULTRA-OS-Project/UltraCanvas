// Tests/UltraOfficePanelsWindowTest.cpp
// UltraOffice's tool panels in its real window
// (Apps/UltraOffice/UltraOfficePanels.h, Docs/Research/UltraOfficeToolPanelsProposal.md):
//   - the Start tab says once where the tools went, with Settings beside it,
//     until it is dismissed - which a restart remembers;
//   - the panels at the left are the default layout;
//   - a value shows what applies: a paragraph with no spacing of its own the
//     view's, a heading its style's, a table on the view's grid its line;
//     "-" only for a selection of paragraphs that differ;
//   - the list style, the page's orientation, size, margins and columns,
//     the table's merge, split and cell shading and a slide's background
//     each set from their panel and read back;
//   - F6 on the tab strip draws its focus ring;
//   - the one top row holds File, Undo, Redo and Save, and the view group
//     last at its right end - no command tabs;
//   - every tool of the table that has a panel is in its panel (or is its
//     "…" button), for each kind;
//   - a value edited in a panel changes the document (a paragraph's space
//     before, a shape's horizontal position) and follows the caret and the
//     selection;
//   - the Table context panel comes when the caret goes into a table and
//     goes when it leaves; the Pivot table panel holds the pivot field list;
//   - a panel dragged by its header to another place is saved, with the
//     open panels, and both are back after the window is opened again;
//   - Settings, Tools: toolbar at the top gives the command tabs and their
//     bars again, every tool on its bar at 1280 px, and back;
//   - a window narrower than 1100 px folds the panels into the icon rail,
//     whose icon opens its panel as a flyout;
//   - F6 goes from the tab strip to the toolbar, the panels and the document.
//
// Opens a window: skips without a display, fails instead when
// ULTRACANVAS_TEST_DISPLAY_REQUIRED is set (build.yml's framework window
// tests). The settings folder is a temporary one (XDG_CONFIG_HOME).
// ULTRACANVAS_SCREENSHOT_DIR=<dir> writes the window as PPM files: each kind
// with a context panel, during a drag, after the restart with its order, in
// the rail at 1024 px with a flyout, and the Settings window's General page.
//
// Usage: UltraOfficePanelsWindowTest
// Exit code is the number of failed checks.

#include "DisplayTestSupport.h"
#include "UltraOfficePanels.h"
#include "UltraOfficeSettingsDialog.h"
#include "UltraOfficeWindow.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasColorSwatchBar.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasRichTextEdit.h"
#include "UltraCanvasSettingsPageKit.h"
#include "UltraCanvasSlideDeckEditor.h"
#include "UltraCanvasSlideEdit.h"
#include "UltraCanvasSpinner.h"
#include "UltraCanvasSpreadsheet.h"
#include "UltraCanvasSpreadsheetPivot.h"
#include "UltraCanvasSpreadsheetPivotPanel.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasToolPanelStack.h"
#include "UltraCanvasToolbar.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace UltraCanvas;
using UltraOffice::OfficeKind;
using UltraOffice::ToolPanels;
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

void PumpUntil(UltraCanvasApplication& app, UltraCanvasWindow& window, const std::function<bool()>& done, int ms = 2000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (!done() && std::chrono::steady_clock::now() < deadline) {
        app.RunOnce();
        window.UpdateAndRender();
    }
    window.UpdateAndRender();
    window.UpdateAndRender();
}

void Pump(UltraCanvasApplication& app, UltraCanvasWindow& window, int ms = 300) {
    PumpUntil(app, window, [] { return false; }, ms);
}

// The window as a PPM file (ULTRACANVAS_SCREENSHOT_DIR).
void Shoot(UltraCanvasWindowBase& window, const std::string& name) {
    const char* dir = std::getenv("ULTRACANVAS_SCREENSHOT_DIR");
    if (!dir || !*dir) return;
    window.UpdateAndRender();
    const int width = window.GetWidth(), height = window.GetHeight();
    const std::string path = std::string(dir) + "/" + name + ".ppm";
    std::ofstream out(PathFromUtf8(path), std::ios::binary);
    if (!out) return;
    out << "P6\n" << width << " " << height << "\n255\n";
    std::vector<unsigned char> row(static_cast<size_t>(width) * 3);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            Color px;
            if (!window.GetPixelColor(x, y, px)) px = Colors::Black;
            row[static_cast<size_t>(x) * 3 + 0] = px.r;
            row[static_cast<size_t>(x) * 3 + 1] = px.g;
            row[static_cast<size_t>(x) * 3 + 2] = px.b;
        }
        out.write(reinterpret_cast<const char*>(row.data()), static_cast<std::streamsize>(row.size()));
    }
    std::printf("   wrote %s\n", path.c_str());
}

struct Harness {
    UltraCanvasApplication& app;
    std::shared_ptr<UltraOffice::UltraOfficeWindow> window;

    void Mouse(UCEventType type, int x, int y, UCMouseButton button = UCMouseButton::Left) {
        UCEvent e;
        e.type = type;
        e.button = button;
        e.pointer = Point2Di(x, y);
        e.pointerWindow = Point2Di(x, y);
        e.pointerGlobal = Point2Di(x, y);
        e.targetWindow = window;
        e.nativeWindowHandle = window->GetNativeHandle();
        app.DispatchEvent(e);
    }
    void Key(UCKeys key, bool shift = false) {
        for (UCEventType type : {UCEventType::KeyDown, UCEventType::KeyUp}) {
            UCEvent e;
            e.type = type;
            e.virtualKey = key;
            e.shift = shift;
            e.targetWindow = window;
            e.nativeWindowHandle = window->GetNativeHandle();
            app.DispatchEvent(e);
        }
        Pump(app, *window, 100);
    }
    void Click(Point2Di at) {
        Mouse(UCEventType::MouseMove, at.x, at.y, UCMouseButton::NoneButton);
        Mouse(UCEventType::MouseDown, at.x, at.y);
        Mouse(UCEventType::MouseUp, at.x, at.y);
        Pump(app, *window, 150);
    }
    UltraCanvasToolPanelStack* Stack(int doc) {
        return Find<UltraCanvasToolPanelStack>(window.get(), "office-doc-" + std::to_string(doc) + "-panels");
    }
    Point2Di HeaderMiddle(UltraCanvasToolPanelStack& stack, const std::string& id) {
        auto box = stack.GetPanel(id);
        const Point2Df at = box->GetPositionInWindow();
        return Point2Di(static_cast<int>(at.x + box->GetWidth() * 0.4f),
                        static_cast<int>(at.y + box->GetTitleBarRect().height * 0.5f));
    }
    // Presses panel `id`'s header, moves it below `below`'s middle; `drop`
    // false keeps the button down (a screenshot of the drag).
    void DragBelow(UltraCanvasToolPanelStack& stack, const std::string& id, const std::string& below, bool drop = true) {
        const Point2Di from = HeaderMiddle(stack, id);
        auto target = stack.GetPanel(below);
        const int toY = static_cast<int>(target->GetPositionInWindow().y + target->GetHeight() * 0.75f);
        Mouse(UCEventType::MouseMove, from.x, from.y, UCMouseButton::NoneButton);
        Mouse(UCEventType::MouseDown, from.x, from.y);
        const int step = toY > from.y ? 12 : -12;
        for (int y = from.y; step > 0 ? y <= toY : y >= toY; y += step)
            Mouse(UCEventType::MouseMove, from.x, y, UCMouseButton::NoneButton);
        Mouse(UCEventType::MouseMove, from.x, toY, UCMouseButton::NoneButton);
        Pump(app, *window, 100);
        dragAt = Point2Di(from.x, toY);
        if (drop) Release();
    }
    // Lets go of a drag where it is.
    void Release() {
        Mouse(UCEventType::MouseUp, dragAt.x, dragAt.y);
        Pump(app, *window, 150);
    }
    Point2Di dragAt;
};

std::shared_ptr<UltraOffice::UltraOfficeWindow> OpenWindow(int width, int height) {
    auto window = std::make_shared<UltraOffice::UltraOfficeWindow>();
    WindowConfig config;
    config.title = "UltraOffice";
    config.width = width;
    config.height = height;
    config.resizable = true;
    if (!window->Create(config)) return nullptr;
    window->Initialize({});
    window->Show();
    return window;
}

// Each panel has its title and its rail icon; a panel's dialog is its "…" button.
void CheckEveryPanel(UltraOffice::UltraOfficeWindow& window, UltraCanvasToolPanelStack& stack, const std::string& kind) {
    ToolPanels* panels = window.GetToolPanels();
    Check(panels != nullptr, kind + ": the tab has its tool panels");
    if (!panels) return;
    for (const std::string& key : stack.GetOrder()) {
        auto box = stack.GetPanel(key);
        Check(box && !box->GetTitle().empty(), kind + ": the panel " + key + " has a title (" + (box ? box->GetTitle() : "") + ")");
        Check(stack.GetRailButton(key) && stack.GetRailButton(key)->GetIconImage() != nullptr,
              kind + ": the panel " + key + " has an icon for the rail");
        const std::string dialog = panels->DialogOf(key);
        if (!dialog.empty())
            Check(box && box->HasMoreButton() && panels->PanelOf(dialog) == key,
                  kind + ": " + dialog + " is the " + key + " panel's \"…\" button");
    }
}

// Every entry with a panel: placed in it.
void CheckTableInPanels(const UltraOffice::ToolTable& table, ToolPanels& panels, UltraCanvasToolPanelStack& stack,
                        const std::string& kind) {
    std::string missing;
    int count = 0;
    for (const UltraOffice::ToolPanelTools& group : UltraOffice::ToolsByPanel(table)) {
        auto content = stack.GetPanelContent(group.key);
        for (const UltraOffice::ToolEntry* entry : group.tools) {
            std::vector<std::string> ids;
            if (entry->kind == UltraOffice::ToolKind::ActionGroup)
                for (const auto& member : entry->members) ids.push_back(member.id);
            else
                ids.push_back(entry->id);
            for (const std::string& id : ids) {
                ++count;
                UltraCanvasUIElement* widget = panels.Widget(id);
                const bool inContent = widget && content && widget->IsDescendantOf(content.get());
                const bool isDialog = panels.DialogOf(group.key) == id && stack.GetPanel(group.key)->HasMoreButton();
                if (!(inContent || isDialog) || panels.PanelOf(id) != group.key) missing += " " + id;
            }
        }
    }
    Check(missing.empty() && count > 10, kind + ": every one of the table's " + std::to_string(count) +
                                             " tools with a panel is in its panel" +
                                             (missing.empty() ? std::string() : " (not:" + missing + ")"));
}

// The top row: File, Undo, Redo, Save, then the view group at its right end.
void CheckTopRow(UltraOffice::UltraOfficeWindow& window, int doc, const std::string& viewBar, const std::string& kind) {
    const std::string id = "office-doc-" + std::to_string(doc);
    auto* commands = Find<UltraCanvasToolbar>(&window, id + "-commands");
    auto* view = Find<UltraCanvasToolbar>(&window, viewBar);
    std::string items;
    if (commands)
        for (auto& item : commands->GetItems()) items += (items.empty() ? "" : " ") + item->GetIdentifier();
    const std::string expected = id + "-file " + id + "-undo " + id + "-redo " + id + "-save";
    Check(commands && items == expected, kind + ": the top row holds File, Undo, Redo, Save only (" + items + ")");
    Check(!Find<UltraCanvasButton>(&window, id + "-tab-0") && !Find<UltraCanvasContainer>(&window, id + "-command-bar"),
          kind + ": no command tabs, no command tab's bar");
    if (!commands || !view) {
        Check(false, kind + ": the view group " + viewBar + " is there");
        return;
    }
    const Rect2Df bar = view->GetBoundsInWindow();
    const Rect2Df row = commands->GetBoundsInWindow();
    Check(std::fabs(bar.y - row.y) < 1.0f && bar.x >= row.x + row.width - 1.0f &&
              std::fabs(bar.x + bar.width - static_cast<float>(window.GetWidth())) < 4.0f,
          kind + ": the view group last, in the same row, at its right end (" + std::to_string(bar.x) + " + " +
              std::to_string(bar.width) + " of " + std::to_string(window.GetWidth()) + ")");
}

// Every command tab's tools on its bar (toolbar mode): nothing behind the chevron.
void CheckCommandTabsFit(Harness& h, int docId, const std::string& kind) {
    const std::string id = "office-doc-" + std::to_string(docId);
    auto* slot = Find<UltraCanvasContainer>(h.window.get(), id + "-command-bar");
    int tabs = 0;
    for (int i = 0;; ++i) {
        auto* tab = Find<UltraCanvasButton>(h.window.get(), id + "-tab-" + std::to_string(i));
        if (!tab || !tab->onToggle) break;
        ++tabs;
        tab->onToggle(true);
        Pump(h.app, *h.window, 200);
        UltraCanvasToolbar* bar = nullptr;
        if (slot)
            for (auto& child : slot->GetChildren())
                if ((bar = dynamic_cast<UltraCanvasToolbar*>(child.get()))) break;
        std::string behind;
        if (bar)
            for (const std::string& item : bar->GetOverflowItemIds()) behind += " " + item;
        // The English labels fit at 1280 px; a longer language's go behind
        // the chevron first, as a narrow window's do (said, not failed).
        const bool english = UltraLocalisation::CurrentLanguage().rfind("en", 0) == 0;
        if (!english && bar && !behind.empty()) {
            std::printf("  note: %s, command tab %s: behind the chevron at %d px in %s:%s\n", kind.c_str(),
                        tab->GetText().c_str(), static_cast<int>(h.window->GetWidth()),
                        UltraLocalisation::CurrentLanguage().c_str(), behind.c_str());
            continue;
        }
        Check(bar && bar->GetOverflowItemIds().empty(), kind + ", command tab " + tab->GetText() + ": every tool on the bar at " +
                                                            std::to_string(h.window->GetWidth()) + " px" +
                                                            (behind.empty() ? std::string() : " (behind:" + behind + ")"));
    }
    Check(tabs >= 4, kind + ": the command tabs are back (" + std::to_string(tabs) + ")");
    if (auto* first = Find<UltraCanvasButton>(h.window.get(), id + "-tab-0"); first && first->onToggle) first->onToggle(true);
    Pump(h.app, *h.window, 200);
}

std::string OrderText(const std::vector<std::string>& order) {
    std::string text;
    for (const std::string& id : order) text += (text.empty() ? "" : ",") + id;
    return text;
}

UltraCanvasSpinner* SpinnerIn(ToolPanels* panels, const std::string& id) {
    return panels ? dynamic_cast<UltraCanvasSpinner*>(panels->Widget(id)) : nullptr;
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
    const fs::path settingsFolder = fs::temp_directory_path() / "uc-ultraoffice-panels-window-test";
    fs::remove_all(settingsFolder, ec);
    fs::create_directories(settingsFolder, ec);
    setenv("XDG_CONFIG_HOME", PathToUtf8(settingsFolder).c_str(), 1);   // path-string-ok: test's own ASCII temp folder
    // The light look unless the run names one (ULTRACANVAS_HOST_APPEARANCE=dark
    // for the dark screenshots); UltraOffice's own catalogue, so a run with
    // ULTRACANVAS_LANG=de shows its panels in German.
    setenv("ULTRACANVAS_HOST_APPEARANCE", "light", 0);

    UltraCanvasApplication app;
    if (!app.Initialize("UltraOffice")) {
        std::printf("%s: the application could not be initialised\n", DisplayTest::DisplayRequired() ? "FAIL" : "SKIP");
        return DisplayTest::DisplayRequired() ? 1 : 0;
    }
    // The flyout and the menus are drawn on the window's own surface.
    UltraCanvasWindowBase::SetPopupsInOwnWindows(false);
    auto window = OpenWindow(1600, 1000);
    if (!window) {
        std::printf("%s: the window could not be created\n", DisplayTest::DisplayRequired() ? "FAIL" : "SKIP");
        return DisplayTest::DisplayRequired() ? 1 : 0;
    }
    const TimerId tick = app.StartTimer(40, true, [](TimerId) {});
    Harness h{app, window};
    Pump(app, *window);

    // ===== THE START TAB'S NOTE =====
    std::printf("=== The Start tab says where the tools went ===\n");
    Check(window->IsToolsNoteShown(), "the first start: the Start tab's note on the tool panels");
    Check(Find<UltraCanvasButton>(window.get(), "office-start-tools-settings") != nullptr,
          "...with Settings, where the toolbar is chosen");
    Shoot(*window, "start-tools-note");
    if (auto* hide = Find<UltraCanvasButton>(window.get(), "office-start-tools-dismiss"); hide && hide->onClick) {
        hide->onClick();
        PumpUntil(app, *window, [&] { return !window->IsToolsNoteShown(); });
    }
    Check(!window->IsToolsNoteShown() && window->Settings().GetToolsNoteDismissed(),
          "Hide this note: gone, and settings.json remembers it");

    // ===== DOCUMENTS =====
    std::printf("\n=== Panels are the default ===\n");
    Check(window->Settings().GetToolLayout() == UltraOffice::ToolLayout::Panels, "the setting reads panels at the left");
    window->NewDocument(OfficeKind::Document);
    Pump(app, *window);
    UltraCanvasToolPanelStack* docStack = h.Stack(1);
    Check(docStack && docStack->IsVisible() && window->GetToolPanels(), "a new document has the tool panels");
    auto* paneOne = Find<UltraCanvasContainer>(window.get(), "office-doc-1");
    auto* edit = Find<UltraCanvasRichTextEdit>(paneOne, "office-document");
    Check(edit != nullptr, "the document's editor");
    if (!docStack || !edit) {
        std::printf("\nFAILED: %d failure(s)\n", failures + 1);
        return failures + 1;
    }
    const Rect2Df stackAt = docStack->GetBoundsInWindow();
    const Rect2Df editAt = edit->GetBoundsInWindow();
    Check(stackAt.x < 2.0f && stackAt.width > 200.0f && editAt.x >= stackAt.x + stackAt.width,
          "the panel column is at the left of the document (" + std::to_string(stackAt.width) + " wide)");
    CheckTopRow(*window, 1, "office-document-view", "document");
    Check(docStack->GetPanelCount() >= 9, "the document's panels: " + OrderText(docStack->GetOrder()));
    Check(docStack->IsPanelOpen("font") && docStack->IsPanelOpen("paragraph") && !docStack->IsPanelOpen("clipboard") &&
              !docStack->IsPanelOpen("review"),
          "open by default: Font and Paragraph");
    Check(!docStack->IsPanelShown("table") && !docStack->IsPanelShown("picture"), "the context panels are hidden");
    Check(docStack->GetPanel("paragraph")->HasMoreButton() && docStack->GetPanel("style")->HasMoreButton() &&
              docStack->GetPanel("page")->HasMoreButton() && docStack->GetPanel("review")->HasMoreButton(),
          "Paragraph, Style, Page and Review have their dialog as \"…\"");
    Check(!docStack->GetPanel("paragraph")->GetHelpText().empty(), "...and help text naming it (" +
                                                                        docStack->GetPanel("paragraph")->GetHelpText() + ")");
    CheckEveryPanel(*window, *docStack, "document");
    if (ToolPanels* panels = window->GetToolPanels()) {
        // The host's table, through a fresh one of the same kind.
        auto host = UltraOffice::CreateDocumentHost(window.get());
        CheckTableInPanels(host->GetTools(), *panels, *docStack, "document");
    }

    std::printf("\n=== A value edited in a panel ===\n");
    edit->InsertText("First paragraph");
    edit->GetEditor().SplitBlock();
    edit->InsertText("Second paragraph");
    Pump(app, *window, 150);
    UltraCanvasSpinner* before = SpinnerIn(window->GetToolPanels(), "doc-spacebefore");
    Check(before != nullptr, "the Paragraph panel's Space before is a spinner");
    auto caretTo = [&](RichDocPosition at, bool extend = false) {
        edit->GetEditor().SetCaret(at, extend);
        if (edit->onSelectionChanged) edit->onSelectionChanged();
        Pump(app, *window, 150);
    };
    auto shows = [](UltraCanvasSpinner* spinner) {
        const std::string text = spinner ? spinner->GetDisplayText() : std::string();
        return !text.empty() && text != "-";
    };

    std::printf("\n=== The values in effect ===\n");
    UltraCanvasSpinner* afterSpinner = SpinnerIn(window->GetToolPanels(), "doc-spaceafter");
    UltraCanvasSpinner* lineSpinner = SpinnerIn(window->GetToolPanels(), "doc-linespacing");
    auto* styleList = dynamic_cast<UltraCanvasDropdown*>(window->GetToolPanels()->Widget("doc-style"));
    if (before && afterSpinner && lineSpinner && styleList) {
        // A paragraph that states no spacing: the view's, which the layout uses.
        const double viewAfter = edit->GetStyle().blockSpacing * 72.0 / 96.0;
        Check(edit->GetParagraphFormat().spaceBeforePt < 0.0f && shows(before) && std::fabs(before->GetValue()) < 0.01 &&
                  shows(afterSpinner) && std::fabs(afterSpinner->GetValue() - viewAfter) < 0.05,
              "a paragraph with no spacing of its own shows the view's: " + before->GetDisplayText() + " before, " +
                  afterSpinner->GetDisplayText() + " after, not \"-\"");
        Check(shows(lineSpinner) && std::fabs(lineSpinner->GetValue() - 1.0) < 0.01,
              "...and single lines (" + lineSpinner->GetDisplayText() + ")");
        // Heading 1 from the style list: no spacing of its own, its style's 12 pt and 4 pt.
        styleList->SetSelectedIndex(1, true);
        Pump(app, *window, 150);
        Check(edit->GetCurrentHeadingLevel() == 1 && edit->GetParagraphFormat().spaceBeforePt < 0.0f &&
                  std::fabs(before->GetValue() - 12.0) < 0.01 && std::fabs(afterSpinner->GetValue() - 4.0) < 0.01,
              "a Heading 1 stating no spacing shows its style's: " + before->GetDisplayText() + " before, " +
                  afterSpinner->GetDisplayText() + " after");
        Shoot(*window, "document-0-paragraph-effective");
        styleList->SetSelectedIndex(0, true);
        Pump(app, *window, 150);
        Check(edit->GetCurrentHeadingLevel() == 0 && std::fabs(before->GetValue()) < 0.01, "Body text again: 0 pt before");
    } else {
        Check(false, "the Paragraph panel's spacing and line spacing, and the style list");
    }

    if (before) {
        Check(before->GetDisplayText().find(Tr("pt")) != std::string::npos || before->GetDisplayText() == "-",
              "...with its unit (" + before->GetDisplayText() + ")");
        before->SetValue(12.0);   // as typed or stepped
        Pump(app, *window, 150);
        Check(std::fabs(edit->GetParagraphFormat().spaceBeforePt - 12.0f) < 0.01f,
              "12 pt in the panel: the caret's paragraph has 12 pt before it (" +
                  std::to_string(edit->GetParagraphFormat().spaceBeforePt) + ")");
        // The caret to the first paragraph: the panel shows its own.
        edit->GetEditor().SetCaret(RichDocPosition(0, 2));
        if (edit->onSelectionChanged) edit->onSelectionChanged();
        Pump(app, *window, 150);
        Check(std::fabs(before->GetValue() - 12.0) > 0.01 || before->GetDisplayText() == "-",
              "the caret in the first paragraph: the panel follows (" + before->GetDisplayText() + ")");
        before->SetValue(6.0);
        Pump(app, *window, 150);
        edit->GetEditor().SetCaret(RichDocPosition(1, 2));
        if (edit->onSelectionChanged) edit->onSelectionChanged();
        Pump(app, *window, 150);
        Check(std::fabs(before->GetValue() - 12.0) < 0.01, "back in the second: 12 pt again (" + before->GetDisplayText() + ")");
        edit->GetEditor().SetCaret(RichDocPosition(0, 2));
        if (edit->onSelectionChanged) edit->onSelectionChanged();
        Pump(app, *window, 150);
        Check(std::fabs(before->GetValue() - 6.0) < 0.01 && std::fabs(edit->GetParagraphFormat().spaceBeforePt - 6.0f) < 0.01f,
              "and the first kept its 6 pt (" + before->GetDisplayText() + ")");
        // Both paragraphs selected: 6 and 12 pt before is "-", the same space after is shown.
        caretTo(RichDocPosition(0, 0));
        caretTo(RichDocPosition(1, 3), true);
        Check(!shows(before) && shows(afterSpinner), "a selection whose paragraphs differ: \"-\" for Space before (" +
                                                         before->GetDisplayText() + "), Space after still shown (" +
                                                         (afterSpinner ? afterSpinner->GetDisplayText() : std::string()) + ")");
        caretTo(RichDocPosition(0, 2));
        Shoot(*window, "document-0-paragraph-values");
    }
    if (auto* bold = dynamic_cast<UltraCanvasButton*>(window->GetToolPanels()->Widget("doc-bold"))) {
        edit->GetEditor().SetCaret(RichDocPosition(0, 0));
        edit->GetEditor().SetCaret(RichDocPosition(0, 5), true);
        if (edit->onSelectionChanged) edit->onSelectionChanged();
        if (bold->onToggle) bold->onToggle(true);
        Pump(app, *window, 150);
        Check(RichCharFormatState::IsOn(edit->GetFormatState().bold) && bold->IsPressed(),
              "Bold in the Font panel makes the selection bold, and shows it pressed");
    } else {
        Check(false, "the Font panel's Bold");
    }

    std::printf("\n=== The Table context panel ===\n");
    edit->GetEditor().SetCaret(RichDocPosition(1, 16));
    edit->InsertTable(3, 3, true);
    Pump(app, *window, 200);
    Check(edit->IsCaretInTable() && docStack->IsPanelShown("table") && docStack->IsPanelOpen("table"),
          "the caret in a table: the Table panel is there, open");
    Check(docStack->GetOrder().front() == "table" &&
              docStack->GetPanel("table")->GetBoundsInWindow().y < docStack->GetPanel("clipboard")->GetBoundsInWindow().y,
          "...at the top of the column");
    Check(docStack->GetPanel("table")->HasMoreButton(), "...with Table properties as its \"…\"");
    Shoot(*window, "document-1-context-table");
    if (ToolPanels* panels = window->GetToolPanels()) {
        // Borders: the view's grid line where the table states none, not "-".
        UltraCanvasSpinner* border = SpinnerIn(panels, "doc-table-border");
        RichTableProperties own;
        Check(border && shows(border) && edit->GetTableProperties(own) && own.borderWidthPt < 0.0f &&
                  std::fabs(border->GetValue() - 0.75) < 0.01,
              "a table on the view's grid: Borders shows its line, 0.75 pt (" + (border ? border->GetDisplayText() : "") + ")");
        // Cell shading, both ways.
        auto* shading = dynamic_cast<UltraCanvasColorSwatchBar*>(panels->Widget("doc-cell-shading"));
        std::string fill;
        if (shading) {
            shading->SelectColor(Color(255, 192, 0, 255), true);
            Pump(app, *window, 150);
        }
        Check(shading && edit->GetCellShading(fill) && fill == "#FFC000", "Cell shading: the cell is filled (" + fill + ")");
        caretTo(edit->GetEditor().GetCaret());
        Check(shading && shading->HasColorSelected() && shading->GetSelectedColor().ToHexString() == "#FFC000",
              "...and the swatch shows it");
        if (auto* none = dynamic_cast<UltraCanvasButton*>(panels->Widget("doc-cell-noshading")); none && none->onClick) none->onClick();
        Pump(app, *window, 150);
        Check(edit->GetCellShading(fill) && fill.empty() && shading && !shading->HasColorSelected(),
              "No shading: the fill is gone, no swatch chosen");
        // Merging and splitting.
        auto* merge = dynamic_cast<UltraCanvasButton*>(panels->Widget("doc-cell-merge"));
        auto* split = dynamic_cast<UltraCanvasButton*>(panels->Widget("doc-cell-split"));
        Check(split && split->IsDisabled(), "Split cell is off on a cell of its own");
        if (merge && merge->onClick) merge->onClick();
        Pump(app, *window, 150);
        Check(edit->CanSplitCurrentCell() && split && !split->IsDisabled(), "Merge cells: the cell spans two, Split cell is on");
        if (split && split->onClick) split->onClick();
        Pump(app, *window, 150);
        const RichDocBlock& table = edit->GetEditor().GetBlock(edit->GetEditor().GetCaret().blockIndex);
        Check(!edit->CanSplitCurrentCell() && !table.tableRows.empty() && table.tableRows.front().cells.size() == 3,
              "Split cell: back to cells of their own (" +
                  std::to_string(table.tableRows.empty() ? 0 : table.tableRows.front().cells.size()) + " in the row)");
        Shoot(*window, "document-1-table-split");
    }
    edit->GetEditor().SetCaret(RichDocPosition(0, 1));
    if (edit->onSelectionChanged) edit->onSelectionChanged();
    Pump(app, *window, 200);
    Check(!edit->IsCaretInTable() && !docStack->IsPanelShown("table"), "the caret out of it: the Table panel goes");

    std::printf("\n=== The Lists and Page panels ===\n");
    if (ToolPanels* panels = window->GetToolPanels()) {
        // The list style, both ways: a. b. c., then None.
        auto* listStyle = dynamic_cast<UltraCanvasDropdown*>(panels->Widget("doc-liststyle"));
        Check(listStyle && listStyle->GetSelectedIndex() == 0, "List style reads None in a plain paragraph");
        if (listStyle) {
            listStyle->SetSelectedIndex(5, true);
            Pump(app, *window, 150);
            const RichDocBlock& block = edit->GetEditor().GetBlock(0);
            Check(block.type == RichBlockType::ListItem && block.orderedList && block.numberFormat == RichNumberFormat::LowerLetter &&
                      listStyle->GetSelectedIndex() == 5,
                  "List style a. b. c.: the paragraph is a lettered list item, and the list says so");
            listStyle->SetSelectedIndex(0, true);
            Pump(app, *window, 150);
            Check(edit->GetEditor().GetBlock(0).type == RichBlockType::Paragraph && listStyle->GetSelectedIndex() == 0,
                  "None: a plain paragraph again");
        }
        // The page: orientation, size, margins, columns - each set and read back.
        docStack->SetPanelOpen("page", true);
        Pump(app, *window, 150);
        auto* portrait = dynamic_cast<UltraCanvasButton*>(panels->Widget("doc-portrait"));
        auto* landscape = dynamic_cast<UltraCanvasButton*>(panels->Widget("doc-landscape"));
        auto* size = dynamic_cast<UltraCanvasDropdown*>(panels->Widget("doc-pagesize"));
        auto* margins = dynamic_cast<UltraCanvasDropdown*>(panels->Widget("doc-margins"));
        UltraCanvasSpinner* columns = SpinnerIn(panels, "doc-columns");
        Check(portrait && landscape && size && margins && columns, "the Page panel: orientation, page size, margins, columns");
        if (portrait && landscape && size && margins && columns) {
            const RichPageSetup start = edit->GetEffectivePageSetup();
            Check(portrait->IsPressed() && !landscape->IsPressed() && size->GetSelectedIndex() == 1 &&
                      margins->GetSelectedIndex() == 1 && std::lround(columns->GetValue()) == 1,
                  "...reading portrait, A4, Normal margins, one column");
            if (landscape->onToggle) landscape->onToggle(true);
            Pump(app, *window, 150);
            RichPageSetup page = edit->GetEffectivePageSetup();
            Check(page.widthPt > page.heightPt && landscape->IsPressed() && !portrait->IsPressed(),
                  "Landscape: the page on its side (" + std::to_string(page.widthPt) + " x " + std::to_string(page.heightPt) + ")");
            size->SetSelectedIndex(2, true);   // A5
            Pump(app, *window, 150);
            page = edit->GetEffectivePageSetup();
            Check(std::fabs(page.heightPt - 419.53f) < 1.0f && std::fabs(page.widthPt - 595.28f) < 1.0f && size->GetSelectedIndex() == 2,
                  "Page size A5: still on its side, and the list says A5");
            margins->SetSelectedIndex(0, true);   // Narrow
            Pump(app, *window, 150);
            page = edit->GetEffectivePageSetup();
            Check(std::fabs(page.marginLeftPt - 36.0f) < 0.1f && std::fabs(page.marginTopPt - 36.0f) < 0.1f &&
                      margins->GetSelectedIndex() == 0,
                  "Margins Narrow: 1.27 cm all round, and the list says Narrow");
            columns->SetValue(2.0);
            Pump(app, *window, 150);
            Check(edit->GetCurrentSection().columns == 2 && std::lround(columns->GetValue()) == 2, "Columns 2: the text in two columns");
            Shoot(*window, "document-0-page-panel");
            columns->SetValue(1.0);
            margins->SetSelectedIndex(1, true);
            size->SetSelectedIndex(1, true);
            if (portrait->onToggle) portrait->onToggle(true);
            Pump(app, *window, 150);
            page = edit->GetEffectivePageSetup();
            Check(std::fabs(page.widthPt - start.widthPt) < 0.1f && std::fabs(page.heightPt - start.heightPt) < 0.1f &&
                      std::fabs(page.marginLeftPt - start.marginLeftPt) < 0.1f && edit->GetCurrentSection().columns == 1,
                  "...and back: portrait A4, Normal margins, one column");
        }
        docStack->SetPanelOpen("page", false);
    }

    std::printf("\n=== Reordering, saved ===\n");
    docStack->SetPanelOpen("lists", false);
    h.DragBelow(*docStack, "clipboard", "paragraph", false);
    Check(docStack->IsDraggingPanel() && docStack->GetDraggedPanel() == "clipboard", "Clipboard dragged by its header");
    Shoot(*window, "document-2-drag");
    h.Release();
    const std::vector<std::string> docOrder = docStack->GetOrder();
    auto after = [&](const std::vector<std::string>& order, const std::string& a, const std::string& b) {
        const auto ia = std::find(order.begin(), order.end(), a), ib = std::find(order.begin(), order.end(), b);
        return ia != order.end() && ib != order.end() && ia > ib;
    };
    Check(after(docOrder, "clipboard", "paragraph"), "dropped below Paragraph: " + OrderText(docOrder));
    // A panel opened with a click on its header.
    h.Click(h.HeaderMiddle(*docStack, "lists"));
    Check(docStack->IsPanelOpen("lists"), "Lists opened with a click on its header");
    const std::string savedDoc = window->Settings().GetPanelState(OfficeKind::Document);
    Check(savedDoc.find("order=") == 0 && savedDoc.find("lists") != std::string::npos,
          "the order and the open panels are in the settings: " + savedDoc);

    // ===== SPREADSHEETS =====
    std::printf("\n=== Spreadsheets ===\n");
    window->NewDocument(OfficeKind::Spreadsheet);
    Pump(app, *window);
    UltraCanvasToolPanelStack* sheetStack = h.Stack(2);
    auto* paneTwo = Find<UltraCanvasContainer>(window.get(), "office-doc-2");
    auto* sheet = Find<UltraCanvasSpreadsheet>(paneTwo, "office-spreadsheet");
    Check(sheetStack && sheet, "a spreadsheet has the tool panels");
    if (sheetStack && sheet) {
        CheckTopRow(*window, 2, "office-sheet-view", "spreadsheet");
        Check(sheetStack->IsPanelOpen("cell") && sheetStack->IsPanelOpen("number") && !sheetStack->IsPanelOpen("data"),
              "open by default: Cell and Number");
        if (ToolPanels* panels = window->GetToolPanels()) {
            auto host = UltraOffice::CreateSpreadsheetHost(window.get());
            CheckTableInPanels(host->GetTools(), *panels, *sheetStack, "spreadsheet");
        }
        // A pivot table: its cell active, the Pivot table panel holds the field list.
        const std::vector<std::vector<std::string>> rows = {
            {"Region", "Product", "Sales"}, {"East", "Apples", "120"}, {"West", "Pears", "80"},
            {"East", "Pears", "60"},        {"West", "Apples", "95"}};
        for (size_t r = 0; r < rows.size(); ++r)
            for (size_t c = 0; c < rows[r].size(); ++c) {
                if (r > 0 && c == 2) sheet->SetCellValue(static_cast<int>(r), static_cast<int>(c), std::stod(rows[r][c]));
                else sheet->SetCellValue(static_cast<int>(r), static_cast<int>(c), rows[r][c]);
            }
        SpreadsheetPivotTable pivot;
        pivot.sourceSheet = sheet->GetActiveSheet()->GetName();
        pivot.sourceRange = CellRange(0, 0, 4, 2);
        PivotField region;
        region.column = 0;
        PivotField product;
        product.column = 1;
        PivotValueField sales;
        sales.column = 2;
        pivot.rowFields.push_back(region);
        pivot.columnFields.push_back(product);
        pivot.valueFields.push_back(sales);
        std::string problem;
        const int made = sheet->InsertPivotTable(pivot, -1, &problem);
        sheet->SelectCell(2, 1);
        Pump(app, *window, 200);
        Check(made == 0 && sheet->GetActivePivotTable() == 0, "a pivot table's cell is active " + problem);
        Check(sheetStack->IsPanelShown("pivot") && sheetStack->IsPanelOpen("pivot"), "the Pivot table panel is there, open");
        auto* fields = Find<UltraCanvasSpreadsheetPivotPanel>(window.get(), "office-pivot-fields");
        auto pivotContent = sheetStack->GetPanelContent("pivot");
        Check(fields && fields->IsVisible() && pivotContent && fields->IsDescendantOf(pivotContent.get()) &&
                  fields->GetHeight() > 200.0f,
              "...holding the pivot field list");
        Shoot(*window, "spreadsheet-1-context-pivot");
        sheet->SelectCell(12, 6);   // out of the pivot: its panel goes
        Pump(app, *window, 200);
        h.DragBelow(*sheetStack, "clipboard", "number", false);
        Check(sheetStack->IsDraggingPanel(), "Clipboard dragged by its header");
        Shoot(*window, "spreadsheet-2-drag");
        h.Release();
        Check(after(sheetStack->GetOrder(), "clipboard", "number"), "dropped below Number: " + OrderText(sheetStack->GetOrder()));
    }

    // ===== PRESENTATIONS =====
    std::printf("\n=== Presentations ===\n");
    window->NewDocument(OfficeKind::Presentation);
    Pump(app, *window);
    UltraCanvasToolPanelStack* slideStack = h.Stack(3);
    auto* paneThree = Find<UltraCanvasContainer>(window.get(), "office-doc-3");
    auto* slides = Find<UltraCanvasSlideEdit>(paneThree, "office-presentation");
    Check(slideStack && slides, "a presentation has the tool panels");
    if (slideStack && slides) {
        CheckTopRow(*window, 3, "office-slides-view", "presentation");
        Check(slideStack->IsPanelOpen("slide") && !slideStack->IsPanelOpen("insert"), "open by default: Slide (and Text, when shown)");
        if (ToolPanels* panels = window->GetToolPanels()) {
            auto host = UltraOffice::CreatePresentationHost(window.get());
            CheckTableInPanels(host->GetTools(), *panels, *slideStack, "presentation");
        }
        slides->Editor().Select({0});
        Pump(app, *window, 200);
        Check(slideStack->IsPanelShown("shape") && slideStack->IsPanelShown("arrange") && slideStack->IsPanelShown("text") &&
                  slideStack->IsPanelOpen("text"),
              "a shape selected: the Shape, Arrange and Text panels are there");
        UltraCanvasSpinner* x = SpinnerIn(window->GetToolPanels(), "slides-x");
        Check(x != nullptr, "the Shape panel's horizontal position is a spinner");
        if (x && !slides->Editor().GetSelection().empty()) {
            const double shown = x->GetValue();
            const double cm = static_cast<double>(slides->Editor().SelectionBox().x) / 360000.0;
            Check(std::fabs(shown - cm) < 0.02, "...showing the shape's (" + std::to_string(shown) + " cm)");
            x->SetValue(3.0);
            Pump(app, *window, 150);
            const double now = static_cast<double>(slides->Editor().SelectionBox().x) / 360000.0;
            Check(std::fabs(now - 3.0) < 0.02, "3 cm in the panel: the shape is 3 cm from the left (" + std::to_string(now) + ")");
            slides->Editor().Select({1});
            Pump(app, *window, 150);
            const double other = static_cast<double>(slides->Editor().SelectionBox().x) / 360000.0;
            Check(std::fabs(x->GetValue() - other) < 0.02, "another shape selected: the panel shows its own (" +
                                                               std::to_string(x->GetValue()) + " cm)");
        }
        Shoot(*window, "presentation-1-context-shape");
        slides->Editor().Select({});
        Pump(app, *window, 150);
        Check(!slideStack->IsPanelShown("shape"), "nothing selected: the Shape panel goes");
        // The slide's background: white as drawn, then a colour of its own.
        if (ToolPanels* panels = window->GetToolPanels()) {
            auto* background = dynamic_cast<UltraCanvasColorSwatchBar*>(panels->Widget("slides-background"));
            Check(background && background->HasColorSelected() && background->GetSelectedColor().ToHexString() == "#FFFFFF",
                  "the Slide panel's Background reads the white the slide is drawn on");
            if (background) {
                background->SelectColor(Color(68, 114, 196, 255), true);
                Pump(app, *window, 150);
            }
            const PptxSlide* slide = slides->Editor().CurrentSlide();
            Check(slide && slide->background == "4472C4" && background && background->GetSelectedColor().ToHexString() == "#4472C4",
                  "Background blue: the slide's background, and the swatch shows it (" + (slide ? slide->background : "") + ")");
        }
        h.DragBelow(*slideStack, "clipboard", "insert", false);
        Check(slideStack->IsDraggingPanel(), "Clipboard dragged by its header");
        Shoot(*window, "presentation-2-drag");
        h.Release();
        Check(after(slideStack->GetOrder(), "clipboard", "insert"), "dropped below Insert: " + OrderText(slideStack->GetOrder()));
    }

    std::printf("\n=== F6 ===\n");
    {
        DisplayTest::ActivateWindow(app, window);
        auto* tabs = Find<UltraCanvasTabbedContainer>(window.get(), "office-tabs");
        if (tabs) tabs->SetActiveTab(1);
        Pump(app, *window, 150);
        edit->SetFocus(true);
        h.Key(UCKeys::F6);
        Check(window->GetFocusedElement() == tabs && tabs && tabs->IsKeyboardFocusOnRequest() && tabs->IsFocused(),
              "F6 from the document: the tab strip, which draws its focus ring");
        if (tabs) {
            // The ring round the open tab: the focus colour on its edge.
            const Rect2Df strip = tabs->GetBoundsInWindow();
            bool ring = false;
            for (int x = static_cast<int>(strip.x); x < static_cast<int>(strip.x + strip.width) && !ring; ++x)
                for (int y = static_cast<int>(strip.y); y < static_cast<int>(strip.y) + 34 && !ring; ++y) {
                    Color px;
                    ring = window->GetPixelColor(x, y, px) && px.r == tabs->GetFocusRingColor().r &&
                           px.g == tabs->GetFocusRingColor().g && px.b == tabs->GetFocusRingColor().b;
                }
            Check(ring, "...in the focus colour, on the strip");
            Shoot(*window, "f6-tab-strip-focus");
        }
        h.Key(UCKeys::F6);
        auto* file = Find<UltraCanvasButton>(window.get(), "office-doc-1-file");
        Check(file && window->GetFocusedElement() == file, "F6: the toolbar (File)");
        h.Key(UCKeys::F6);
        Check(!docStack->GetFocusedPanel().empty(), "F6: the panels (" + docStack->GetFocusedPanel() + ")");
        h.Key(UCKeys::F6);
        Check(window->GetFocusedElement() == edit, "F6: the document again");
        h.Key(UCKeys::F6, true);
        Check(!docStack->GetFocusedPanel().empty(), "Shift+F6: back to the panels");
        edit->SetFocus(true);
    }

    std::printf("\n=== Toolbar at the top, as a setting ===\n");
    window->SetWindowSize(1280, 820);
    PumpUntil(app, *window, [&] { return window->GetWidth() == 1280; });
    window->Settings().SetToolLayout(UltraOffice::ToolLayout::Toolbar);
    window->ApplySettings();
    Pump(app, *window);
    Check(!h.Stack(1) && !h.Stack(2) && !h.Stack(3) && !window->GetToolPanels(), "the panels are gone from every tab");
    if (auto* tabs = Find<UltraCanvasTabbedContainer>(window.get(), "office-tabs")) tabs->SetActiveTab(1);
    Pump(app, *window);
    CheckCommandTabsFit(h, 1, "document");
    if (auto* tabs = Find<UltraCanvasTabbedContainer>(window.get(), "office-tabs")) tabs->SetActiveTab(2);
    Pump(app, *window);
    CheckCommandTabsFit(h, 2, "spreadsheet");
    Check(Find<UltraCanvasSpreadsheetPivotPanel>(window.get(), "office-pivot-fields") != nullptr,
          "the pivot field list is back beside the grid");
    if (auto* tabs = Find<UltraCanvasTabbedContainer>(window.get(), "office-tabs")) tabs->SetActiveTab(3);
    Pump(app, *window);
    CheckCommandTabsFit(h, 3, "presentation");
    window->Settings().SetToolLayout(UltraOffice::ToolLayout::Panels);
    window->ApplySettings();
    Pump(app, *window);
    Check(h.Stack(1) && h.Stack(2) && h.Stack(3), "panels again: every tab has them");
    Check(h.Stack(1) && after(h.Stack(1)->GetOrder(), "clipboard", "paragraph"), "...in the order saved");

    std::printf("\n=== The icon rail ===\n");
    window->SetWindowSize(1024, 768);
    PumpUntil(app, *window, [&] { return window->GetWidth() == 1024; });
    for (int doc = 1; doc <= 3; ++doc) {
        if (auto* tabs = Find<UltraCanvasTabbedContainer>(window.get(), "office-tabs")) tabs->SetActiveTab(doc);
        Pump(app, *window, 300);
        UltraCanvasToolPanelStack* stack = h.Stack(doc);
        if (!stack) { Check(false, "the panels of tab " + std::to_string(doc)); continue; }
        const std::string kind = doc == 1 ? "document" : doc == 2 ? "spreadsheet" : "presentation";
        Check(stack->IsRailMode() && stack->GetWidth() < 60.0f, kind + ": at 1024 px the panels are the icon rail (" +
                                                                    std::to_string(stack->GetWidth()) + " wide)");
        const std::string open = doc == 1 ? "font" : doc == 2 ? "cell" : "slide";
        if (auto icon = stack->GetRailButton(open)) {
            const Point2Df at = icon->GetPositionInWindow();
            h.Click(Point2Di(static_cast<int>(at.x + icon->GetWidth() / 2), static_cast<int>(at.y + icon->GetHeight() / 2)));
            Pump(app, *window, 200);
            Check(stack->IsFlyoutOpen() && stack->GetFlyoutPanel() == open, kind + ": its icon opens " + open + " as a flyout");
            Shoot(*window, kind + "-4-rail-flyout");
            stack->CloseFlyout();
            Pump(app, *window, 100);
        } else {
            Check(false, kind + ": the rail icon of " + open);
        }
    }
    window->SetWindowSize(1600, 1000);
    PumpUntil(app, *window, [&] { return window->GetWidth() == 1600; });
    Pump(app, *window, 300);
    Check(h.Stack(3) && !h.Stack(3)->IsRailMode(), "wide again: the column");
    window->Settings().SetToolRail(true);
    window->ApplySettings();
    Pump(app, *window, 300);
    Check(h.Stack(3) && h.Stack(3)->IsRailMode() && h.Stack(3)->IsRailModeChosen(), "Icon rail chosen: the rail at 1600 px too");
    window->Settings().SetToolRail(false);
    window->ApplySettings();
    Pump(app, *window, 300);

    std::printf("\n=== The Settings window ===\n");
    window->ShowSettings();
    Pump(app, *window);
    if (auto* dialog = UltraOffice::GetSettingsDialog(); dialog && dialog->window) {
        dialog->SelectPage(UltraOffice::kSettingsGeneral);
        auto* tools = Find<UltraCanvasDropdown>(dialog->window.get(), "office-set-tools");
        auto* rail = Find<UltraCanvasCheckbox>(dialog->window.get(), "office-set-rail");
        Check(tools && tools->GetSelectedIndex() == 0 && rail && !rail->IsChecked(),
              "General: Tools reads panels at the left, the icon rail off");
        for (int i = 0; i < 4; ++i) dialog->window->UpdateAndRender();
        Shoot(*dialog->window, "settings-general");
        if (tools) {
            tools->SetSelectedIndex(1, true);
            Pump(app, *window);
            Check(!h.Stack(1) && Find<UltraCanvasButton>(window.get(), "office-doc-1-tab-0"),
                  "Toolbar at the top chosen there: the command tabs at once");
            tools->SetSelectedIndex(0, true);
            Pump(app, *window);
            Check(h.Stack(1) != nullptr, "...and Panels at the left again: the panels at once");
        }
        dialog->window->Close();
        PumpUntil(app, *window, [] { return UltraOffice::GetSettingsDialog() == nullptr; });
    } else {
        Check(false, "the Settings window opens");
    }

    std::printf("\n=== After a restart ===\n");
    const std::string docState = window->Settings().GetPanelState(OfficeKind::Document);
    const std::string sheetState = window->Settings().GetPanelState(OfficeKind::Spreadsheet);
    const std::string slideState = window->Settings().GetPanelState(OfficeKind::Presentation);
    const std::vector<std::string> docOrderSaved = h.Stack(1) ? h.Stack(1)->GetOrder() : std::vector<std::string>();
    const std::vector<std::string> sheetOrderSaved = h.Stack(2) ? h.Stack(2)->GetOrder() : std::vector<std::string>();
    const std::vector<std::string> slideOrderSaved = h.Stack(3) ? h.Stack(3)->GetOrder() : std::vector<std::string>();
    window->Close();
    Pump(app, *window, 100);
    window.reset();
    window = OpenWindow(1600, 1000);
    h.window = window;
    Check(window != nullptr, "the window opened again");
    if (window) {
        Pump(app, *window);
        Check(!window->IsToolsNoteShown(), "the Start tab's note, dismissed, stays gone after a restart");
        window->NewDocument(OfficeKind::Document);
        window->NewDocument(OfficeKind::Spreadsheet);
        window->NewDocument(OfficeKind::Presentation);
        Pump(app, *window);
        UltraCanvasToolPanelStack* again = h.Stack(1);
        Check(again && again->GetOrder() == docOrderSaved, "the document's panels in the saved order: " +
                                                               (again ? OrderText(again->GetOrder()) : std::string()));
        Check(again && again->IsPanelOpen("lists") && again->IsPanelOpen("font") && !again->IsPanelOpen("clipboard"),
              "...the panels open as they were (Lists, Font, Paragraph)");
        Check(h.Stack(2) && h.Stack(2)->GetOrder() == sheetOrderSaved, "the spreadsheet's too");
        Check(h.Stack(3) && h.Stack(3)->GetOrder() == slideOrderSaved, "the presentation's too");
        Check(window->Settings().GetPanelState(OfficeKind::Document) == docState && !sheetState.empty() && !slideState.empty(),
              "settings.json kept each kind's panels");
        for (int doc = 1; doc <= 3; ++doc) {
            if (auto* tabs = Find<UltraCanvasTabbedContainer>(window.get(), "office-tabs")) tabs->SetActiveTab(doc);
            Pump(app, *window, 300);
            Shoot(*window, doc == 1 ? "document-3-restart" : doc == 2 ? "spreadsheet-3-restart" : "presentation-3-restart");
        }
        // The splitter's width, kept per kind.
        window->Settings().SetPanelWidth(OfficeKind::Document, 320);
        window->NewDocument(OfficeKind::Document);
        Pump(app, *window);
        Check(h.Stack(4) && std::fabs(h.Stack(4)->GetWidth() - 320.0f) < 2.0f,
              "a new tab takes the kind's column width (" + std::to_string(h.Stack(4) ? h.Stack(4)->GetWidth() : 0.0f) + ")");
        window->Close();
    }

    app.StopTimer(tick);
    fs::remove_all(settingsFolder, ec);
    std::printf("\n%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures;
}
