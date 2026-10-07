// Tests/MenuAndTabBehaviourTest.cpp
// Menu, tabbed container and text area settings do what their names say.
//
//  - MenuItemData::Input() was declared but never defined, so a call compiled
//    and then failed to link. The declarations are gone; MenuItemType::Input
//    stays, reserved.
//  - MenuStyle::enableAnimations computed an opening progress that nothing
//    drew. A popup now fades its entries in over animationDuration.
//  - Escape closes an open menu through the popup system (the application
//    closes the topmost popup when its closeByEscapeKey is set). That was
//    already so; the check here keeps the menu registered that way.
//  - UltraCanvasTabbedContainer stored SetDropdownSearchEnabled() and
//    SetDropdownSearchThreshold() and ignored them: the overflow button always
//    opened the search popup. It now opens a plain menu of the tabs unless
//    search is on and enough tabs are listed. Turning search off on a
//    container outside a window no longer calls through the null window
//    pointer (undefined behaviour that did not crash, so the last tab check
//    only shows the call is harmless).
//  - UltraCanvasTextArea::SetCursorPosition(pos, true) ignored `selecting`;
//    it extends the selection from its anchor now. GetCursorPosition() is
//    const.
//  - Activating a menu item with no application called through a null
//    pointer to post the MenuClick event; it runs the item and posts nothing.
//
// Runs headless: popups open in a window stand-in with no native side, and
// the menu is drawn into an offscreen surface read back pixel by pixel.
// Version: 1.1.0 - activating an item with no application
// Last Modified: 2026-10-07
// Author: UltraCanvas Framework

#include "UltraCanvasMenu.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasWindow.h"

#include <cairo/cairo.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

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

// Asked in a template so that a missing member reads as false, not as an error.
template <typename Item>
constexpr bool HasInputFactory = requires {
    Item::Input(std::string(), std::string(), std::function<void(const std::string&)>());
};

template <typename Area>
constexpr bool CursorReadableThroughConst = requires(const Area& area) { area.GetCursorPosition(); };

// A window with nothing native behind it: popups open into its list exactly as
// they do in a real one, and focus requests are only recorded (a real focus
// change would start the caret, which needs an application).
class HeadlessWindow : public UltraCanvasWindowBase {
public:
    HeadlessWindow() { SetBounds(Rect2Df(0, 0, 800, 600)); }

    UltraCanvasUIElement* focusRequested = nullptr;
    void SetFocusedElement(UltraCanvasUIElement* element) override { focusRequested = element; }

    void Show() override {}
    void Hide() override {}
    void RaiseAndFocus() override {}
    void SetWindowTitle(const std::string&) override {}
    void SetWindowIcon(const std::string&) override {}
    void SetWindowPosition(int, int) override {}
    void SetWindowSize(int, int) override {}
    void Minimize() override {}
    void Maximize() override {}
    void Restore() override {}
    void SetFullscreen(bool) override {}
    void SetResizable(bool) override {}
    void GetScreenSize(int& width, int& height) const override { width = 800; height = 600; }
    NativeWindowHandle GetNativeHandle() const override { return NativeWindowHandle{}; }
    void InvalidateWindowNative() override {}

protected:
    bool CreateNative() override { return true; }
    void DestroyNative() override {}
    void DoResizeNative() override {}
    bool RecreateNativeSurface() override { return true; }
};

// A white offscreen surface; counts the dark (text) pixels drawn into it.
struct Canvas {
    std::unique_ptr<IRenderContext> ctx;
    cairo_t* cr = nullptr;
    int width, height;

    Canvas(int w, int h) : width(w), height(h) {
        ctx = CreateRenderContext(Size2Di(w, h), nullptr);
        if (ctx) cr = static_cast<cairo_t*>(ctx->GetNativeContext());
    }

    void Clear() {
        cairo_save(cr);
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_paint(cr);
        cairo_restore(cr);
    }

    int DarkPixels() const {
        cairo_surface_t* s = cairo_get_target(cr);
        cairo_surface_flush(s);
        const unsigned char* data = cairo_image_surface_get_data(s);
        const int stride = cairo_image_surface_get_stride(s);
        int dark = 0;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const auto px = *reinterpret_cast<const uint32_t*>(data + y * stride + x * 4);
                if (((px >> 16) & 0xFF) < 100 && ((px >> 8) & 0xFF) < 100 && (px & 0xFF) < 100) ++dark;
            }
        }
        return dark;
    }
};

