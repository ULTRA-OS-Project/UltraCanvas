// Tests/ToolPanelStackElementTest.cpp
// UltraCanvasToolPanelStack on a real window (Xvfb in CI), driven through the
// application as the platform layer drives it: panels added, opened and
// closed (a click on the header, SetPanelOpen); the order by SetOrder (an
// unknown id skipped, an unnamed panel at its default place), by dragging a
// header with the mouse (the drop line, the drop index, no fold on the
// release) and by Ctrl+Up / Ctrl+Down on a focused header; SaveState /
// RestoreState round trip into a second stack, ids with separators in them;
// a context panel shown and hidden without moving the panel under the
// pointer, and at the top when the column is not scrolled; rail mode, its
// flyout (a click on an icon, another icon, a click outside), Escape closing
// one level - the font list in the flyout first, then the flyout; the
// keyboard (FocusFirst, Up / Down, Enter / Space, Tab into a panel's
// controls and past a closed one); the "…" button; the rows' setters
// (UltraCanvasToolRows.h) and Sync, which calls none. Accessibility: each
// panel a group named by its title, the stack a toolbar.
// ULTRACANVAS_SCREENSHOT_DIR=<dir> writes the window as PPM files: open,
// closed, context panel, during a drag, rail, flyout. Skips without a
// display (fails with ULTRACANVAS_TEST_DISPLAY_REQUIRED).
// Version: 1.1.0 - a spinner's value in German: shown with a comma, typed with either key
// Version: 1.0.0
// Author: UltraCanvas Framework

#include "DisplayTestSupport.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasToolPanelStack.h"
#include "UltraLocalisation/UltraLocalisation.h"
#include "UltraCanvasToolRows.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasWindow.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace UltraCanvas;

static int testCount = 0;
static int failCount = 0;

#define TEST(name, condition)                                                 \
    do {                                                                      \
        bool passed = (condition);                                            \
        std::cerr << (passed ? "PASS" : "FAIL") << ": " << name << std::endl; \
        if (!passed) failCount++;                                             \
        testCount++;                                                          \
    } while (0)

#define SKIP_ALL(reason)                                                      \
    do {                                                                      \
        if (UltraCanvas::DisplayTest::DisplayRequired()) {                    \
            std::cerr << "FAIL: a display was required, and " << reason       \
                      << std::endl;                                           \
            return 1;                                                         \
        }                                                                     \
        std::cerr << "SKIP: whole suite (" << reason << ")" << std::endl;     \
        return 0;                                                             \
    } while (0)

namespace {

constexpr int kWidth = 900, kHeight = 720;

bool WritePpm(const std::shared_ptr<UltraCanvasWindow>& window, const std::string& path) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out << "P6\n" << kWidth << " " << kHeight << "\n255\n";
    std::vector<unsigned char> row(static_cast<size_t>(kWidth) * 3);
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            Color px;
            if (!window->GetPixelColor(x, y, px)) px = Colors::Black;
            row[static_cast<size_t>(x) * 3 + 0] = px.r;
            row[static_cast<size_t>(x) * 3 + 1] = px.g;
            row[static_cast<size_t>(x) * 3 + 2] = px.b;
        }
        out.write(reinterpret_cast<const char*>(row.data()), static_cast<std::streamsize>(row.size()));
    }
    return static_cast<bool>(out);
}

std::string Icon(const std::string& relative) {
    return NormalizePath(GetResourcesDir() + "media/icons/" + relative);
}

// What the panels edit: a stand-in for a document's selection.
struct Document {
    bool bold = false, italic = false, underline = false, strike = false;
    int align = 0;
    double size = 12, spacing = 1.15, before = 0, after = 10;
    int style = 0;
    Color colour = Colors::Black;
    std::string font = "Ubuntu";
    int setterCalls = 0;
    int dialogs = 0;
};

