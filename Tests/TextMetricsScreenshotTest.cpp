// Tests/TextMetricsScreenshotTest.cpp
// The text-metrics and crisp-border rules, checked on composited pixels.
//
// Single-line text centres on its capitals, a checkbox or radio
// indicator is centred on the same line as its label, a 1px border sits on
// whole pixels inside its box, and a text field's caret spans the same line
// box as its glyphs. Those are statements about pixels, so this test renders
// a window with the elements in question and reads the pixels back.
//
// It doubles as the screenshot fixture for the change: set
// ULTRACANVAS_SCREENSHOT_DIR to a directory and it writes the window there as
// PPM files (text-metrics-fields.ppm, text-metrics-menu.ppm) for a human to
// look at, e.g. after `convert text-metrics-menu.ppm menu.png`. Run it with
// GDK_SCALE=2 to render at 2x; the pixel assertions then stand down, since
// they are written for whole logical pixels, and the PPM is still read back
// at logical size (for the actual 2x pixels, screenshot the X display).
//
// Runs headless under Xvfb. Skips - rather than fails - when there is no
// display, so it stays usable on a bare CI machine. Under Xvfb there is no
// window manager to activate the window, and a window that is never activated
// draws no caret: the test hands the application the activation event itself,
// as CaretStackingTest does.
// Version: 1.0.0
// Last Modified: 2026-09-30
// Author: UltraCanvas Framework

#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasCaret.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasListModel.h"
#include "UltraCanvasListView.h"
#include "UltraCanvasMenu.h"
#include "UltraCanvasRadio.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasWindow.h"

#include <cstdio>
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
        std::cerr << "SKIP: whole suite (" << reason << ")" << std::endl;     \
        return 0;                                                             \
    } while (0)

namespace {

const Color kCaretColor(255, 0, 255);

int Luminance(const Color& c) { return (299 * c.r + 587 * c.g + 114 * c.b) / 1000; }

// The selection highlight is translucent blue over the field's white, so on
// screen it is a light blue: bluer than it is red, and not ink.
bool IsSelectionPixel(const Color& px) { return px.b > px.r + 40 && Luminance(px) > 128; }

// The vertical extent of pixels in a window rectangle that a predicate picks
// out: the ink of a label, the ring of a radio, the box of a checkbox.
struct Extent {
    int top = -1;
    int bottom = -1;
    bool found = false;
    double Centre() const { return (top + bottom) / 2.0; }
};

bool DiffersFrom(const Color& px, const Color& reference, int tolerance) {
    return std::abs(px.r - reference.r) > tolerance ||
           std::abs(px.g - reference.g) > tolerance ||
           std::abs(px.b - reference.b) > tolerance;
}

template <typename Pick>
Extent ExtentOf(const std::shared_ptr<UltraCanvasWindow>& window, const Rect2Di& area, Pick pick) {
    Extent e;
    for (int y = area.y; y < area.y + area.height; ++y) {
        for (int x = area.x; x < area.x + area.width; ++x) {
            Color px;
            if (!window->GetPixelColor(x, y, px) || !pick(px)) continue;
            if (!e.found) { e.top = y; e.found = true; }
            e.bottom = y;
            break;
        }
    }
    return e;
}

// Dark pixels: the ink of black text. Antialiased edge rows are lighter and
// fall out symmetrically at the top and bottom, so the centre is unaffected.
Extent InkExtent(const std::shared_ptr<UltraCanvasWindow>& window, const Rect2Di& area) {
    return ExtentOf(window, area, [](const Color& px) { return Luminance(px) < 128; });
}

// Anything that is not the surrounding background: a box, a ring, a mark.
Extent DrawnExtent(const std::shared_ptr<UltraCanvasWindow>& window, const Rect2Di& area, const Color& background) {
    return ExtentOf(window, area, [&](const Color& px) { return DiffersFrom(px, background, 40); });
}

// Width, in pixels, of the first run of non-background pixels met when
// walking a row to the right from `fromX`: the left border of a box. A 1px
// border stroked on whole pixels gives 1; one smeared across a pixel
// boundary gives 2.
int LeftBorderWidth(const std::shared_ptr<UltraCanvasWindow>& window, int fromX, int y, int maxX, const Color& background) {
    int run = 0;
    for (int x = fromX; x < maxX; ++x) {
        Color px;
        if (!window->GetPixelColor(x, y, px)) break;
        bool drawn = DiffersFrom(px, background, 40);
        if (drawn) {
            ++run;
        } else if (run > 0) {
            break;
        }
    }
    return run;
}

template <typename Pick>
bool AnyPixel(const std::shared_ptr<UltraCanvasWindow>& window, const Rect2Di& area, Pick pick) {
    for (int y = area.y; y < area.y + area.height; ++y) {
        for (int x = area.x; x < area.x + area.width; ++x) {
            Color px;
            if (window->GetPixelColor(x, y, px) && pick(px)) return true;
        }
    }
    return false;
}

Rect2Di WindowRect(const std::shared_ptr<UltraCanvasUIElement>& element) {
    Rect2Df r = element->GetBoundsInWindow();
    return Rect2Di(static_cast<int>(r.x), static_cast<int>(r.y),
                   static_cast<int>(r.width), static_cast<int>(r.height));
}

// The window as a binary PPM: no library needed to write it, and any image
// tool reads it.
bool WritePpm(const std::shared_ptr<UltraCanvasWindow>& window, int width, int height, const std::string& path) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out << "P6\n" << width << " " << height << "\n255\n";
    std::vector<unsigned char> row(static_cast<size_t>(width) * 3);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
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

void Frame(const std::shared_ptr<UltraCanvasWindow>& window,
           const std::vector<std::shared_ptr<UltraCanvasUIElement>>& elements) {
    for (auto& e : elements) e->RequestRedraw();
    window->UpdateAndRender();
}

} // namespace

