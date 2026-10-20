// Tests/ColorPaletteManagerTest.cpp
// The palette rows under a colour picker (UltraCanvasColorPaletteManager),
// the swatch bar's wrapping and the picker's foreground-only mode.
//
//  - A wrapping swatch bar keeps its swatches at least minSwatchSize, shares
//    the colours out evenly over its lines and reports the height they take.
//  - Palettes go into one settings line and come back the same.
//  - One palette: its row ends in "+" alone. Several: every row has "-",
//    and only the last one "+". The buttons stay inside the element.
//  - "+" opens the gallery; adding a preset adds a row; "-" removes one.
//  - A right click on a swatch opens the entry editor: a picker with no
//    background colour that sets that one entry.
//
// Every press goes through the application's own dispatch. Opens a real
// window, so it runs under Xvfb (xvfb-run -a) and skips the window part
// without a DISPLAY. With ULTRACANVAS_SCREENSHOT_DIR set it writes the
// window with the gallery and with the entry editor open as PPM files.
// Version: 1.0.0
// Last Modified: 2026-10-19
// Author: UltraCanvas Framework

#include "DisplayTestSupport.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasColorPaletteManager.h"
#include "UltraCanvasColorPicker.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasWindow.h"

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

namespace {

constexpr int kWidth = 760;
constexpr int kHeight = 640;
constexpr float kManagerW = 250.0f;

UltraCanvasUIElement* Find(UltraCanvasUIElement* root, const std::string& id) {
    if (!root) return nullptr;
    if (root->GetIdentifier() == id) return root;
    if (auto* c = dynamic_cast<UltraCanvasContainer*>(root)) {
        for (const auto& child : c->GetChildren()) {
            if (auto* hit = Find(child.get(), id)) return hit;
        }
    }
    return nullptr;
}

void Click(UltraCanvasApplication& app, const std::shared_ptr<UltraCanvasWindow>& window,
           const UltraCanvasUIElement& in, float localX, float localY,
           UCMouseButton button = UCMouseButton::Left) {
    const Rect2Df b = in.GetBoundsInWindow();
    for (UCEventType type : {UCEventType::MouseDown, UCEventType::MouseUp}) {
        UCEvent e;
        e.type = type;
        e.button = button;
        e.targetWindow = window;
        e.nativeWindowHandle = window->GetNativeHandle();
        e.pointerWindow = Point2Di(static_cast<int>(std::lround(b.x + localX)),
                                   static_cast<int>(std::lround(b.y + localY)));
        e.pointer = e.pointerWindow;
        app.DispatchEvent(e);
    }
}

void Frames(const std::shared_ptr<UltraCanvasWindow>& window,
            const std::vector<std::shared_ptr<UltraCanvasUIElement>>& elements) {
    for (int frame = 0; frame < 4; ++frame) DisplayTest::Frame(window, elements);
}

bool Inside(const Rect2Df& inner, const Rect2Df& outer) {
    return inner.x >= outer.x - 0.5f && inner.y >= outer.y - 0.5f &&
           inner.x + inner.width <= outer.x + outer.width + 0.5f &&
           inner.y + inner.height <= outer.y + outer.height + 0.5f;
}

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

} // namespace

