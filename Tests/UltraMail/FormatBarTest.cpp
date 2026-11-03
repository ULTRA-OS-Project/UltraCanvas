// Tests/UltraMail/FormatBarTest.cpp
// The compose window's formatting bar (UltraMailFormatBar) on a real rich
// text editor:
//   * in one row (Options::singleRow) every tool stands in that row;
//   * Font and Size show the font and size of the text at the cursor -
//     "Default" for text without one, a font or size the list lacks added to
//     it - and keep what they showed over a selection of several;
//   * the Text | Background switch decides what a swatch colours: the
//     letters, or the background behind them, where white takes it away;
//   * hiding the tools keeps the host's own control in the row.
//
// Opens a real window, so it runs under Xvfb (xvfb-run -a) and skips itself
// without a DISPLAY.
// Version: 1.0.0
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasColorSwatchBar.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasRichTextEdit.h"
#include "UltraCanvasSegmentedControl.h"
#include "UltraCanvasWindow.h"
#include "UltraMailFormatBar.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

using namespace UltraCanvas;
using namespace UltraMail;

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
        std::cerr << "SKIP: whole suite (" << reason << ")" << std::endl;     \
        return 0;                                                             \
    } while (0)

namespace {

std::shared_ptr<UltraCanvasUIElement> Find(const std::shared_ptr<UltraCanvasUIElement>& element,
                                           const std::string& id) {
    if (!element) return nullptr;
    if (element->GetIdentifier() == id) return element;
    if (auto* box = dynamic_cast<UltraCanvasContainer*>(element.get()))
        for (const auto& child : box->GetChildren())
            if (auto hit = Find(child, id)) return hit;
    return nullptr;
}

std::string Shown(const std::shared_ptr<UltraCanvasUIElement>& element) {
    auto dropdown = std::dynamic_pointer_cast<UltraCanvasDropdown>(element);
    const DropdownItem* item = dropdown ? dropdown->GetSelectedItem() : nullptr;
    return item ? item->text : std::string("(nothing)");
}

RichTextRun Run(const std::string& text, const std::string& family, float size) {
    RichTextRun run;
    run.text = text;
    run.fontFamily = family;
    run.fontSizePt = size;
    return run;
}

} // namespace