// A popup menu with one wide label, open in `win` and laid out at the origin.
std::shared_ptr<UltraCanvasMenu> OpenLabelMenu(HeadlessWindow& win, bool animate, float seconds) {
    auto menu = std::make_shared<UltraCanvasMenu>("fade", 220, 40);
    menu->SetMenuType(MenuType::PopupMenu);
    MenuStyle style = MenuStyle::Default();
    style.font.fontSize = 20.0f;
    style.itemHeight = 36;
    style.enableAnimations = animate;
    style.animationDuration = seconds;
    menu->SetStyle(style);
    menu->AddItem(MenuItemData::Action("MMMM WWWW", [] {}));
    menu->OpenMenu(Point2Di(0, 0), win, PopupElementSettings());
    CSSLayout::LayoutContext lctx;
    menu->Arrange(Rect2Df(0, 0, 220, 44), lctx);
    return menu;
}

int DrawAndCount(UltraCanvasMenu& menu, Canvas& canvas) {
    canvas.Clear();
    menu.Render(canvas.ctx.get(), Rect2Df(0, 0, 220, 44));
    return canvas.DarkPixels();
}

void MenuChecks(HeadlessWindow& win) {
    // ---- 1. No factory for the unimplemented Input item ----
    TEST("MenuItemData has no Input() factory to link against", !HasInputFactory<MenuItemData>);

    // ---- 2. enableAnimations fades the entries in ----
    Canvas canvas(220, 44);
    if (!canvas.cr) {
        TEST("an offscreen surface to draw the menu into", false);
        return;
    }

    auto still = OpenLabelMenu(win, false, 0.6f);
    const int full = DrawAndCount(*still, canvas);
    TEST("without animation the label is drawn at once", full > 40);
    still->CloseMenu();

    auto fading = OpenLabelMenu(win, true, 0.6f);
    const int atOpen = DrawAndCount(*fading, canvas);
    TEST("with animation the label starts out transparent", atOpen < full / 10);
    std::this_thread::sleep_for(std::chrono::milliseconds(700));
    const int afterFade = DrawAndCount(*fading, canvas);
    TEST("and is drawn in full once animationDuration has passed", afterFade > full * 9 / 10);
    std::cerr << "  (dark label pixels: still " << full << ", fade at open " << atOpen
              << ", after the fade " << afterFade << ")" << std::endl;
    fading->CloseMenu();

    // ---- 3. Escape: the menu's popup is one the application closes on Escape ----
    bool closed = false;
    auto menu = std::make_shared<UltraCanvasMenu>("escape", 160, 0);
    menu->SetMenuType(MenuType::PopupMenu);
    menu->AddItem(MenuItemData::Action("Item", [] {}));
    menu->onMenuClosed = [&closed] { closed = true; };
    menu->OpenMenu(Point2Di(10, 10), win, PopupElementSettings());
    PopupElement* top = win.GetActivePopupElement();
    TEST("an opened menu is the window's topmost popup", top && top->element == menu.get());
    TEST("registered to close on Escape", top && top->settings.closeByEscapeKey);
    // What UltraCanvasApplication does with an Escape key while it is open.
    win.ClosePopup(*menu, ClosePopupReason::EscapeKey);
    TEST("closing it for Escape hides the menu", !menu->IsVisible() && closed);

    // ---- 4. Activating an item with no application runs it ----
    // The menu reports the click to the application's event queue; with no
    // application (this test has none) it used to call through a null pointer.
    bool ran = false;
    auto picker = std::make_shared<UltraCanvasMenu>("noapp", 160, 0);
    picker->SetMenuType(MenuType::PopupMenu);
    picker->AddItem(MenuItemData::Action("Run", [&ran] { ran = true; }));
    picker->OpenMenu(Point2Di(10, 10), win, PopupElementSettings());
    UCEvent key;
    key.type = UCEventType::KeyDown;
    key.virtualKey = UCKeys::Down;
    picker->OnEvent(key);
    key.virtualKey = UCKeys::Return;
    picker->OnEvent(key);
    TEST("Return on an item runs it without an application", ran);
    if (picker->IsVisible()) picker->CloseMenu();
}

