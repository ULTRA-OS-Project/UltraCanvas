// Tests/CaretStackingTest.cpp
// Where the shared text caret sits in the window's stacking order.
//
// The caret is not drawn by the widget that owns it: UltraCanvasCaret paints it
// into its own small surface and the window blends it in while compositing.
// Blending it in last put it above everything, so a menu opened over the text
// cursor had the cursor blinking through the menu's items. The caret belongs to
// the layer of the widget that owns it, and these tests read the composited
// window pixels back to prove it does.
//
// Runs headless under Xvfb. Skips - rather than fails - when there is no
// display, so it stays usable on a bare CI machine.
// Version: 1.0.0
// Last Modified: 2026-09-13
// Author: UltraCanvas Framework

#include "UltraCanvasApplication.h"
#include "UltraCanvasCaret.h"
#include "UltraCanvasMenu.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasWindow.h"

#include <cstdlib>
#include <iostream>
#include <memory>

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

// A caret colour nothing else in the window uses, so a pixel of it is proof the
// caret - and only the caret - is what is on screen at that spot.
const Color kCaretColor(255, 0, 255);

bool NearlyEqual(const Color& a, const Color& b, int tolerance = 24) {
    auto close = [tolerance](uint8_t l, uint8_t r) {
        return std::abs(static_cast<int>(l) - static_cast<int>(r)) <= tolerance;
    };
    return close(a.r, b.r) && close(a.g, b.g) && close(a.b, b.b);
}

// The harness drives frames by hand instead of running the event loop, so it
// marks the text area dirty itself - what is under test is the compositing
// order, not the dirty-rect plumbing.
void Frame(const std::shared_ptr<UltraCanvasWindow>& window,
           const std::shared_ptr<UltraCanvasTextArea>& area) {
    area->RequestRedraw();
    window->UpdateAndRender();
}

// Is any pixel of the caret's rectangle showing the caret's colour?
bool CaretPixelsOnScreen(const std::shared_ptr<UltraCanvasWindow>& window,
                         const Rect2Di& caretRect) {
    for (int y = caretRect.y; y < caretRect.y + caretRect.height; ++y) {
        for (int x = caretRect.x; x < caretRect.x + caretRect.width; ++x) {
            Color px;
            if (!window->GetPixelColor(x, y, px)) continue;
            if (NearlyEqual(px, kCaretColor)) return true;
        }
    }
    return false;
}

MenuItemData Item(const std::string& label) {
    MenuItemData item;
    item.label = label;
    return item;
}

} // namespace

int main() {
    std::cerr << "========================================" << std::endl;
    std::cerr << "   Caret Stacking Order Suite"           << std::endl;
    std::cerr << "========================================" << std::endl;

    if (!std::getenv("DISPLAY")) SKIP_ALL("no DISPLAY");

    UltraCanvasApplication app;
    if (!app.Initialize("CaretStackingTest")) SKIP_ALL("application would not initialise");

    WindowConfig cfg;
    cfg.title = "CaretStackingTest";
    cfg.width = 600;
    cfg.height = 400;
    auto window = CreateWindow(cfg);
    if (!window) SKIP_ALL("window could not be created");

    // UpdateAndRender() is a no-op on an unmapped window, and every check here
    // depends on frames actually being drawn.
    window->Show();

    auto area = std::make_shared<UltraCanvasTextArea>("Editor", 0, 0, 580, 380);
    area->SetCursorColor(kCaretColor);
    window->AddChild(area);
    area->SetText("The first step for the fix\nis a caret to hide.\n");

    // A caret only appears in a focused element of a focused window, and under
    // Xvfb there is no window manager to activate the window - so hand the
    // application the activation event the backend would have delivered.
    UCEvent activate;
    activate.type = UCEventType::WindowFocus;
    activate.targetWindow = window;
    activate.nativeWindowHandle = window->GetNativeHandle();
    app.DispatchEvent(activate);
    area->SetFocus(true);
    if (!area->IsFocused()) SKIP_ALL("the window could not be activated");

    auto& caret = UltraCanvasCaret::GetInstance();
    for (int frame = 0; frame < 60 && !caret.IsOnWindow(window.get()); ++frame) {
        Frame(window, area);
    }
    if (!caret.IsOnWindow(window.get())) SKIP_ALL("the text area never claimed the caret");

    const Rect2Di caretRect = caret.GetRect();
    std::cerr << "   caret at " << caretRect.x << "," << caretRect.y
              << " " << caretRect.width << "x" << caretRect.height << std::endl;

    // ===== NO OVERLAY =====
    std::cerr << "\n--- With nothing over it ---" << std::endl;
    TEST("A caret in the window content reports no popup layer",
         caret.GetPopupLayer() == nullptr);
    Frame(window, area);
    TEST("The caret is on the composited window",
         caret.IsPhaseVisible() && CaretPixelsOnScreen(window, caretRect));

    // ===== MENU OVER THE CARET =====
    // The bug this suite exists for: Texter's Edit menu opened over the editing
    // position and the caret went on blinking on top of the menu items.
    std::cerr << "\n--- With a menu opened over it ---" << std::endl;
    auto menu = std::make_shared<UltraCanvasMenu>("EditMenu", 220, 160);
    MenuStyle menuStyle = MenuStyle::Default();
    menuStyle.showShadow = false;      // keep the menu's own pixels opaque
    menuStyle.borderRadius = 0;        // ... including its corners
    menu->SetStyle(menuStyle);
    menu->SetMenuType(MenuType::PopupMenu);
    menu->AddItem(Item("Undo"));
    menu->AddItem(Item("Redo"));
    menu->AddItem(Item("Cut"));
    menu->AddItem(Item("Copy"));
    menu->AddItem(Item("Paste"));

    // Open it so that the caret is well inside the menu, not on its border.
    PopupElementSettings popupSettings;
    menu->OpenMenu(Point2Di(caretRect.x - 30, caretRect.y - 20), *window, popupSettings);
    for (int frame = 0; frame < 5; ++frame) Frame(window, area);

    TEST("The menu covers the caret",
         menu->GetBoundsInWindow().Contains(
                 Point2Df((float)caretRect.x, (float)caretRect.y)) &&
         menu->GetBoundsInWindow().Contains(
                 Point2Df((float)(caretRect.x + caretRect.width),
                          (float)(caretRect.y + caretRect.height))));
    TEST("The caret still belongs to the window content, not to the menu",
         caret.GetPopupLayer() == nullptr);
    TEST("The caret is in its visible blink phase (so this is a real test)",
         caret.IsPhaseVisible());
    TEST("No caret pixel shows through the open menu",
         !CaretPixelsOnScreen(window, caretRect));

    // ===== MENU CLOSED AGAIN =====
    std::cerr << "\n--- With the menu closed again ---" << std::endl;
    menu->CloseMenu();
    for (int frame = 0; frame < 5; ++frame) Frame(window, area);
    TEST("The caret comes back once the menu is gone",
         caret.IsPhaseVisible() && CaretPixelsOnScreen(window, caret.GetRect()));

    std::cerr << "\n========================================" << std::endl;
    std::cerr << "   " << (testCount - failCount) << "/" << testCount << " passed"
              << std::endl;
    std::cerr << "========================================" << std::endl;
    return failCount == 0 ? 0 : 1;
}
