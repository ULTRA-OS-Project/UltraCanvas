// Tests/MenuAndTabBehaviourTest.cpp
// Menu, tabbed container and text area settings do what their names say.
//
//  - MenuItemData::Input() was declared but never defined, so a call compiled
//    and then failed to link. The declarations are gone; MenuItemType::Input
//    stays, reserved.
//  - MenuStyle::enableAnimations computed an opening progress that nothing
//    drew. A popup now fades in over animationDuration - the whole panel,
//    background, border, shadow and entries, not only the entries - by
//    stepping its popup opacity on the window.
//  - UltraCanvasWindowBase::SetPopupOpacity: below 1 the window mixes a popup
//    with the content beneath it; at 1, the default, the popup is copied onto
//    the window bit for bit as before.
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
// the menu is drawn into an offscreen surface read back pixel by pixel. The
// fade is read off a second stand-in whose "screen" is an offscreen surface,
// so UpdateAndRender() composites onto it exactly as onto a real window.
// Version: 1.2.0 - the whole popup fades, composited by the window at its opacity
// Version: 1.1.0 - activating an item with no application
// Last Modified: 2026-10-07
// Author: UltraCanvas Framework

#include "UltraCanvasMenu.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasWindow.h"

#include <cairo/cairo.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
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

// A window stand-in with a screen: an offscreen surface takes the native
// surface's place, so UpdateAndRender() lays out, paints and composites the
// popups onto it exactly as a real window does. The content is one flat
// colour, so whatever a popup lets through of it can be told apart.
class ScreenWindow : public HeadlessWindow {
public:
    static constexpr int kWidth = 400;
    static constexpr int kHeight = 300;
    const Color content = Color(30, 160, 60, 255);
    cairo_surface_t* screen = nullptr;

    ScreenWindow() {
        config_.width = kWidth;
        config_.height = kHeight;
        SetBounds(Rect2Df(0, 0, kWidth, kHeight));
        screen = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, kWidth, kHeight);
        nativeSurface = screen;
        renderContext = CreateRenderContext(Size2Di(kWidth, kHeight), nativeSurface);
        _created = renderContext != nullptr;
        _windowVisible = true;
        AddDirtyRectangle(Rect2Di(0, 0, kWidth, kHeight));
    }
    ~ScreenWindow() override {
        CloseAllPopups();
        renderContext.reset();
        cairo_surface_destroy(screen);
    }

    bool Ready() const { return _created; }

    void RenderCustomContent(IRenderContext* ctx, const Rect2Di&) override {
        ctx->SetFillPaint(content);
        ctx->FillRectangle(Rect2Df(0, 0, kWidth, kHeight));
    }

    // One frame, as the application's loop would run it.
    void Frame() { UpdateAndRender(); }

    // The screen's pixels (premultiplied ARGB) inside `r`, row by row.
    std::vector<uint32_t> Pixels(const Rect2Di& r) const {
        cairo_surface_flush(screen);
        const unsigned char* data = cairo_image_surface_get_data(screen);
        const int stride = cairo_image_surface_get_stride(screen);
        std::vector<uint32_t> out;
        for (int y = r.y; y < r.y + r.height; ++y)
            for (int x = r.x; x < r.x + r.width; ++x)
                out.push_back(*reinterpret_cast<const uint32_t*>(data + y * stride + x * 4));
        return out;
    }

    // The largest difference of any channel of any pixel in `r` from the content colour.
    int MaxDifferenceFromContent(const Rect2Di& r) const {
        int worst = 0;
        for (uint32_t px : Pixels(r)) {
            worst = std::max({worst,
                              std::abs(static_cast<int>((px >> 16) & 0xFF) - content.r),
                              std::abs(static_cast<int>((px >> 8) & 0xFF) - content.g),
                              std::abs(static_cast<int>(px & 0xFF) - content.b),
                              std::abs(static_cast<int>(px >> 24) - content.a)});
        }
        return worst;
    }
};

// A popup that is not a menu: opaque blue on the left, half-transparent red in
// the middle, nothing on the right - so a copy and a blend give different pixels.
class Swatch : public UltraCanvasUIElement {
public:
    Swatch() : UltraCanvasUIElement("swatch", 0, 0, 90, 30) {}
    void Render(IRenderContext* ctx, const Rect2Df&) override {
        ctx->SetFillPaint(Color(0, 0, 255, 255));
        ctx->FillRectangle(Rect2Df(0, 0, 30, 30));
        ctx->SetFillPaint(Color(255, 0, 0, 128));
        ctx->FillRectangle(Rect2Df(30, 0, 30, 30));
    }
};