// The sample panels of a word processor's column (the proposal's §3.1).
void AddSamplePanels(UltraCanvasToolPanelStack& stack, Document& doc, const std::string& prefix) {
    auto clipboard = CreateToolPanelContent(prefix + "clipboard");
    auto clipRow = CreateToolButtonRow(prefix + "clip-row");
    clipRow->AddButton("cut", Icon("menu/cut.svg"), "Cut", "Ctrl+X", [] {});
    clipRow->AddButton("copy", Icon("menu/copy.svg"), "Copy", "Ctrl+C", [] {});
    clipRow->AddButton("paste", Icon("menu/paste.svg"), "Paste", "Ctrl+V", [] {});
    clipboard->AddChild(clipRow);
    ToolPanelOptions clipOptions;
    clipOptions.iconPath = Icon("menu/paste.svg");
    clipOptions.helpText = "Cut, copy and paste";
    stack.AddPanel("clipboard", "Clipboard", clipboard, clipOptions);

    auto style = CreateToolPanelContent(prefix + "style");
    auto styleRows = CreateToolValueRows(prefix + "style-rows");
    styleRows->AddListRow("style-list", {"Body text", "Heading 1", "Heading 2", "Quote"},
                          [&doc] { return doc.style; }, [&doc](int i) { doc.style = i; ++doc.setterCalls; });
    style->AddChild(styleRows);
    ToolPanelOptions styleOptions;
    styleOptions.iconPath = Icon("menu/format.svg");
    styleOptions.dialogAction = [&doc] { ++doc.dialogs; };
    styleOptions.dialogTooltip = "Styles";
    styleOptions.open = false;
    stack.AddPanel("style", "Style", style, styleOptions);

    auto font = CreateToolPanelContent(prefix + "font");
    auto fontRows = CreateToolValueRows(prefix + "font-rows");
    fontRows->AddFontPicker("font-picker", [&doc] { return doc.font; },
                            [&doc](const FontPickerChoice& c) { doc.font = c.family; ++doc.setterCalls; });
    fontRows->AddSpinner("font-size", "Size", 6, 96, 1, 0, [&doc] { return doc.size; },
                         [&doc](double v) { doc.size = v; ++doc.setterCalls; }, "pt");
    auto fontButtons = CreateToolButtonRow(prefix + "font-buttons");
    fontButtons->AddToggle("bold", Icon("textformat/bold.svg"), "Bold", "Ctrl+B",
                           [&doc] { return doc.bold; }, [&doc](bool on) { doc.bold = on; ++doc.setterCalls; });
    fontButtons->AddToggle("italic", Icon("textformat/italic.svg"), "Italic", "Ctrl+I",
                           [&doc] { return doc.italic; }, [&doc](bool on) { doc.italic = on; ++doc.setterCalls; });
    fontButtons->AddToggle("underline", Icon("textformat/underline.svg"), "Underline", "Ctrl+U",
                           [&doc] { return doc.underline; }, [&doc](bool on) { doc.underline = on; ++doc.setterCalls; });
    fontButtons->AddToggle("strike", Icon("textformat/strike.svg"), "Strikethrough", "",
                           [&doc] { return doc.strike; }, [&doc](bool on) { doc.strike = on; ++doc.setterCalls; });
    fontRows->AddWideRow(fontButtons);
    fontRows->AddColorSwatches("text-colour", "Colour",
                                 {Colors::Black, Color(196, 43, 28), Color(0, 120, 215), Color(16, 124, 16), Color(255, 185, 0)},
                                 [&doc] { return doc.colour; }, [&doc](const Color& c) { doc.colour = c; ++doc.setterCalls; });
    font->AddChild(fontRows);
    ToolPanelOptions fontOptions;
    fontOptions.iconPath = Icon("textformat/bold.svg");
    fontOptions.dialogAction = [&doc] { ++doc.dialogs; };
    fontOptions.dialogTooltip = "Font";
    stack.AddPanel("font", "Font", font, fontOptions);

    auto paragraph = CreateToolPanelContent(prefix + "paragraph");
    auto alignRow = CreateToolButtonRow(prefix + "align-row");
    const char* alignIcons[] = {"textformat/align-left.svg", "textformat/align-centre.svg",
                                "textformat/align-right.svg", "office/justify.svg"};
    const char* alignNames[] = {"Align left", "Centre", "Align right", "Justify"};
    for (int i = 0; i < 4; ++i) {
        alignRow->AddToggle(std::string("align") + std::to_string(i), Icon(alignIcons[i]), alignNames[i], "",
                            [&doc, i] { return doc.align == i; }, [&doc, i](bool) { doc.align = i; ++doc.setterCalls; });
    }
    alignRow->AddGap();
    alignRow->AddButton("outdent", Icon("office/outdent.svg"), "Decrease indent", "", [] {});
    alignRow->AddButton("indent", Icon("office/indent.svg"), "Increase indent", "", [] {});
    auto spacingRows = CreateToolValueRows(prefix + "spacing-rows");
    spacingRows->AddSpinner("spacing", "Spacing", 0.5, 4.0, 0.05, 2, [&doc] { return doc.spacing; },
                            [&doc](double v) { doc.spacing = v; ++doc.setterCalls; });
    spacingRows->AddSpinner("before", "Before", 0, 200, 1, 0, [&doc] { return doc.before; },
                            [&doc](double v) { doc.before = v; ++doc.setterCalls; }, "pt");
    spacingRows->AddSpinner("after", "After", 0, 200, 1, 0, [&doc] { return doc.after; },
                            [&doc](double v) { doc.after = v; ++doc.setterCalls; }, "pt");
    paragraph->AddChild(alignRow);
    paragraph->AddChild(spacingRows);
    ToolPanelOptions paragraphOptions;
    paragraphOptions.iconPath = Icon("textformat/align-left.svg");
    paragraphOptions.helpText = "More: Paragraph (Ctrl+Shift+P)";
    paragraphOptions.dialogAction = [&doc] { ++doc.dialogs; };
    paragraphOptions.dialogTooltip = "Paragraph";
    stack.AddPanel("paragraph", "Paragraph", paragraph, paragraphOptions);

    auto simple = [&](const std::string& id, const std::string& title, const std::string& icon,
                      std::vector<std::pair<std::string, std::string>> tools, const std::string& chip) {
        auto content = CreateToolPanelContent(prefix + id);
        auto row = CreateToolButtonRow(prefix + id + "-row");
        for (const auto& t : tools) row->AddButton(id + "-" + t.first, Icon(t.second), t.first, "", [] {});
        content->AddChild(row);
        ToolPanelOptions options;
        options.iconPath = Icon(icon);
        options.open = false;
        options.chip = chip;
        stack.AddPanel(id, title, content, options);
    };
    simple("lists", "Lists", "ultramail/list-bullets.svg",
           {{"Bullets", "ultramail/list-bullets.svg"}, {"Numbering", "ultramail/list-numbers.svg"}}, "");
    simple("insert", "Insert", "image.svg",
           {{"Table", "table.svg"}, {"Picture", "image.svg"}, {"Link", "link.svg"}}, "");
    simple("page", "Page", "document.svg", {{"Page setup", "document.svg"}}, "");
    simple("review", "Review", "edit.svg", {{"Comment", "edit.svg"}, {"Track changes", "ultramail/check.svg"}}, "3");

    auto table = CreateToolPanelContent(prefix + "table");
    auto tableRow = CreateToolButtonRow(prefix + "table-row");
    tableRow->AddButton("row-above", Icon("arrow-up.svg"), "Insert row above", "", [] {});
    tableRow->AddButton("row-below", Icon("arrow-down.svg"), "Insert row below", "", [] {});
    tableRow->AddButton("col-left", Icon("arrow-left.svg"), "Insert column left", "", [] {});
    tableRow->AddButton("col-right", Icon("arrow-right.svg"), "Insert column right", "", [] {});
    table->AddChild(tableRow);
    ToolPanelOptions tableOptions;
    tableOptions.iconPath = Icon("table.svg");
    tableOptions.context = true;
    tableOptions.chip = "in a table";
    tableOptions.dialogAction = [&doc] { ++doc.dialogs; };
    stack.AddPanel("table", "Table", table, tableOptions);
}