int main() {
    std::cerr << "========================================" << std::endl;
    std::cerr << "   UltraMail Format Bar Suite"            << std::endl;
    std::cerr << "========================================" << std::endl;

    if (!std::getenv("DISPLAY")) SKIP_ALL("no DISPLAY");

    UltraCanvasApplication app;
    if (!app.Initialize("FormatBarTest")) SKIP_ALL("application would not initialise");

    WindowConfig cfg;
    cfg.title = "FormatBarTest";
    cfg.width = 1140;
    cfg.height = 400;
    auto window = CreateWindow(cfg);
    if (!window) SKIP_ALL("window could not be created");
    window->Show();   // UpdateAndRender() is a no-op on an unmapped window

    auto page = CreateContainer("page", 0, 0, 1140, 400);
    page->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    window->AddChild(page);

    // One paragraph: plain text, then Georgia 14, then Calibri 10.5 - a font
    // and a size the lists do not offer, as a reply's quoted HTML brings them.
    auto doc = std::make_shared<UCRichDocument>();
    RichDocBlock block;
    block.runs.push_back(Run("Plain ", "", 0.0f));
    block.runs.push_back(Run("Georgia ", "Georgia", 14.0f));
    block.runs.push_back(Run("Calibri", "Calibri", 10.5f));
    doc->blocks.push_back(block);

    auto rich = CreateRichTextEdit("rich", 0, 0, 0, 200);
    rich->SetDocument(doc);

    FormatBar::Options options;
    options.idPrefix = "fmt";
    options.quoteTools = true;
    options.singleRow = true;
    UltraCanvasRichTextEdit* editor = rich.get();
    options.editor = [editor]() { return editor; };
    FormatBar bar = FormatBar::Build(options);
    auto hostControl = CreateButton("host", 0, 0, 80, 24, "Host");
    bar.paragraphRow->AddChild(hostControl);
    page->AddChild(bar.root);
    page->AddChild(rich);
    rich->onSelectionChanged = [&bar, editor]() { bar.Sync(editor->GetFormatState()); };

    auto render = [&]() {
        for (int frame = 0; frame < 3; ++frame) {
            page->RequestRedraw();
            window->UpdateAndRender();
        }
    };
    render();

    // ----- one row -----
    auto bold = Find(bar.root, "fmtBold");
    auto picture = Find(bar.root, "fmtPicture");
    TEST("one row: the bar holds a single row", bar.root->GetChildren().size() == 1);
    TEST("one row: Bold and Picture stand side by side",
         bold && picture && std::abs(bold->GetBoundsInWindow().y - picture->GetBoundsInWindow().y) < 1.0f
         && picture->GetBoundsInWindow().x > bold->GetBoundsInWindow().x);
    auto middle = [](const UltraCanvasUIElement& e) {
        const Rect2Df box = e.GetBoundsInWindow();
        return box.y + box.height / 2;
    };
    TEST("one row: the host's control is in the row",
         std::abs(middle(*hostControl) - middle(*bold)) < 1.5f);
    TEST("one row: everything fits in the window",
         hostControl->GetBoundsInWindow().x + hostControl->GetWidth() <= cfg.width);

    // ----- Font and Size follow the cursor -----
    auto font = Find(bar.root, "fmtFont");
    auto size = Find(bar.root, "fmtSize");
    UCRichDocumentEditor& core = rich->GetEditor();
    core.SetCaret(RichDocPosition(0, 2));
    TEST("plain text: Font shows Default (" + Shown(font) + ")", Shown(font) == "Default");
    TEST("plain text: Size shows Default (" + Shown(size) + ")", Shown(size) == "Default");
    core.SetCaret(RichDocPosition(0, 9));   // inside "Georgia "
    TEST("in Georgia 14: Font shows Georgia (" + Shown(font) + ")", Shown(font) == "Georgia");
    TEST("in Georgia 14: Size shows 14 (" + Shown(size) + ")", Shown(size) == "14");
    core.SetCaret(RichDocPosition(0, 17));  // inside "Calibri"
    TEST("in Calibri 10.5: Font shows Calibri (" + Shown(font) + ")", Shown(font) == "Calibri");
    TEST("in Calibri 10.5: Size shows 10.5 (" + Shown(size) + ")", Shown(size) == "10.5");
    core.SetSelection(RichDocPosition(0, 0), RichDocPosition(0, 21));
    TEST("over three fonts: Font keeps what it showed", Shown(font) == "Calibri");

    // ----- Text | Background -----
    auto target = std::dynamic_pointer_cast<UltraCanvasSegmentedControl>(Find(bar.root, "fmtColourTarget"));
    auto swatches = std::dynamic_pointer_cast<UltraCanvasColorSwatchBar>(Find(bar.root, "fmtColour"));
    TEST("the colour switch and the swatches are there", target && swatches);
    if (target && swatches && swatches->onColorSelected) {
        TEST("the switch starts on Text", target->GetSelectedIndex() == 0);
        core.SetSelection(RichDocPosition(0, 0), RichDocPosition(0, 5));   // "Plain"
        swatches->onColorSelected(Color(192, 57, 43));
        const RichTextRun& red = core.GetBlock(0).runs[0];
        TEST("Text: a swatch colours the letters", red.color == "#C0392B" && red.highlightColor.empty());

        target->SetSelectedIndex(1);
        core.SetSelection(RichDocPosition(0, 0), RichDocPosition(0, 5));
        swatches->onColorSelected(Color(255, 245, 157));
        TEST("Background: a swatch colours the background",
             core.GetBlock(0).runs[0].highlightColor == "#FFF59D");
        core.SetSelection(RichDocPosition(0, 0), RichDocPosition(0, 5));
        swatches->onColorSelected(Color(255, 255, 255));
        TEST("Background: white takes the background away",
             core.GetBlock(0).runs[0].highlightColor.empty());
        TEST("Background: the letters keep their colour", core.GetBlock(0).runs[0].color == "#C0392B");
    }

    // ----- plain text: the tools go, the host's control stays -----
    bar.SetToolsVisible(false);
    render();
    TEST("tools hidden: Bold is hidden", !bold->IsVisible());
    TEST("tools hidden: the host's control is still shown", hostControl->IsVisible());

    std::cerr << "----------------------------------------" << std::endl;
    std::cerr << (testCount - failCount) << "/" << testCount << " passed" << std::endl;
    return failCount == 0 ? 0 : 1;
}