int main() {
    std::cerr << "========================================" << std::endl;
    std::cerr << "   Colour Palette Manager Suite"          << std::endl;
    std::cerr << "========================================" << std::endl;

    // ===== Wrapping swatch bar =====
    {
        UltraCanvasColorSwatchBar bar("wrap", 230, 10);
        ColorSwatchBarStyle s = bar.GetStyle();
        s.padding = 1.0f;
        s.spacing = 1.0f;
        s.minSwatchSize = 8.0f;
        s.swatchSize = 16.0f;
        bar.SetStyle(s);
        bar.SetColors(std::vector<Color>(256, Colors::Black));
        TEST("unwrapped, the bar wants one row", std::fabs(bar.HeightForWidth(230) - 18.0f) < 0.5f);
        bar.SetWrap(true);
        // 228 px across: 25 swatches of 8 + 24 gaps fit a line, so 11 lines
        // of at most 24, at 8 px.
        const float h = bar.HeightForWidth(230);
        TEST("256 colours wrap into a block of lines", h > 80.0f && h < 140.0f);
        bar.SetElementSize(Size2Df(230, h));
        bar.SetBounds(Rect2Df(0, 0, 230, h));
        TEST("each swatch at least the minimum", bar.GetEffectiveSwatchSize() >= 8.0f - 0.01f);

        UltraCanvasColorSwatchBar few("few", 230, 10);
        few.SetStyle(s);
        few.SetColors(std::vector<Color>(4, Colors::White));
        few.SetWrap(true);
        TEST("a few colours stay on one line at the preferred size",
             std::fabs(few.HeightForWidth(230) - (2.0f + 16.0f)) < 0.5f);
    }

    // ===== Settings line =====
    {
        std::vector<NamedColorPalette> palettes = {
            UltraCanvasColorPaletteManager::DefaultPalette(),
            {"Mine|;odd", "", {Color(1, 2, 3, 255), Color(255, 128, 0, 255)}},
        };
        const std::string line = UltraCanvasColorPaletteManager::SerializePalettes(palettes);
        std::vector<NamedColorPalette> back;
        TEST("the line parses", UltraCanvasColorPaletteManager::ParsePalettes(line, back));
        TEST("both palettes come back", back.size() == 2);
        TEST("with their colours", back.size() == 2 && back[0].colors == palettes[0].colors &&
                                   back[1].colors == palettes[1].colors);
        TEST("a name loses the separators", back.size() == 2 && back[1].name == "Mineodd");
        TEST("a preset gets its category back", !back.empty() && back[0].category == "Basics");
        std::vector<NamedColorPalette> untouched = palettes;
        TEST("text with no palette is refused",
             !UltraCanvasColorPaletteManager::ParsePalettes("nonsense", untouched) &&
             untouched.size() == 2);
        bool hasGif = false;
        for (const auto& p : UltraCanvasColorPaletteManager::PresetPalettes()) {
            if (p.colors.size() == 256) hasGif = true;
        }
        TEST("the gallery offers a whole 256-colour GIF palette", hasGif);
    }

    if (!std::getenv("DISPLAY")) {
        std::cerr << "SKIP: window part (no DISPLAY)" << std::endl;
        std::cerr << "\n" << (testCount - failCount) << "/" << testCount << " passed" << std::endl;
        return failCount == 0 ? 0 : 1;
    }

    UltraCanvasApplication app;
    if (!app.Initialize("ColorPaletteManagerTest")) {
        std::cerr << "SKIP: application would not initialise" << std::endl;
        return failCount == 0 ? 0 : 1;
    }
    WindowConfig cfg;
    cfg.title = "ColorPaletteManagerTest";
    cfg.width = kWidth;
    cfg.height = kHeight;
    auto window = CreateWindow(cfg);
    if (!window) {
        std::cerr << "SKIP: window could not be created" << std::endl;
        return failCount == 0 ? 0 : 1;
    }
    window->Show();
    DisplayTest::ActivateWindow(app, window);

    // A toolbox column on the right, as UltraPaint's Colour card.
    auto toolbox = std::make_shared<UltraCanvasContainer>("toolbox", kWidth - 280, 20, 266, 600);
    toolbox->layout.SetFlexColumn().SetFlexGap(6);
    toolbox->SetPadding(8);
    toolbox->SetBackgroundColor(Colors::White);
    window->AddChild(toolbox);
    auto picker = CreateColorPicker("picker", Color(0xFF, 0x80, 0x00, 0xFF), 0, 0, kManagerW, 300);
    picker->SetStyle(ColorPickerStyle::Light());
    picker->SetUIScale(0.78f);
    picker->SetAutoHeight(true);
    picker->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    toolbox->AddChild(picker);
    auto manager = CreateColorPaletteManager("pal", 0, 0, kManagerW);
    manager->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    toolbox->AddChild(manager);
    int changes = 0;
    manager->onPalettesChanged = [&changes]() { ++changes; };
    Color picked = Colors::Transparent;
    manager->onColorSelected = [&picked](const Color& c) { picked = c; };
    const std::vector<std::shared_ptr<UltraCanvasUIElement>> all = {toolbox, picker, manager};
    Frames(window, all);

    // ===== One palette =====
    auto* add0 = Find(manager.get(), "pal-row0-add");
    TEST("one palette: its row ends in +", add0 != nullptr);
    TEST("and has no -", Find(manager.get(), "pal-row0-remove") == nullptr);
    TEST("the + is inside the element",
         add0 && Inside(add0->GetBoundsInWindow(), manager->GetBoundsInWindow()));
    auto* bar0 = dynamic_cast<UltraCanvasColorSwatchBar*>(Find(manager.get(), "pal-row0-bar"));
    TEST("the row's swatches are inside the element",
         bar0 && Inside(bar0->GetBoundsInWindow(), manager->GetBoundsInWindow()));
    TEST("the default row is one line", bar0 && bar0->GetHeight() < 20.0f);

    // A left click picks the colour.
    if (bar0) {
        const float s = bar0->GetEffectiveSwatchSize();
        Click(app, window, *bar0, 1.0f + 6 * (s + 2.0f) + s * 0.5f, 1.0f + s * 0.5f);
    }
    TEST("a click on the seventh swatch picks red",
         picked.r == 229 && picked.g == 57 && picked.b == 53);

    // ===== The gallery =====
    if (add0) Click(app, window, *add0, 7, 7);
    Frames(window, all);
    auto* active = window->GetActivePopupElement();
    TEST("+ opens the gallery", active && active->element &&
                                active->element->GetIdentifier() == "pal-gallery");
    if (const char* dir = std::getenv("ULTRACANVAS_SCREENSHOT_DIR")) {
        WritePpm(window, std::string(dir) + "/palette-gallery.ppm");
    }
    UltraCanvasUIElement* gallery = active ? active->element : nullptr;
    // The Web-safe preset's own + (the gallery's cards are numbered in order).
    int webIndex = -1;
    const auto& presets = UltraCanvasColorPaletteManager::PresetPalettes();
    for (size_t i = 0; i < presets.size(); ++i) {
        if (presets[i].name == "GIF 256 (RGB 3-3-2)") webIndex = static_cast<int>(i);
    }
    auto* addGif = Find(gallery, "pal-gallery-p" + std::to_string(webIndex) + "-add");
    TEST("the GIF palette has an add button", addGif != nullptr);
    // Scroll it into view is the list's business; the click is dispatched
    // where the button is, wherever that is.
    if (addGif) dynamic_cast<UltraCanvasButton*>(addGif)->onClick();
    Frames(window, all);
    TEST("adding it makes a second row", manager->GetPaletteCount() == 2);
    TEST("and reports the change", changes == 1);
    TEST("the button turns into a check mark",
         addGif && dynamic_cast<UltraCanvasButton*>(addGif)->GetText() != "+");
    manager->ClosePopups();
    Frames(window, all);

    // ===== Several palettes =====
    TEST("two palettes: the first row has -", Find(manager.get(), "pal-row0-remove") != nullptr);
    TEST("but no +", Find(manager.get(), "pal-row0-add") == nullptr);
    TEST("the last row has - and +", Find(manager.get(), "pal-row1-remove") != nullptr &&
                                     Find(manager.get(), "pal-row1-add") != nullptr);
    auto* bar1 = Find(manager.get(), "pal-row1-bar");
    TEST("the GIF row wraps into a block", bar1 && bar1->GetHeight() > 60.0f);
    TEST("and the element grew for it",
         std::fabs(manager->GetHeight() - manager->HeightForWidth(kManagerW)) < 0.5f &&
         manager->GetHeight() > 70.0f);
    auto* add1 = Find(manager.get(), "pal-row1-add");
    TEST("the last row's + is inside the element",
         add1 && Inside(add1->GetBoundsInWindow(), manager->GetBoundsInWindow()));

    // ===== The entry editor =====
    auto* firstBar = Find(manager.get(), "pal-row0-bar");
    if (firstBar) Click(app, window, *firstBar, 6, 6, UCMouseButton::Right);
    Frames(window, all);
    active = window->GetActivePopupElement();
    TEST("a right click opens the entry editor", active && active->element &&
                                                 active->element->GetIdentifier() == "pal-editor");
    if (const char* dir = std::getenv("ULTRACANVAS_SCREENSHOT_DIR")) {
        WritePpm(window, std::string(dir) + "/palette-editor.ppm");
    }
    auto* editorPicker = dynamic_cast<UltraCanvasColorPicker*>(
            Find(active ? active->element : nullptr, "pal-editor-picker"));
    TEST("its picker has no background colour",
         editorPicker && !editorPicker->GetShowBackgroundColor());
    if (editorPicker) editorPicker->SetColor(Color(10, 20, 30, 255), true);
    TEST("setting its colour sets the entry",
         manager->GetPalettes()[0].colors[0] == Color(10, 20, 30, 255));
    auto* revert = dynamic_cast<UltraCanvasButton*>(Find(active ? active->element : nullptr, "pal-editor-revert"));
    if (revert) revert->onClick();
    TEST("Revert puts it back", manager->GetPalettes()[0].colors[0] == Color(255, 255, 255, 255));
    Frames(window, all);

    // ===== Removing =====
    auto* remove0 = Find(manager.get(), "pal-row0-remove");
    if (remove0) Click(app, window, *remove0, 7, 7);
    Frames(window, all);
    TEST("- removes its palette", manager->GetPaletteCount() == 1 &&
                                  manager->GetPalettes()[0].colors.size() == 256);
    TEST("the one row left has + again", Find(manager.get(), "pal-row0-add") != nullptr &&
                                         Find(manager.get(), "pal-row0-remove") == nullptr);

    std::cerr << "\n" << (testCount - failCount) << "/" << testCount << " passed" << std::endl;
    return failCount == 0 ? 0 : 1;
}