Rect2Di WindowBounds(const UltraCanvasUIElement& e) {
    Rect2Df b = e.GetBoundsInWindow();
    return Rect2Di(static_cast<int>(b.x), static_cast<int>(b.y),
                   static_cast<int>(b.width), static_cast<int>(b.height));
}

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

    // The window does the fading now, so the menu itself draws in full: its
    // entries are no longer faded a second time inside a faded popup.
    auto fading = OpenLabelMenu(win, true, 60.0f);
    TEST("a fading menu opens with its popup at opacity 0", win.GetPopupOpacity(*fading) == 0.0f);
    const int atOpen = DrawAndCount(*fading, canvas);
    TEST("and draws its entries in full: the window fades the whole popup", atOpen == full);
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

void PopupOpacityChecks() {
    ScreenWindow win;
    if (!win.Ready()) {
        TEST("a window stand-in with a screen to composite onto", false);
        return;
    }
    win.Frame();
    const Rect2Di whole(0, 0, ScreenWindow::kWidth, ScreenWindow::kHeight);
    TEST("the stand-in's screen shows its content", win.MaxDifferenceFromContent(whole) == 0);

    // ---- A popup opened with default settings is copied onto the window as before ----
    auto swatch = std::make_shared<Swatch>();
    win.OpenPopup(Point2Di(40, 50), *swatch, PopupElementSettings());
    win.Frame();
    const Rect2Di at = WindowBounds(*swatch);
    Canvas own(90, 30);   // the popup drawn on its own, onto nothing
    if (own.cr) swatch->Render(own.ctx.get(), Rect2Df(0, 0, 90, 30));
    std::vector<uint32_t> ownPixels;
    if (own.cr) {
        cairo_surface_flush(cairo_get_target(own.cr));
        const unsigned char* data = cairo_image_surface_get_data(cairo_get_target(own.cr));
        const int stride = cairo_image_surface_get_stride(cairo_get_target(own.cr));
        for (int y = 0; y < 30; ++y)
            for (int x = 0; x < 90; ++x)
                ownPixels.push_back(*reinterpret_cast<const uint32_t*>(data + y * stride + x * 4));
    }
    TEST("a popup opens at opacity 1", win.GetPopupOpacity(*swatch) == 1.0f);
    TEST("at the default opacity its pixels are copied as they are, transparent ones too",
         at.x == 40 && at.y == 50 && at.width == 90 && at.height == 30 &&
         !ownPixels.empty() && win.Pixels(at) == ownPixels);

    // ---- Below 1 it is mixed with the content beneath ----
    TEST("SetPopupOpacity takes an open popup", win.SetPopupOpacity(*swatch, 0.0f));
    win.Frame();
    TEST("at opacity 0 the content beneath shows unchanged", win.MaxDifferenceFromContent(at) == 0);

    win.SetPopupOpacity(*swatch, 0.5f);
    win.Frame();
    const uint32_t mid = win.Pixels(Rect2Di(at.x + 10, at.y + 10, 1, 1))[0];
    auto near = [](int v, int want) { return std::abs(v - want) <= 2; };
    TEST("at opacity 0.5 its opaque blue is half way to the content",
         near((mid >> 16) & 0xFF, (30 + 0) / 2) && near((mid >> 8) & 0xFF, (160 + 0) / 2) &&
         near(mid & 0xFF, (60 + 255) / 2));
    std::cerr << "  (opacity 0.5 over the content: " << std::hex << mid << std::dec << ")" << std::endl;

    win.SetPopupOpacity(*swatch, 1.0f);
    win.Frame();
    TEST("back at 1 it is the plain copy again", win.Pixels(at) == ownPixels);

    Swatch loose;
    TEST("an element that is not an open popup has no opacity to set", !win.SetPopupOpacity(loose, 0.5f) &&
                                                                       win.GetPopupOpacity(loose) == 1.0f);
    win.ClosePopup(*swatch);
    win.Frame();
    TEST("closing it uncovers the content", win.MaxDifferenceFromContent(at) == 0);

    // ---- A menu with enableAnimations fades in as a whole panel ----
    auto open = [&win](bool animate, float seconds) {
        auto menu = std::make_shared<UltraCanvasMenu>("panel", 220, 40);
        menu->SetMenuType(MenuType::PopupMenu);
        MenuStyle style = MenuStyle::Default();
        style.font.fontSize = 20.0f;
        style.itemHeight = 36;
        style.enableAnimations = animate;
        style.animationDuration = seconds;
        menu->SetStyle(style);
        menu->AddItem(MenuItemData::Action("MMMM WWWW", [] {}));
        menu->OpenMenu(Point2Di(20, 20), win, PopupElementSettings());
        win.Frame();
        return menu;
    };

    auto still = open(false, 1.0f);
    const Rect2Di panel = WindowBounds(*still);
    const std::vector<uint32_t> stillPixels = win.Pixels(panel);
    TEST("a menu without animation is on screen at once",
         panel.width > 100 && panel.height > 30 && win.MaxDifferenceFromContent(panel) > 100);
    still->CloseMenu();
    win.Frame();

    auto fading = open(true, 1.0f);
    const float opacityAtOpen = win.GetPopupOpacity(*fading);
    const int differenceAtOpen = win.MaxDifferenceFromContent(panel);
    TEST("an animated menu opens nearly transparent", opacityAtOpen < 0.25f);
    TEST("its whole panel - background, border, shadow, entries - is close to the content beneath",
         WindowBounds(*fading).x == panel.x && WindowBounds(*fading).y == panel.y &&
         differenceAtOpen <= static_cast<int>(std::ceil(255.0f * opacityAtOpen)) + 2);
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    // No application, so no timer: a repaint moves the fade on to where the clock has got.
    fading->RequestRedraw();
    win.Frame();
    TEST("after animationDuration the popup is opaque", win.GetPopupOpacity(*fading) == 1.0f);
    TEST("and the panel's pixels are the menu's own, exactly as without animation",
         win.Pixels(panel) == stillPixels);
    std::cerr << "  (fade: opacity at open " << opacityAtOpen << ", largest difference from the content "
              << differenceAtOpen << ")" << std::endl;
    fading->CloseMenu();
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

// Dragging a tab to another place: the swap follows the bar's axis, every
// index that names a tab follows the tab, and a pointer held at either end of
// the strip carries the tab on past the visible range. The steps a drag is
// made of are driven directly: there is no application here to deliver the
// mouse events or run the auto-scroll timer.
void TabReorderChecks(HeadlessWindow& win) {
    CSSLayout::LayoutContext lctx;

    // ---- A vertical bar swaps on y, not x ----
    auto side = std::make_shared<UltraCanvasTabbedContainer>("side", 0, 0, 300, 400);
    win.AddChild(side);
    side->SetTabPosition(TabPosition::Left);
    side->SetTabHeight(30);
    side->SetAllowTabReordering(true);
    for (int i = 0; i < 4; ++i) side->AddTab("Tab " + std::to_string(i));
    side->Arrange(Rect2Df(0, 0, 300, 400), lctx);
    side->SetActiveTab(0);
    side->draggingTabIndex = 0;
    side->isDraggingTab = true;
    Rect2Di below = side->GetTabBounds(1);
    TEST("left bar: the tabs are stacked", below.y >= 30 && below.width > 0);
    side->UpdateDragReorder(below.x + 5, below.y + 3);
    TEST("over the upper half of the next tab nothing moves yet",
         side->GetTabTitle(0) == "Tab 0" && side->dragInsertionIndex == 1);
    side->UpdateDragReorder(below.x + 5, below.y + below.height - 3);
    TEST("past its centre the dragged tab takes its place",
         side->GetTabTitle(1) == "Tab 0" && side->GetTabTitle(0) == "Tab 1" && side->draggingTabIndex == 1);
    TEST("and stays the active tab", side->GetActiveTab() == 1);
    side->isDraggingTab = false;
    side->draggingTabIndex = -1;

    // ---- Indices follow the tabs they name ----
    auto tabs = std::make_shared<UltraCanvasTabbedContainer>("reorder", 0, 0, 600, 200);
    win.AddChild(tabs);
    for (int i = 0; i < 5; ++i) tabs->AddTab("Tab " + std::to_string(i));
    tabs->Arrange(Rect2Df(0, 0, 600, 200), lctx);
    tabs->SetActiveTab(1);
    tabs->hoveredTabIndex = 3;
    tabs->hoveredCloseButtonIndex = 3;
    tabs->ReorderTabs(0, 4);
    TEST("moving the first tab to the end shifts the hovered tab down with it",
         tabs->hoveredTabIndex == 2 && tabs->hoveredCloseButtonIndex == 2 && tabs->GetTabTitle(2) == "Tab 3");
    TEST("and the active tab", tabs->GetActiveTab() == 0 && tabs->GetTabTitle(0) == "Tab 1");
    tabs->hoveredTabIndex = 1;
    tabs->ReorderTabs(1, 3);
    TEST("moving the hovered tab itself moves the hover with it",
         tabs->hoveredTabIndex == 3 && tabs->GetTabTitle(3) == "Tab 2");
    tabs->hoveredTabIndex = -1;
    tabs->hoveredCloseButtonIndex = -1;

    // ---- A pointer at the strip's end carries the tab past the visible range ----
    auto strip = std::make_shared<UltraCanvasTabbedContainer>("strip", 0, 0, 260, 200);
    win.AddChild(strip);
    strip->SetTabMinWidth(100);
    strip->SetTabMaxWidth(100);
    strip->SetAllowTabReordering(true);
    for (int i = 0; i < 6; ++i) strip->AddTab("Tab " + std::to_string(i));
    strip->Arrange(Rect2Df(0, 0, 260, 200), lctx);
    strip->SetActiveTab(0);
    TEST("the strip shows fewer tabs than it holds", strip->maxVisibleTabs > 0 && strip->maxVisibleTabs < 6);
    strip->draggingTabIndex = 0;
    strip->isDraggingTab = true;
    Rect2Di area = strip->GetTabAreaBounds();
    strip->UpdateDragAutoScroll(area.x + area.width / 2, area.y + 5);
    TEST("a pointer in the middle of the strip arms nothing", strip->dragAutoScrollDirection == 0);
    strip->UpdateDragAutoScroll(area.x + area.width - 2, area.y + 5);
    TEST("a pointer at the right end arms the auto-scroll forwards", strip->dragAutoScrollDirection == 1);
    for (int tick = 0; tick < 10; ++tick) strip->DragAutoScrollTick();
    TEST("the ticks carry the tab to the end",
         strip->GetTabTitle(5) == "Tab 0" && strip->draggingTabIndex == 5 && strip->GetActiveTab() == 5);
    TEST("and scroll the strip to keep it in view",
         strip->tabScrollOffset <= 5 && 5 < strip->tabScrollOffset + strip->maxVisibleTabs);
    TEST("at the end the auto-scroll stands down", strip->dragAutoScrollDirection == 0);
    strip->UpdateDragAutoScroll(area.x + area.width - 2, area.y + 5);
    TEST("and is not re-armed with nowhere to go", strip->dragAutoScrollDirection == 0);
    strip->UpdateDragAutoScroll(area.x + 2, area.y + 5);
    TEST("a pointer at the left end arms it backwards", strip->dragAutoScrollDirection == -1);
    strip->DragAutoScrollTick();
    TEST("one tick carries the tab back one place and shows it",
         strip->GetTabTitle(4) == "Tab 0" && strip->draggingTabIndex == 4 &&
         strip->tabScrollOffset <= 4 && 4 < strip->tabScrollOffset + strip->maxVisibleTabs);
    strip->isDraggingTab = false;
    strip->StopDragAutoScroll();
    strip->draggingTabIndex = -1;
    TEST("stopping clears the direction", strip->dragAutoScrollDirection == 0);

    // ---- A whole drag through the events, with no application behind the window ----
    auto drag = std::make_shared<UltraCanvasTabbedContainer>("drag", 0, 0, 600, 200);
    win.AddChild(drag);
    drag->SetAllowTabReordering(true);
    for (int i = 0; i < 3; ++i) drag->AddTab("Tab " + std::to_string(i));
    drag->Arrange(Rect2Df(0, 0, 600, 200), lctx);
    int reorders = 0;
    drag->onTabReorder = [&reorders](int, int) { ++reorders; };
    auto mouse = [](UCEventType type, int x, int y) {
        UCEvent e;
        e.type = type;
        e.button = UCMouseButton::Left;
        e.pointer = Point2Di(x, y);
        e.pointerWindow = Point2Di(x, y);
        e.pointerGlobal = Point2Di(x, y);
        return e;
    };
    Rect2Di first = drag->GetTabBounds(0);
    Rect2Di last = drag->GetTabBounds(2);
    const int row = first.y + first.height / 2;
    drag->OnEvent(mouse(UCEventType::MouseDown, first.x + first.width / 2, row));
    TEST("pressing a tab activates it and arms the drag", drag->GetActiveTab() == 0 && drag->draggingTabIndex == 0 && !drag->isDraggingTab);
    drag->OnEvent(mouse(UCEventType::MouseMove, first.x + first.width / 2 + 3, row));
    TEST("a move within the threshold is not a drag", !drag->isDraggingTab);
    drag->OnEvent(mouse(UCEventType::MouseMove, first.x + first.width / 2 + 12, row));
    TEST("a move past the threshold is", drag->isDraggingTab);
    drag->OnEvent(mouse(UCEventType::MouseMove, last.x + last.width - 3, row));
    TEST("carried past the last tab's centre, the tab lands at the end",
         drag->GetTabTitle(2) == "Tab 0" && drag->draggingTabIndex == 2 && drag->GetActiveTab() == 2 && reorders == 1);
    drag->OnEvent(mouse(UCEventType::MouseUp, last.x + last.width - 3, row));
    TEST("releasing ends the drag", !drag->isDraggingTab && drag->draggingTabIndex == -1 && drag->dragAutoScrollDirection == 0);
    TEST("the order stays", drag->GetTabTitle(0) == "Tab 1" && drag->GetTabTitle(1) == "Tab 2" && drag->GetTabTitle(2) == "Tab 0");
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
    PopupOpacityChecks();
    TabChecks(*win);
    TabReorderChecks(*win);
    TextAreaChecks();

    std::cerr << "\nMenuAndTabBehaviourTest: " << testCount << " checks, " << failCount << " failures" << std::endl;
    return failCount;
}