UCEvent KeyEvent(UCEventType type, UCKeys key, bool ctrl = false, bool shift = false) {
    UCEvent event;
    event.type = type;
    event.virtualKey = key;
    event.ctrl = ctrl;
    event.shift = shift;
    return event;
}

} // namespace

int main() {
    std::cerr << "========================================" << std::endl;
    std::cerr << "   Tool Panel Stack Element Suite"       << std::endl;
    std::cerr << "========================================" << std::endl;

    if (!std::getenv("DISPLAY")) SKIP_ALL("no DISPLAY");
    UltraCanvasApplication app;
    if (!app.Initialize("ToolPanelStackElementTest")) SKIP_ALL("application would not initialise");
    // The flyout and the menus are read back from the window's surface.
    UltraCanvasWindowBase::SetPopupsInOwnWindows(false);
    WindowConfig cfg;
    cfg.title = "ToolPanelStackElementTest";
    cfg.width = kWidth;
    cfg.height = kHeight;
    cfg.resizable = false;
    auto window = CreateWindow(cfg);
    if (!window) SKIP_ALL("window could not be created");
    window->Show();

    Document doc;
    auto stack = CreateToolPanelStack("tools", 0, 0, 286, kHeight);
    AddSamplePanels(*stack, doc, "a.");
    std::vector<std::pair<std::string, bool>> openChanges;
    std::vector<std::vector<std::string>> orderChanges;
    stack->onPanelOpenChanged = [&openChanges](const std::string& id, bool open) { openChanges.push_back({id, open}); };
    stack->onOrderChanged = [&orderChanges](const std::vector<std::string>& order) { orderChanges.push_back(order); };
    window->AddChild(stack);

    // The document beside it: somewhere for the pointer to be that is not the stack.
    auto page = CreateLabel("page", 300, 0, kWidth - 300, kHeight, "The document");
    page->SetAlignment(TextAlignment::Center, VerticalAlignment::Middle);
    page->SetBackgroundColor(Color(214, 216, 222));
    window->AddChild(page);

    std::vector<std::shared_ptr<UltraCanvasUIElement>> elements = {stack, page};
    auto frames = [&](int n) {
        for (int i = 0; i < n; ++i) DisplayTest::Frame(window, elements);
    };
    frames(4);

    const char* shotDir = std::getenv("ULTRACANVAS_SCREENSHOT_DIR");
    auto shoot = [&](const char* name) {
        if (!shotDir) return;
        frames(1);
        const std::string path = std::string(shotDir) + "/" + name;
        std::cerr << (WritePpm(window, path) ? "   wrote " : "   could not write ") << path << std::endl;
    };

    auto mouse = [&](UCEventType type, int x, int y, UCMouseButton button = UCMouseButton::Left) {
        UCEvent e;
        e.type = type;
        e.button = button;
        e.pointer = Point2Di(x, y);
        e.pointerWindow = Point2Di(x, y);
        e.pointerGlobal = Point2Di(x, y);
        e.targetWindow = window;
        e.nativeWindowHandle = window->GetNativeHandle();
        app.DispatchEvent(e);
    };
    auto key = [&](UCKeys k, bool ctrl = false, bool shift = false) {
        for (UCEventType t : {UCEventType::KeyDown, UCEventType::KeyUp}) {
            UCEvent e = KeyEvent(t, k, ctrl, shift);
            e.targetWindow = window;
            e.nativeWindowHandle = window->GetNativeHandle();
            app.DispatchEvent(e);
        }
        frames(2);
    };
    // A panel's header, in window coordinates: its middle, and a point at its left.
    auto headerMiddle = [&](const std::string& id) {
        auto box = stack->GetPanel(id);
        const Point2Df at = box->GetPositionInWindow();
        return Point2Di(static_cast<int>(at.x + box->GetWidth() * 0.5f),
                        static_cast<int>(at.y + box->GetTitleBarRect().height * 0.5f));
    };
    auto windowY = [&](const std::string& id) { return stack->GetPanel(id)->GetPositionInWindow().y; };
    auto click = [&](Point2Di at) {
        mouse(UCEventType::MouseMove, at.x, at.y, UCMouseButton::NoneButton);
        mouse(UCEventType::MouseDown, at.x, at.y);
        mouse(UCEventType::MouseUp, at.x, at.y);
        frames(2);
    };
    const std::vector<std::string> defaultOrder = {"table", "clipboard", "style", "font", "paragraph",
                                                   "lists", "insert", "page", "review"};

    // ===== ADDING, OPEN AND CLOSED =====
    TEST("nine panels, in the order they were added, the context panel at the top",
         stack->GetPanelCount() == 9 && stack->GetOrder() == defaultOrder);
    TEST("a duplicate id is refused", stack->AddPanel("font", "Font again", CreateToolPanelContent("dup")) == nullptr);
    TEST("open at the start as the options say",
         stack->IsPanelOpen("clipboard") && stack->IsPanelOpen("font") && stack->IsPanelOpen("paragraph") &&
         !stack->IsPanelOpen("style") && !stack->IsPanelOpen("lists"));
    TEST("the context panel is hidden until shown",
         !stack->IsPanelShown("table") && !stack->GetPanel("table")->IsVisible() && stack->IsPanelShown("font"));
    TEST("each panel is a group named by its title, in the Header style, collapsible",
         stack->GetPanel("font")->GetAccessibleRole() == AccessibleRole::Group &&
         stack->GetPanel("font")->GetAccessibleName() == "Font" &&
         stack->GetPanel("font")->GetFrameStyle() == GroupBoxFrameStyle::Header &&
         stack->GetPanel("font")->IsCollapsible());
    TEST("the stack is a toolbar of them, 286 px wide", stack->GetAccessibleRole() == AccessibleRole::Toolbar &&
         std::abs(stack->GetWidth() - 286.0f) < 0.5f);
    TEST("a closed panel is its header alone",
         stack->GetPanel("lists")->GetHeight() <= stack->GetPanel("lists")->GetTitleBarRect().height + 2.0f &&
         stack->GetPanel("font")->GetHeight() > 100.0f);
    shoot("01-open.ppm");

    const float listsClosed = stack->GetPanel("lists")->GetHeight();
    stack->SetPanelOpen("lists", true);
    frames(2);
    TEST("SetPanelOpen opens a panel", stack->IsPanelOpen("lists") && !stack->GetPanel("lists")->IsCollapsed() &&
         stack->GetPanel("lists")->GetHeight() > listsClosed + 10.0f);
    TEST("and does not report it as the user's", openChanges.empty());
    stack->SetPanelOpen("lists", false);
    frames(2);

    click(headerMiddle("clipboard"));
    TEST("a click on a header closes the panel", !stack->IsPanelOpen("clipboard"));
    TEST("onPanelOpenChanged reports it", openChanges.size() == 1 && openChanges[0].first == "clipboard" &&
         !openChanges[0].second);
    click(headerMiddle("clipboard"));
    TEST("a second click opens it again", stack->IsPanelOpen("clipboard") && openChanges.size() == 2);

    // The "…" button: at the header's right end.
    {
        auto box = stack->GetPanel("font");
        const Point2Df at = box->GetPositionInWindow();
        const int dialogsBefore = doc.dialogs;
        click(Point2Di(static_cast<int>(at.x + box->GetWidth() - 13), static_cast<int>(at.y + box->GetTitleBarRect().height * 0.5f)));
        TEST("the header's \"…\" button opens the panel's dialog", doc.dialogs == dialogsBefore + 1 && stack->IsPanelOpen("font"));
    }

    // All closed, for the screenshot.
    for (const auto& id : stack->GetOrder()) stack->SetPanelOpen(id, false);
    frames(3);
    shoot("02-closed.ppm");
    for (const auto& id : {"clipboard", "font", "paragraph"}) stack->SetPanelOpen(id, true);
    frames(3);

    // ===== ORDER BY SetOrder =====
    stack->SetOrder({"paragraph", "font", "no-such-panel", "clipboard", "style", "lists", "insert", "page", "review", "table"});
    TEST("SetOrder takes the order named, an unknown id skipped, the context panel kept on top",
         stack->GetOrder() == std::vector<std::string>({"table", "paragraph", "font", "clipboard", "style", "lists",
                                                        "insert", "page", "review"}));
    TEST("and is not the user's change", orderChanges.empty());
    stack->SetOrder({"clipboard", "font", "paragraph", "lists", "insert", "page", "review"});
    TEST("a panel SetOrder does not name goes after the panel added before it",
         stack->GetOrder() == std::vector<std::string>({"table", "clipboard", "style", "font", "paragraph", "lists",
                                                        "insert", "page", "review"}));
    frames(3);
    TEST("the column shows the panels in that order",
         windowY("clipboard") < windowY("style") && windowY("style") < windowY("font") && windowY("font") < windowY("paragraph"));

    // ===== ORDER BY DRAGGING A HEADER =====
    {
        stack->SetOrder(defaultOrder);
        frames(3);
        const Point2Di from = headerMiddle("clipboard");
        auto paragraphBox = stack->GetPanel("paragraph");
        // Past the paragraph panel's middle: the clipboard goes below it.
        const int toY = static_cast<int>(paragraphBox->GetPositionInWindow().y + paragraphBox->GetHeight() * 0.75f);
        mouse(UCEventType::MouseMove, from.x, from.y, UCMouseButton::NoneButton);
        mouse(UCEventType::MouseDown, from.x, from.y);
        TEST("a press alone is no drag", !stack->IsDraggingPanel());
        for (int y = from.y; y <= toY; y += 12) {
            mouse(UCEventType::MouseMove, from.x, y, UCMouseButton::NoneButton);
        }
        mouse(UCEventType::MouseMove, from.x, toY, UCMouseButton::NoneButton);
        frames(2);
        TEST("moving the pressed header drags the panel", stack->IsDraggingPanel() && stack->GetDraggedPanel() == "clipboard");
        TEST("the drop index says below the paragraph panel", stack->GetDropIndex() == 4);
        // The drop line: the accent between the paragraph and the lists panel.
        {
            auto lists = stack->GetPanel("lists");
            const int lineY = static_cast<int>(std::lround(lists->GetPositionInWindow().y - 3.0f));
            const Color accent = stack->GetStyle().dropIndicatorColor;
            TEST("a drop line in the accent shows where it goes",
                 DisplayTest::RowShows(window, Rect2Di(40, lineY - 2, 200, 5), accent));
        }
        shoot("04-drag.ppm");
        mouse(UCEventType::MouseUp, from.x, toY);
        frames(3);
        TEST("the drop moves it there",
             stack->GetOrder() == std::vector<std::string>({"table", "style", "font", "paragraph", "clipboard", "lists",
                                                            "insert", "page", "review"}));
        TEST("onOrderChanged reports the new order", orderChanges.size() == 1 && orderChanges[0] == stack->GetOrder());
        TEST("the release of a drag does not fold the panel", stack->IsPanelOpen("clipboard") && !stack->IsDraggingPanel());
        TEST("the column shows it there", windowY("paragraph") < windowY("clipboard") && windowY("clipboard") < windowY("lists"));
    }

    // ===== THE KEYBOARD =====
    DisplayTest::ActivateWindow(app, window);
    stack->SetOrder(defaultOrder);
    frames(3);
    orderChanges.clear();
    TEST("FocusFirst puts the focus on the first header", stack->FocusFirst() && stack->GetFocusedPanel() == "clipboard");
    frames(2);
    {
        const Rect2Di header(static_cast<int>(stack->GetPanel("clipboard")->GetPositionInWindow().x),
                             static_cast<int>(stack->GetPanel("clipboard")->GetPositionInWindow().y),
                             static_cast<int>(stack->GetPanel("clipboard")->GetWidth()),
                             static_cast<int>(stack->GetPanel("clipboard")->GetTitleBarRect().height));
        TEST("a focus ring around the focused header", DisplayTest::RingSides(window, header, stack->GetStyle().focusColor) >= 3);
    }
    key(UCKeys::Down);
    TEST("Down goes to the next header", stack->GetFocusedPanel() == "style");
    key(UCKeys::Down);
    key(UCKeys::Up);
    TEST("Up goes back", stack->GetFocusedPanel() == "style");
    key(UCKeys::Return);
    TEST("Enter opens the focused panel", stack->IsPanelOpen("style"));
    key(UCKeys::Space);
    TEST("Space closes it again", !stack->IsPanelOpen("style"));
    key(UCKeys::Tab);
    TEST("Tab from a closed panel's header goes to the next header, past its hidden controls",
         stack->GetFocusedPanel() == "font");
    key(UCKeys::Tab);
    {
        auto* focused = window->GetFocusedElement();
        TEST("Tab from an open panel's header walks into its controls",
             focused && focused->IsDescendantOf(stack->GetPanel("font").get()) && stack->GetFocusedPanel().empty());
    }
    stack->FocusPanel("font");
    key(UCKeys::Down, true);
    TEST("Ctrl+Down moves the focused panel down", stack->GetOrder() == std::vector<std::string>(
         {"table", "clipboard", "style", "paragraph", "font", "lists", "insert", "page", "review"}));
    TEST("the focus stays on it", stack->GetFocusedPanel() == "font");
    key(UCKeys::Up, true);
    key(UCKeys::Up, true);
    TEST("Ctrl+Up moves it up, twice", stack->GetOrder() == std::vector<std::string>(
         {"table", "clipboard", "font", "style", "paragraph", "lists", "insert", "page", "review"}));
    TEST("each reported", orderChanges.size() == 3);
    stack->FocusPanel("clipboard");
    const size_t changesAtTop = orderChanges.size();
    key(UCKeys::Up, true);
    TEST("the top plain panel does not move above the context panels", stack->GetOrder()[1] == "clipboard" &&
         orderChanges.size() == changesAtTop);
    frames(2);
    TEST("the column follows the keyboard's order", windowY("font") < windowY("style"));
    window->ClearFocus();

    // ===== SAVE AND RESTORE =====
    {
        stack->SetPanelOpen("review", true);
        const ToolPanelStackState saved = stack->SaveState();
        const std::string text = saved.ToString();
        Document other;
        auto second = CreateToolPanelStack("second", 600, 0, 286, 300);
        AddSamplePanels(*second, other, "b.");
        second->RestoreState(ToolPanelStackState::FromString(text));
        bool sameOpen = true;
        // A context panel opens whenever it is shown: its state is not saved.
        for (const auto& id : stack->GetOrder()) {
            if (stack->IsContextPanel(id)) continue;
            if (second->IsPanelOpen(id) != stack->IsPanelOpen(id)) {
                std::cerr << "   open differs: " << id << " " << stack->IsPanelOpen(id) << " / " << second->IsPanelOpen(id) << std::endl;
                sameOpen = false;
            }
        }
        TEST("SaveState / RestoreState: the order comes back in another stack", second->GetOrder() == stack->GetOrder());
        TEST("and the open panels", sameOpen && second->IsPanelOpen("review") && !second->IsPanelOpen("style"));
        TEST("the state reads back as it was written", ToolPanelStackState::FromString(text) == saved);
        ToolPanelStackState odd;
        odd.order = {"a,b", "c;d", "e=f", "g%h", "plain"};
        odd.open = {"c;d"};
        odd.railMode = true;
        TEST("ids with a comma, a semicolon, = and % survive the string", ToolPanelStackState::FromString(odd.ToString()) == odd);
        TEST("an empty string is an empty state", ToolPanelStackState::FromString("").order.empty());
        second->RestoreState(ToolPanelStackState());
        TEST("and restoring it changes nothing", second->GetOrder() == stack->GetOrder());
        stack->SetPanelOpen("review", false);
    }

    // ===== CONTEXT PANELS =====
    {
        stack->SetOrder(defaultOrder);
        for (const auto& id : stack->GetOrder()) stack->SetPanelOpen(id, true);
        frames(3);
        // At the top, the pointer on the document: the panel comes in at the top, seen.
        mouse(UCEventType::MouseMove, 600, 300, UCMouseButton::NoneButton);
        const float clipboardTop = windowY("clipboard");
        stack->SetPanelShown("table", true);
        frames(3);
        TEST("a context panel shown comes in at the top", stack->IsPanelShown("table") && stack->GetPanel("table")->IsVisible() &&
             windowY("table") < windowY("clipboard") && std::abs(windowY("table") - clipboardTop) < 1.5f);
        TEST("opened", stack->IsPanelOpen("table") && !stack->GetPanel("table")->IsCollapsed());
        TEST("with its chip", stack->GetPanel("table")->GetHeaderChip() == "in a table");
        TEST("in the accent: its frame and header", stack->GetPanel("table")->GetVisualStyle().borderColor == stack->GetStyle().contextBorderColor &&
             stack->GetPanel("table")->GetVisualStyle().headerBackgroundColor == stack->GetStyle().contextHeaderColor);
        shoot("03-context.ppm");
        stack->SetPanelShown("table", false);
        frames(3);

        // Scrolled, the pointer over the paragraph panel: it stays where it is.
        auto column = std::dynamic_pointer_cast<UltraCanvasContainer>(stack->GetChildren().front());
        column->ScrollToVertical(160);
        frames(3);
        TEST("the column scrolls when its panels are taller than it", column->GetVerticalScrollPosition() > 0);
        const Point2Di over = headerMiddle("paragraph");
        mouse(UCEventType::MouseMove, over.x + 20, over.y, UCMouseButton::NoneButton);
        frames(1);
        const float before = windowY("paragraph");
        const int scrollBefore = column->GetVerticalScrollPosition();
        stack->SetPanelShown("table", true);
        frames(3);
        TEST("shown above it, the panel under the pointer does not move", std::abs(windowY("paragraph") - before) < 1.5f);
        TEST("the column scrolled by the new panel instead", column->GetVerticalScrollPosition() > scrollBefore);
        stack->SetPanelShown("table", false);
        frames(3);
        TEST("hidden again, it still does not move", std::abs(windowY("paragraph") - before) < 1.5f);

        // Scrolled, the pointer elsewhere: the first panel in view stays.
        mouse(UCEventType::MouseMove, 600, 300, UCMouseButton::NoneButton);
        column->ScrollToVertical(200);
        frames(3);
        std::string firstInView;
        for (const auto& id : stack->GetOrder()) {
            auto box = stack->GetPanel(id);
            if (!box->IsVisible()) continue;
            if (box->GetPositionInWindow().y + box->GetHeight() > 0) {
                firstInView = id;
                break;
            }
        }
        const float firstBefore = firstInView.empty() ? 0.0f : windowY(firstInView);
        stack->SetPanelShown("table", true);
        frames(3);
        TEST("with the pointer elsewhere, the first panel in view keeps its place",
             !firstInView.empty() && std::abs(windowY(firstInView) - firstBefore) < 1.5f);
        stack->SetPanelShown("table", false);
        column->ScrollToVertical(0);
        for (const auto& id : stack->GetOrder()) stack->SetPanelOpen(id, id == "clipboard" || id == "font" || id == "paragraph");
        frames(3);
    }

    // ===== THE ROWS' VALUES =====
    {
        auto content = stack->GetPanelContent("paragraph");
        UltraCanvasSpinner* spacing = dynamic_cast<UltraCanvasSpinner*>(content->FindChildById("spacing"));
        TEST("the value rows show the document's values", spacing && std::abs(spacing->GetValue() - 1.15) < 1e-6);
        const int calls = doc.setterCalls;
        if (spacing) spacing->SetValue(1.5);
        TEST("a changed spinner calls its setter", std::abs(doc.spacing - 1.5) < 1e-6 && doc.setterCalls == calls + 1);

        frames(2);
        auto fontContent = stack->GetPanelContent("font");
        UltraCanvasButton* bold = dynamic_cast<UltraCanvasButton*>(fontContent->FindChildById("bold"));
        TEST("the toggle's tooltip has the shortcut", bold && bold->GetTooltip() == "Bold (Ctrl+B)");
        if (bold) {
            const Point2Df at = bold->GetPositionInWindow();
            click(Point2Di(static_cast<int>(at.x + 14), static_cast<int>(at.y + 14)));
        }
        TEST("a click on a toggle calls its setter", doc.bold && doc.setterCalls == calls + 2);

        auto styleContent = stack->GetPanelContent("style");
        UltraCanvasDropdown* styleList = dynamic_cast<UltraCanvasDropdown*>(styleContent->FindChildById("style-list"));
        if (styleList) styleList->SetSelectedIndex(2, true);
        TEST("a list row calls its setter with the index", doc.style == 2 && doc.setterCalls == calls + 3);

        UltraCanvasColorSwatchBar* colour = dynamic_cast<UltraCanvasColorSwatchBar*>(fontContent->FindChildById("text-colour"));
        if (colour) colour->SelectColor(Color(0, 120, 215), true);
        TEST("a colour row calls its setter", doc.colour == Color(0, 120, 215) && doc.setterCalls == calls + 4);

        // The document changes under the panels; Sync shows it and calls no setter.
        doc.spacing = 2.0;
        doc.bold = false;
        doc.style = 1;
        doc.font = "Ubuntu Mono";
        doc.colour = Color(196, 43, 28);
        const int callsBeforeSync = doc.setterCalls;
        stack->Sync();
        UltraCanvasFontPicker* picker = dynamic_cast<UltraCanvasFontPicker*>(fontContent->FindChildById("font-picker"));
        TEST("Sync shows the document's values",
             spacing && std::abs(spacing->GetValue() - 2.0) < 1e-6 && bold && !bold->IsPressed() &&
             styleList && styleList->GetSelectedIndex() == 1 && picker && picker->GetCurrentFamily() == "Ubuntu Mono" &&
             colour && colour->GetSelectedColor() == Color(196, 43, 28));
        TEST("and calls no setter", doc.setterCalls == callsBeforeSync);
    }

    // ===== A NUMBER AS THE LANGUAGE WRITES IT =====
    {
        UltraLocalisation::Load("de", "");
        auto content = stack->GetPanelContent("paragraph");
        UltraCanvasSpinner* spacing = dynamic_cast<UltraCanvasSpinner*>(content->FindChildById("spacing"));
        TEST("in German a value shows a decimal comma", spacing && spacing->GetDisplayText().find("2,00") != std::string::npos);
        auto type = [&](const std::string& keys) {
            for (char c : keys) {
                UCEvent e = KeyEvent(UCEventType::KeyDown, UCKeys::Unknown);
                e.character = c;
                spacing->OnEvent(e);
            }
            spacing->OnEvent(KeyEvent(UCEventType::KeyDown, UCKeys::Return));
        };
        if (spacing) {
            spacing->SetFocus(true);
            type("1,7");
        }
        TEST("\"1,7\" typed is one point seven", std::abs(doc.spacing - 1.7) < 1e-6);
        if (spacing) type("2.5");
        TEST("the dot key puts the comma in: \"2.5\" typed is two and a half",
             std::abs(doc.spacing - 2.5) < 1e-6 && spacing && spacing->GetDisplayText().find("2,50") != std::string::npos);
        UltraLocalisation::Load("en", "");
        stack->Sync();
        TEST("in English the point again", spacing && spacing->GetDisplayText().find("2.50") != std::string::npos);
    }

    // ===== RAIL MODE AND ITS FLYOUT =====
    {
        stack->SetRailMode(true);
        frames(3);
        TEST("rail mode: a 44 px strip", stack->IsRailMode() && std::abs(stack->GetWidth() - 44.0f) < 0.5f);
        auto fontButton = stack->GetRailButton("font");
        TEST("an icon per shown panel, named by its title",
             fontButton && fontButton->IsVisible() && fontButton->GetAccessibleName() == "Font" &&
             !stack->GetRailButton("table")->IsVisible());
        shoot("05-rail.ppm");

        auto centre = [&](const std::shared_ptr<UltraCanvasButton>& b) {
            const Point2Df at = b->GetPositionInWindow();
            return Point2Di(static_cast<int>(at.x + b->GetWidth() * 0.5f), static_cast<int>(at.y + b->GetHeight() * 0.5f));
        };
        click(centre(fontButton));
        frames(2);
        TEST("a click on an icon opens its panel as a flyout",
             stack->IsFlyoutOpen() && stack->GetFlyoutPanel() == "font" && stack->GetPanel("font")->IsVisible());
        {
            auto box = stack->GetPanel("font");
            const Point2Df at = box->GetPositionInWindow();
            TEST("beside the rail, open", at.x > 44.0f && !box->IsCollapsed() && box->GetHeight() > 100.0f);
        }
        shoot("06-flyout.ppm");

        // Escape, one level at a time: the font list inside the flyout first.
        auto fontContent = stack->GetPanelContent("font");
        UltraCanvasFontPicker* picker = dynamic_cast<UltraCanvasFontPicker*>(fontContent->FindChildById("font-picker"));
        if (picker) picker->OpenList();
        frames(2);
        TEST("the font list opens inside the flyout", picker && picker->IsListOpen() && stack->IsFlyoutOpen());
        key(UCKeys::Escape);
        TEST("Escape closes the list alone", picker && !picker->IsListOpen() && stack->IsFlyoutOpen());
        key(UCKeys::Escape);
        TEST("a second Escape closes the flyout", !stack->IsFlyoutOpen() && window->IsVisible());
        TEST("the panel is back in the column, as it was",
             stack->GetPanel("font")->GetParentContainer() == stack->GetChildren().front().get() && stack->IsPanelOpen("font"));

        click(centre(fontButton));
        click(centre(stack->GetRailButton("paragraph")));
        TEST("another icon switches the flyout", stack->IsFlyoutOpen() && stack->GetFlyoutPanel() == "paragraph" &&
             stack->GetPanel("font")->GetParentContainer() == stack->GetChildren().front().get());
        click(centre(stack->GetRailButton("paragraph")));
        TEST("its own icon closes it", !stack->IsFlyoutOpen());
        click(centre(fontButton));
        TEST("open again", stack->IsFlyoutOpen());
        click(Point2Di(700, 600));
        TEST("a click outside closes it", !stack->IsFlyoutOpen());

        // A closed panel shows open in its flyout, and is closed again after.
        const size_t openChangesBefore = openChanges.size();
        click(centre(stack->GetRailButton("lists")));
        TEST("a closed panel opens in its flyout", stack->GetFlyoutPanel() == "lists" &&
             !stack->GetPanel("lists")->IsCollapsed() && stack->GetPanelContent("lists")->IsVisible());
        key(UCKeys::Escape);
        TEST("and is closed again when the flyout closes, nothing reported",
             !stack->IsFlyoutOpen() && !stack->IsPanelOpen("lists") && stack->GetPanel("lists")->IsCollapsed() &&
             openChanges.size() == openChangesBefore);

        // Keyboard on the rail: FocusFirst, Down, Enter.
        TEST("FocusFirst in rail mode focuses the first icon", stack->FocusFirst() &&
             window->GetFocusedElement() == stack->GetRailButton("clipboard").get());
        key(UCKeys::Down);
        TEST("Down goes to the next icon", window->GetFocusedElement() == stack->GetRailButton("style").get());
        window->ClearFocus();

        stack->SetRailMode(false);
        frames(3);
        TEST("out of rail mode, the column again", !stack->IsRailMode() && std::abs(stack->GetWidth() - 286.0f) < 0.5f);
        TEST("SaveState keeps the rail choice", !stack->SaveState().railMode);

        stack->SetAutoRailBelowWidth(1000.0f);
        frames(4);
        TEST("a window narrower than SetAutoRailBelowWidth folds the column", stack->IsRailMode() &&
             !stack->IsRailModeChosen());
        stack->SetAutoRailBelowWidth(800.0f);
        frames(4);
        TEST("and a wider one unfolds it", !stack->IsRailMode());
    }

    // ===== REMOVING =====
    TEST("RemovePanel takes a panel out", stack->RemovePanel("page") && !stack->HasPanel("page") &&
         stack->GetPanelCount() == 8 && !stack->RemovePanel("page"));

    window->Close();
    std::cerr << "========================================" << std::endl;
    std::cerr << "   " << (testCount - failCount) << " / " << testCount << " passed" << std::endl;
    std::cerr << "========================================" << std::endl;
    return failCount == 0 ? 0 : 1;
}