int main() {
    std::cerr << "========================================" << std::endl;
    std::cerr << "   Text Metrics Screenshot Suite"        << std::endl;
    std::cerr << "========================================" << std::endl;

    if (!std::getenv("DISPLAY")) SKIP_ALL("no DISPLAY");

    UltraCanvasApplication app;
    if (!app.Initialize("TextMetricsScreenshotTest")) SKIP_ALL("application would not initialise");

    const int kWidth = 640, kHeight = 400;
    WindowConfig cfg;
    cfg.title = "TextMetricsScreenshotTest";
    cfg.width = kWidth;
    cfg.height = kHeight;
    cfg.resizable = false;
    auto window = CreateWindow(cfg);
    if (!window) SKIP_ALL("window could not be created");
    window->Show();

    // Caps-only labels where a centre is measured: no descender to pull the
    // ink's centre below the capitals'.
    auto checkbox = std::make_shared<UltraCanvasCheckbox>("Checkbox", 20, 20, 220, 24, "HIGH LEVEL");
    checkbox->SetChecked(true);
    auto checkbox2 = std::make_shared<UltraCanvasCheckbox>("Checkbox2", 20, 50, 220, 24, "Unchecked, lowercase only");
    auto radio = std::make_shared<UltraCanvasRadio>("Radio", 20, 80, 220, 24, "RADIO LEVEL");
    radio->SetChecked(true);
    auto button = CreateButton("Button", 260, 20, 140, 30, "Button Label");
    auto button2 = CreateButton("Button2", 260, 60, 140, 30, "lowercase only");
    auto field = CreateTextInput("Field", 20, 120, 300, 30);
    field->SetText("Same sender (info@example.com)");
    field->SetCaretPosition(field->GetText().length());   // SetText leaves the caret at 0
    // A caret colour nothing else in the window uses, so a pixel of it is
    // proof the caret is what is on screen at that spot.
    TextInputStyle fieldStyle = field->GetStyle();
    fieldStyle.caretColor = kCaretColor;
    field->SetStyle(fieldStyle);
    auto capsField = CreateTextInput("CapsField", 340, 120, 220, 30);
    capsField->SetText("HIGH LEVEL");
    auto list = std::make_shared<UltraCanvasListView>("List", 20, 170, 300, 150);
    auto model = std::make_shared<UltraCanvasSimpleListModel>();
    for (const char* label : {"All messages", "Same sender (info@example.com)", "Unread", "Needs an answer", "Payments & invoices"}) {
        model->AddItem(label);
    }
    list->SetModel(model);

    std::vector<std::shared_ptr<UltraCanvasUIElement>> elements = {
        checkbox, checkbox2, radio, button, button2, field, capsField, list};
    for (auto& e : elements) window->AddChild(e);

    // A caret only appears in a focused element of a focused window, and under
    // Xvfb there is no window manager to activate the window - so hand the
    // application the activation event the backend would have delivered.
    UCEvent activate;
    activate.type = UCEventType::WindowFocus;
    activate.targetWindow = window;
    activate.nativeWindowHandle = window->GetNativeHandle();
    app.DispatchEvent(activate);
    field->SetFocus(true);
    if (!field->IsFocused()) SKIP_ALL("the window could not be activated");

    // A text input shows its caret only while nothing is selected, so the
    // caret is measured first and the selection made afterwards.
    auto& caret = UltraCanvasCaret::GetInstance();
    for (int frame = 0; frame < 60 && !caret.IsOnWindow(window.get()); ++frame) Frame(window, elements);
    if (!caret.IsOnWindow(window.get())) SKIP_ALL("the text field never claimed the caret");
    for (int frame = 0; frame < 3; ++frame) Frame(window, elements);
    const Rect2Di caretRect = caret.GetRect();
    const bool caretShown = caret.IsPhaseVisible() &&
        AnyPixel(window, caretRect, [](const Color& px) { return !DiffersFrom(px, kCaretColor, 40); });

    const char* shotDir = std::getenv("ULTRACANVAS_SCREENSHOT_DIR");
    auto shoot = [&](const char* name) {
        if (!shotDir) return;
        std::string path = std::string(shotDir) + "/" + name;
        std::cerr << (WritePpm(window, kWidth, kHeight, path) ? "   wrote " : "   could not write ") << path << std::endl;
    };
    shoot("text-metrics-caret.ppm");

    field->SetSelection(5, 11);
    for (int frame = 0; frame < 3; ++frame) Frame(window, elements);
    shoot("text-metrics-fields.ppm");

    // The pixel rules below are written for whole logical pixels. At another
    // device scale the screenshots are still worth having; the rules are not.
    const bool scaled = std::getenv("GDK_SCALE") != nullptr || std::getenv("QT_SCALE_FACTOR") != nullptr;
    if (scaled) std::cerr << "   device scale overridden: pixel checks skipped" << std::endl;

    Color windowBg;
    window->GetPixelColor(kWidth - 10, kHeight - 10, windowBg);

    if (!scaled) {
        // ===== TEXT FIELD =====
        std::cerr << "\n--- Text field ---" << std::endl;
        std::cerr << "   caret at " << caretRect.x << "," << caretRect.y << " "
                  << caretRect.width << "x" << caretRect.height << std::endl;
        // The field's interior up to the caret column, so the caret's own
        // pixels never count as ink.
        Rect2Di fieldRect = WindowRect(field);
        Rect2Di fieldInterior(fieldRect.x + 2, fieldRect.y + 2, caretRect.x - fieldRect.x - 3, fieldRect.height - 4);
        Extent ink = InkExtent(window, fieldInterior);
        TEST("The field's text is on screen", ink.found);
        TEST("The caret spans the text's line box: at or above the ink's top",
             ink.found && caretRect.y <= ink.top);
        TEST("The caret spans the text's line box: at or below the ink's bottom",
             ink.found && caretRect.y + caretRect.height >= ink.bottom + 1);
        TEST("The caret was on the composited window before the selection was made", caretShown);
        TEST("The selection highlight is on the composited window",
             AnyPixel(window, fieldInterior, IsSelectionPixel));

        Rect2Di capsRect = WindowRect(capsField);
        Rect2Di capsInterior(capsRect.x + 2, capsRect.y + 2, capsRect.width - 4, capsRect.height - 4);
        Extent caps = InkExtent(window, capsInterior);
        double capsCentre = capsRect.y + capsRect.height / 2.0;
        std::cerr << "   caps ink rows " << caps.top << ".." << caps.bottom
                  << " in a field centred on " << capsCentre << std::endl;
        TEST("A caps-only field centres its capitals on the field (within a pixel)",
             caps.found && std::abs(caps.Centre() - capsCentre) <= 1.0);

        // ===== CHECKBOX =====
        std::cerr << "\n--- Checkbox ---" << std::endl;
        Rect2Di cbRect = WindowRect(checkbox);
        const int boxSize = static_cast<int>(checkbox->GetVisualStyle().boxSize);
        Rect2Di boxArea(cbRect.x, cbRect.y, boxSize + 4, cbRect.height);
        Rect2Di labelArea(cbRect.x + boxSize + 6, cbRect.y, cbRect.width - boxSize - 6, cbRect.height);
        Extent box = DrawnExtent(window, boxArea, windowBg);
        Extent label = InkExtent(window, labelArea);
        std::cerr << "   box rows " << box.top << ".." << box.bottom
                  << ", label ink rows " << label.top << ".." << label.bottom << std::endl;
        TEST("The checkbox box and its caps-only label share a centre line (within a pixel)",
             box.found && label.found && std::abs(box.Centre() - label.Centre()) <= 1.0);
        int border = box.found
                     ? LeftBorderWidth(window, cbRect.x - 2, static_cast<int>(box.Centre()), cbRect.x + boxSize, windowBg)
                     : 0;
        std::cerr << "   left border run " << border << " px" << std::endl;
        TEST("The checkbox border is one crisp pixel wide, not smeared over two", border == 1);

        // ===== RADIO =====
        std::cerr << "\n--- Radio ---" << std::endl;
        Rect2Di rdRect = WindowRect(radio);
        Rect2Di ringArea(rdRect.x, rdRect.y, boxSize + 4, rdRect.height);
        Rect2Di rdLabelArea(rdRect.x + boxSize + 6, rdRect.y, rdRect.width - boxSize - 6, rdRect.height);
        Extent ring = DrawnExtent(window, ringArea, windowBg);
        Extent rdLabel = InkExtent(window, rdLabelArea);
        TEST("The radio ring and its caps-only label share a centre line (within a pixel)",
             ring.found && rdLabel.found && std::abs(ring.Centre() - rdLabel.Centre()) <= 1.0);
    }

    // ===== MENU =====
    // The UltraMail "Show emails" filter menu, item for item, on the default
    // style: round radio outlines, level indicators, a disabled item greyed.
    std::cerr << "\n--- Menu ---" << std::endl;
    auto menu = CreateMenu("Menu", 0, 0, 260, 200);
    menu->SetMenuType(MenuType::PopupMenu);
    MenuStyle menuStyle = MenuStyle::Default();
    menuStyle.showShadow = false;
    menu->SetStyle(menuStyle);
    menu->AddItem(MenuItemData::Radio("HIGH LEVEL", 1, true, [] {}));
    menu->AddItem(MenuItemData::Radio("Same sender (info@example.com)", 1, false, [] {}));
    menu->AddItem(MenuItemData::Checkbox("Unread", false, [](bool) {}));
    menu->AddItem(MenuItemData::Checkbox("Needs an answer", true, [](bool) {}));
    menu->AddItem(MenuItemData::Checkbox("Spam", false, [](bool) {}));
    menu->AddItem(MenuItemData::Separator());
    menu->AddItem(MenuItemData::Action("Mark as unread", [] {}));
    MenuItemData disabledItem = MenuItemData::Checkbox("Disabled, checked", true, [](bool) {});
    disabledItem.enabled = false;
    menu->AddItem(disabledItem);
    PopupElementSettings popupSettings;
    menu->OpenMenu(Point2Di(340, 170), *window, popupSettings);
    for (int frame = 0; frame < 5; ++frame) Frame(window, elements);

    shoot("text-metrics-menu.ppm");

    if (!scaled) {
        Rect2Df mb = menu->GetBoundsInWindow();
        Color menuBg = menuStyle.backgroundColor;
        const int rowTop = static_cast<int>(mb.y) + menuStyle.paddingTop;
        Rect2Di indicatorArea(static_cast<int>(mb.x) + menuStyle.paddingLeft - 1, rowTop,
                              menuStyle.iconSize + 2, menuStyle.itemHeight);
        Rect2Di menuLabelArea(indicatorArea.x + indicatorArea.width + menuStyle.iconSpacing, rowTop,
                              static_cast<int>(mb.width) - indicatorArea.width - menuStyle.paddingLeft - menuStyle.paddingRight, menuStyle.itemHeight);
        Extent indicator = DrawnExtent(window, indicatorArea, menuBg);
        Extent menuLabel = InkExtent(window, menuLabelArea);
        std::cerr << "   radio rows " << indicator.top << ".." << indicator.bottom
                  << ", label ink rows " << menuLabel.top << ".." << menuLabel.bottom << std::endl;
        TEST("The menu's radio indicator and its caps-only label share a centre line (within a pixel)",
             indicator.found && menuLabel.found && std::abs(indicator.Centre() - menuLabel.Centre()) <= 1.0);
        TEST("The menu's radio outline is as tall as iconSize: a circle, not a box with a dot",
             indicator.found && indicator.bottom - indicator.top + 1 == menuStyle.iconSize);
    }

    menu->CloseMenu();
    for (int frame = 0; frame < 3; ++frame) Frame(window, elements);

    std::cerr << "\n========================================" << std::endl;
    std::cerr << "   " << (testCount - failCount) << "/" << testCount << " passed" << std::endl;
    std::cerr << "========================================" << std::endl;
    return failCount == 0 ? 0 : 1;
}