void TabChecks(HeadlessWindow& win) {
    auto tabs = std::make_shared<UltraCanvasTabbedContainer>("tabs", 0, 0, 400, 300);
    win.AddChild(tabs);
    tabs->SetOverflowDropdownPosition(OverflowDropdownPosition::Left);
    for (int i = 0; i < 3; ++i) tabs->AddTab("Tab " + std::to_string(i));
    tabs->SetDropdownSearchThreshold(5);

    // ---- 4. Below the threshold: the plain list, not the search popup ----
    tabs->overflowButton->onClick();   // what a click on the overflow button runs
    PopupElement* top = win.GetActivePopupElement();
    auto* list = top ? dynamic_cast<UltraCanvasMenu*>(top->element) : nullptr;
    TEST("3 tabs, threshold 5: the overflow button opens a menu", list != nullptr);
    TEST("and not the search popup", !tabs->dropdownSearchActive &&
                                     (!top || top->element != tabs->searchAutoComplete.get()));
    if (list) {
        auto& items = list->GetItems();
        TEST("the menu lists every tab", items.size() == 3);
        TEST("with the active one checked", items.size() == 3 && items[0].checked && !items[2].checked);
        if (items.size() == 3 && items[2].onClick) items[2].onClick();
        TEST("choosing an entry activates its tab", tabs->GetActiveTab() == 2);
        list->CloseMenu();
    }

    // ---- At the threshold: the search popup ----
    tabs->AddTab("Tab 3");
    tabs->AddTab("Tab 4");
    tabs->overflowButton->onClick();
    top = win.GetActivePopupElement();
    TEST("5 tabs, threshold 5: the search popup opens", tabs->dropdownSearchActive &&
                                                         top && top->element == tabs->searchAutoComplete.get());
    tabs->HideSearchAutoComplete();

    // ---- Search off: the plain list however many tabs there are ----
    tabs->SetDropdownSearchEnabled(false);
    tabs->overflowButton->onClick();
    top = win.GetActivePopupElement();
    TEST("search disabled: the overflow button opens the menu",
         !tabs->dropdownSearchActive && top && dynamic_cast<UltraCanvasMenu*>(top->element) != nullptr);
    if (top) win.ClosePopup(*top->element);

    // ---- Turning search off before the container is in a window ----
    auto loose = CreateTabbedContainerWithDropdown("loose", 0, 0, 300, 200,
                                                   OverflowDropdownPosition::Left, false, 5);
    TEST("a container outside any window takes search off", loose && !loose->IsDropdownSearchEnabled());
}

void TextAreaChecks() {
    // ---- 5. SetCursorPosition(pos, true) extends the selection ----
    auto area = std::make_shared<UltraCanvasTextArea>("area", 0, 0, 300, 200);
    area->SetText("hello world\nsecond line");
    area->SetCursorPosition({0, 2});
    TEST("placing the caret selects nothing", !area->HasSelection());

    area->SetCursorPosition({0, 7}, true);
    TEST("selecting from the caret selects up to the new place",
         area->HasSelection() && area->GetSelectionStart().lineIndex == 0 &&
         area->GetSelectionStart().columnIndex == 2 && area->GetSelectionEnd().columnIndex == 7);
    TEST("the selected text", area->GetSelectedText() == "llo w");

    area->SetCursorPosition({1, 3}, true);
    TEST("selecting again keeps the anchor and moves the end",
         area->GetSelectionStart().lineIndex == 0 && area->GetSelectionStart().columnIndex == 2 &&
         area->GetSelectionEnd().lineIndex == 1 && area->GetSelectionEnd().columnIndex == 3);

    TEST("the caret follows", area->GetCursorPosition().lineIndex == 1 &&
                              area->GetCursorPosition().columnIndex == 3);
    TEST("GetCursorPosition() can be called on a const text area",
         CursorReadableThroughConst<UltraCanvasTextArea>);
}

} // namespace

int main() {
    auto win = std::make_shared<HeadlessWindow>();
    MenuChecks(*win);
    TabChecks(*win);
    TextAreaChecks();

    std::cerr << "\nMenuAndTabBehaviourTest: " << testCount << " checks, " << failCount << " failures" << std::endl;
    return failCount;
}
