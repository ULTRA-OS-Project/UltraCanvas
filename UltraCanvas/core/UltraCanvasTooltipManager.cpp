// UltraCanvasTooltipManager.cpp
// Implementation of tooltip system for UltraCanvas
// Version: 2.5.2 - an element with no window (and a program with no application) asks for
//                  a tooltip without crashing: nothing is shown
// Version: 2.5.1 - in a window of its own on macOS too
// Version: 2.5.0 - shown in a window of its own (WindowType::Tooltip) above every other
//                  window, placed on the screen rather than inside the window it
//                  belongs to; drawn in that window only where the platform has none
// Version: 1.2.0 - SetStyle static
// Version: 2.4.1 - long words wrap between characters when they must, so a
//                  URL or a path stays inside the tooltip's box
// Version: 2.4.0 - the tooltip sits above and to the right of the pointer,
//                  clear of the line under it (below the pointer's arrow only
//                  when there is no room above)
// Version: 2.3.0
// Last Modified: 2026-11-09
// Author: UltraCanvas Framework

#include "UltraCanvasTooltipManager.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasApplication.h"
#include <array>
#include <string>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <fmt/os.h>
#include "UltraCanvasDebug.h"

namespace UltraCanvas {
// ===== STATIC MEMBER DEFINITIONS =====
    std::string UltraCanvasTooltipManager::currentText;
    std::unique_ptr<IRenderContext> UltraCanvasTooltipManager::renderCtx;
    Rect2Di UltraCanvasTooltipManager::tooltipRect;
    UltraCanvasWindowBase* UltraCanvasTooltipManager::targetWindow = nullptr;
//    Point2Di UltraCanvasTooltipManager::cursorPosition;
    bool UltraCanvasTooltipManager::visible = false;
    bool UltraCanvasTooltipManager::pendingShow = false;
    bool UltraCanvasTooltipManager::pendingHide = false;

    TimerId UltraCanvasTooltipManager::showTimerId = InvalidTimerId;
    TimerId UltraCanvasTooltipManager::hideTimerId = InvalidTimerId;

    TooltipStyle UltraCanvasTooltipManager::style;

    bool UltraCanvasTooltipManager::enabled = true;

    std::shared_ptr<UltraCanvasWindowBase> UltraCanvasTooltipManager::ownWindow;
    UltraCanvasWindowBase* UltraCanvasTooltipManager::ownWindowTarget = nullptr;
    bool UltraCanvasTooltipManager::useOwnWindow = true;
    bool UltraCanvasTooltipManager::shownInOwnWindow = false;
    Point2Di UltraCanvasTooltipManager::cursorPosition;
    Point2Di UltraCanvasTooltipManager::screenPosition;

    // Where a tooltip can have a window of its own: a backend that places a
    // WindowType::Tooltip window on the screen. The browser and Android have
    // one surface per application, and iOS no windows of this kind: there
    // the tooltip is drawn inside its window, as before.
#if defined(__ANDROID__) || defined(__EMSCRIPTEN__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
    static constexpr bool kOwnWindowPlatform = false;
#else
    static constexpr bool kOwnWindowPlatform = true;
#endif

    // How far below the pointer's tip a tooltip goes when there is no room
    // above it: clear of the arrow, which hangs down from the tip.
    static constexpr int kPointerArrowHeight = 22;
//    Rect2Di UltraCanvasTooltipManager::screenBounds = Rect2Di(0, 0, 1920, 1080);

// ===== INTERNAL LAYOUT STATE =====
    // One laid-out block of the current tooltip content, positioned in
    // content-local coordinates (0,0 = inside the padding).
    static constexpr int kMaxRowColumns = 3;

    struct TooltipBlockLayout {
        TooltipBlockType type = TooltipBlockType::Text;
        std::unique_ptr<ITextLayout> textLayout;   // Title/Text/Bullet text
        // Row cells, left to right; only the first `columns` entries are set
        std::array<std::unique_ptr<ITextLayout>, kMaxRowColumns> cells;
        std::array<int, kMaxRowColumns> cellWidth = {0, 0, 0};   // drawn width per cell
        // Width of the cell text up to its decimal anchor; Decimal columns only
        std::array<int, kMaxRowColumns> cellPrefixWidth = {0, 0, 0};
        int columns = 0;                           // Row column count (2 or 3)
        Color swatch = Color(0, 0, 0, 0);
        int y = 0;
        int height = 0;
    };

    static TooltipContent currentContent;
    static std::vector<TooltipBlockLayout> blockLayouts;
    static std::unique_ptr<ITextLayout> bulletGlyphLayout;
    static int rowLabelIndent = 0;   // label x-offset when any row carries a swatch

    // Column grids: two- and three-column rows are measured as independent
    // tables, so a "Total" row of a different arity never disturbs the other.
    static std::array<int, 2> rowGrid2 = {0, 0};
    static std::array<int, 3> rowGrid3 = {0, 0, 0};
    static std::array<TooltipColumnAlign, 2> rowAlign2 = {
        TooltipColumnAlign::Left, TooltipColumnAlign::Right};
    static std::array<TooltipColumnAlign, 3> rowAlign3 = {
        TooltipColumnAlign::Left, TooltipColumnAlign::Right, TooltipColumnAlign::Right};
    // Decimal columns: the column-wide anchor, i.e. the widest integer part
    static std::array<int, 2> rowPrefix2 = {0, 0};
    static std::array<int, 3> rowPrefix3 = {0, 0, 0};

    // Row swatch and bullet metrics
    static constexpr int kSwatchSize = 9;
    static constexpr int kSwatchGap = 6;
    static constexpr int kBulletIndent = 13;
    static constexpr int kSeparatorMargin = 3;   // extra space above/below separators
    // Share of the row width the first (label) column may claim before the
    // remaining columns get their say, applied only when the row overflows.
    static constexpr int kFirstColumnMaxPercent = 55;

    // Split `avail` pixels over `n` columns whose natural widths are `nat`.
    // Everything fits => natural widths; otherwise the first column is capped
    // and the rest share what is left in proportion to their natural width.
    static void DistributeColumns(const int* nat, int n, int avail, int* out) {
        for (int i = 0; i < n; ++i) out[i] = 0;
        if (avail <= 0) return;

        int total = 0;
        for (int i = 0; i < n; ++i) total += nat[i];
        if (total <= avail) {
            for (int i = 0; i < n; ++i) out[i] = nat[i];
            return;
        }

        out[0] = std::min(nat[0], avail * kFirstColumnMaxPercent / 100);
        const int remaining = avail - out[0];
        const int restNat = total - nat[0];
        int assigned = 0;
        for (int i = 1; i < n; ++i) {
            int share = restNat > 0
                ? static_cast<int>(static_cast<long long>(remaining) * nat[i] / restNat)
                : 0;
            out[i] = std::min(nat[i], share);
            assigned += out[i];
        }
        // Columns narrower than their share leave slack; hand it to the ones
        // still clipped so no space is wasted.
        int slack = remaining - assigned;
        for (int i = 1; i < n && slack > 0; ++i) {
            int grow = std::min(slack, nat[i] - out[i]);
            out[i] += grow;
            slack -= grow;
        }
        // A column holding text never collapses to nothing, however tight the
        // budget — a zero-width text layout has nothing to render into.
        for (int i = 0; i < n; ++i) {
            if (nat[i] > 0 && out[i] == 0) out[i] = 1;
        }
    }

    // Left edge of every column box, stretched so the last column ends flush
    // with the tooltip's content width. Surplus space is spread over the gaps.
    static void ComputeColumnOrigins(const int* colWidth, int n, int contentWidth,
                                     int indent, int gap, int* outX) {
        int sum = 0;
        for (int i = 0; i < n; ++i) sum += colWidth[i];
        int extra = contentWidth - indent - sum - (n - 1) * gap;
        if (extra < 0) extra = 0;

        int x = indent;
        for (int i = 0; i < n; ++i) {
            outX[i] = x;
            if (i < n - 1) {
                x += colWidth[i] + gap + extra / (n - 1) + (i < extra % (n - 1) ? 1 : 0);
            }
        }
    }

    // A Decimal cell that had to be clipped can no longer keep its anchor, so
    // it falls back to Right — decimal alignment is a refinement of it.
    static TextAlignment ToTextAlignment(TooltipColumnAlign align) {
        switch (align) {
            case TooltipColumnAlign::Center:  return TextAlignment::Center;
            case TooltipColumnAlign::Right:
            case TooltipColumnAlign::Decimal: return TextAlignment::Right;
            case TooltipColumnAlign::Left:
            default:                          return TextAlignment::Left;
        }
    }

    // Byte offset of a number's decimal separator: the last '.' or ',' that is
    // followed by a digit, so a trailing unit ("1.5 sec.") cannot steal the
    // anchor and both "1,204.50" and "1.204,50" resolve to their real comma.
    // Returns the string length when there is no separator, which anchors an
    // integer at its end — right where the separator column sits.
    static size_t DecimalAnchor(const std::string& s) {
        size_t anchor = s.size();
        for (size_t i = 0; i + 1 < s.size(); ++i) {
            if ((s[i] == '.' || s[i] == ',') && s[i + 1] >= '0' && s[i + 1] <= '9') {
                anchor = i;
            }
        }
        return anchor;
    }

    // x of a cell's text inside its column box. For Decimal, the cell shifts
    // right by however much narrower its integer part is than the column's
    // widest one, which puts every separator on the same pixel column.
    static int AlignedCellX(int boxX, int boxWidth, int cellWidth,
                            int cellPrefixWidth, int columnPrefixWidth,
                            TooltipColumnAlign align) {
        switch (align) {
            case TooltipColumnAlign::Center: return boxX + (boxWidth - cellWidth) / 2;
            case TooltipColumnAlign::Right:  return boxX + boxWidth - cellWidth;
            case TooltipColumnAlign::Decimal: {
                const int room = std::max(0, boxWidth - cellWidth);
                const int shift = std::max(0, columnPrefixWidth - cellPrefixWidth);
                return boxX + std::min(shift, room);
            }
            case TooltipColumnAlign::Left:
            default:                         return boxX;
        }
    }

    // Wrap a layout to `width` pixels: at word boundaries first, then between
    // any two characters. A URL or a path has long runs with no break
    // opportunity, and with Pango's default word-only wrap those ran on past
    // `width` - out of the box that had been sized for it. Returns the width
    // the wrapped text actually takes, which is what the box has to hold.
    static int WrapToWidth(ITextLayout& layout, int width) {
        layout.SetWrap(TextWrap::WrapWordChar);
        layout.SetExplicitWidth(width);
        return static_cast<int>(std::ceil(layout.GetLayoutSize().width));
    }

    static std::string EscapeMarkup(const std::string& s) {
        std::string out;
        out.reserve(s.size());
        for (char c : s) {
            switch (c) {
                case '&': out += "&amp;"; break;
                case '<': out += "&lt;"; break;
                case '>': out += "&gt;"; break;
                default: out += c;
            }
        }
        return out;
    }

    static std::string SpanMarkup(const TooltipStyle& style, float fontSize, bool bold,
                                  const std::string& inner) {
        return fmt::format("<span size=\"{}pt\" face=\"{}\"{}>{}</span>",
                           fontSize, style.fontFamily, bold ? " weight=\"bold\"" : "", inner);
    }

    // Plain-text projection of the content, kept for GetCurrentText()
    static std::string PlainTextOf(const TooltipContent& content) {
        std::string out;
        for (const auto& block : content.blocks) {
            if (block.type == TooltipBlockType::Separator) continue;
            if (!out.empty()) out += "\n";
            out += block.text;
            if (block.type == TooltipBlockType::Row) {
                out += ": " + block.value;
                if (block.columns >= 3) {
                    out += "\t" + block.value2;
                }
            }
        }
        return out;
    }

    // The style the tooltip is drawn with. In its own window it has no soft
    // shadow and square corners: the window is opaque, so a shadow or the
    // corners outside a rounded body would be painted on it in black, not
    // blended over what is beneath. (A compositing window manager gives a
    // tooltip window a shadow of its own.)
    static TooltipStyle DrawnStyle(const TooltipStyle& style, bool inOwnWindow) {
        TooltipStyle drawn = style;
        if (inOwnWindow) {
            drawn.hasShadow = false;
            drawn.cornerRadius = 0.0f;
        }
        return drawn;
    }

    // The opaque colour the own window is filled with, under the tooltip's
    // own (slightly translucent) background
    static Color OwnWindowBackground(const TooltipStyle& style) {
        Color c = style.backgroundColor;
        c.a = 255;
        return c;
    }

    // Margins the soft shadow needs around the tooltip body on each side
    static void GetShadowMargins(const TooltipStyle& style, int& left, int& top, int& right, int& bottom) {
        left = top = right = bottom = 0;
        if (!style.hasShadow || style.shadowColor.a == 0) return;
        int blur = std::max(0, style.shadowBlur);
        left = std::max(0, blur - style.shadowOffset.x);
        top = std::max(0, blur - style.shadowOffset.y);
        right = std::max(0, blur + style.shadowOffset.x);
        bottom = std::max(0, blur + style.shadowOffset.y);
    }

    void UltraCanvasTooltipManager::CancelShowTimer() {
        if (showTimerId != InvalidTimerId) {
            if (auto* app = UltraCanvasApplication::GetInstance()) app->StopTimer(showTimerId);
            showTimerId = InvalidTimerId;
        }
    }

    void UltraCanvasTooltipManager::CancelHideTimer() {
        if (hideTimerId != InvalidTimerId) {
            if (auto* app = UltraCanvasApplication::GetInstance()) app->StopTimer(hideTimerId);
            hideTimerId = InvalidTimerId;
        }
    }

    void UltraCanvasTooltipManager::UpdateAndShowTooltip(UltraCanvasWindowBase* win, const TooltipContent& content,
                                                         const Point2Di& position, const TooltipStyle& newStyle) {
        if (!enabled) return;
        // An element not in a window yet (built but not added, drawn
        // offscreen, in a test) has nowhere to show a tooltip: one shown
        // for another window goes, and nothing else happens.
        if (!win) {
            if (visible || pendingShow) HideTooltipImmediately();
            return;
        }

        // If already showing for this element, just update content
        if (visible) {
            if (targetWindow != win) {
                HideTooltipImmediately();
            } else {
                if (content.Empty()) {
                    HideTooltipImmediately();
                    return;
                }
            }
        }
        if (content.Empty()) return;

        if (targetWindow != win || currentContent != content || style != newStyle) {
            renderCtx.reset();
        }
        style = newStyle;
        currentContent = content;
        currentText = PlainTextOf(content);
        targetWindow = win;
        CalculateTooltipLayout();
        UpdateTooltipPosition(position);

        // A pending hide for the same target is stale now
        CancelHideTimer();
        pendingHide = false;

        if (!visible) {
            CancelShowTimer();
            auto* app = UltraCanvasApplication::GetInstance();
            if (!app) return;   // no application: no timer to wait on, nothing to show it in
            pendingShow = true;
            showTimerId = app->StartTimer(style.showDelay, false,
                [](TimerId) {
                    showTimerId = InvalidTimerId;
                    if (!pendingShow) return;
                    pendingShow = false;
                    visible = true;
                    if (!ShowInOwnWindow() && targetWindow) {
                        targetWindow->RequestWindowComposition();
                    }
                    //debugOutput << "Tooltip shown" << std::endl;
                });
        } else {
            RefreshShown();
        }

        //debugOutput << "Tooltip requested. Text: " << currentText << std::endl;
    }

    void UltraCanvasTooltipManager::UpdateAndShowTooltip(UltraCanvasWindowBase* win, const std::string &text,
                                                const Point2Di &position, const TooltipStyle& newStyle) {
        TooltipContent content;
        if (!text.empty()) {
            content.AddPlainText(text);   // plain data: < > & are shown literally
        }
        UpdateAndShowTooltip(win, content, position, newStyle);
    }

    void UltraCanvasTooltipManager::HideTooltip() {
        if (!visible && !pendingShow) return;

        // Cancel any in-flight show
        CancelShowTimer();
        pendingShow = false;

        auto* app = UltraCanvasApplication::GetInstance();
        if (visible && !app) {
            HideTooltipImmediately();   // no timer to wait on
            return;
        }
        if (visible) {
            CancelHideTimer();
            pendingHide = true;
            hideTimerId = app->StartTimer(style.hideDelay, false,
                [](TimerId) {
                    hideTimerId = InvalidTimerId;
                    if (!pendingHide) return;
                    pendingHide = false;
                    visible = false;
                    if (shownInOwnWindow && ownWindow) {
                        ownWindow->Hide();
                    }
                    shownInOwnWindow = false;
                    if (targetWindow) {
                        targetWindow->RequestWindowComposition();
                    }
                    renderCtx.reset();
                    //debugOutput << "Tooltip hidden" << std::endl;
                });
        }

        //debugOutput << "Tooltip hide requested" << std::endl;
    }

    void UltraCanvasTooltipManager::UpdateAndShowTooltipImmediately(UltraCanvasWindowBase* win, const TooltipContent& content,
                                                                    const Point2Di& position, const TooltipStyle& newStyle) {
        UpdateAndShowTooltip(win, content, position, newStyle);
        if (!enabled || content.Empty() || !win) return;
        CancelShowTimer();
        CancelHideTimer();
        pendingShow = false;
        pendingHide = false;
        const bool wasVisible = visible;
        visible = true;
        if (!wasVisible && !ShowInOwnWindow() && targetWindow) {
            targetWindow->RequestWindowComposition();
        }
    }

    void UltraCanvasTooltipManager::UpdateAndShowTooltipImmediately(UltraCanvasWindowBase* win, const std::string &text,
                                                                    const Point2Di &position, const TooltipStyle& newStyle) {
        TooltipContent content;
        if (!text.empty()) {
            content.AddPlainText(text);   // plain data: < > & are shown literally
        }
        UpdateAndShowTooltipImmediately(win, content, position, newStyle);
    }

    void UltraCanvasTooltipManager::HideTooltipImmediately() {
        CancelShowTimer();
        CancelHideTimer();
        pendingHide = false;
        pendingShow = false;
        visible = false;
        if (shownInOwnWindow && ownWindow) {
            ownWindow->Hide();
        }
        shownInOwnWindow = false;
        if (targetWindow) {
            targetWindow->RequestWindowComposition();
        }
        renderCtx.reset();
    }

    // ===== ITS OWN WINDOW =====

    void UltraCanvasTooltipManager::SetUseOwnWindow(bool use) {
        if (useOwnWindow == use) return;
        HideTooltipImmediately();
        CloseOwnWindow();
        useOwnWindow = use;
    }

    bool UltraCanvasTooltipManager::GetUseOwnWindow() {
        return useOwnWindow;
    }

    UltraCanvasWindowBase* UltraCanvasTooltipManager::GetTooltipWindow() {
        return (visible && shownInOwnWindow) ? ownWindow.get() : nullptr;
    }

    bool UltraCanvasTooltipManager::OwnWindowAvailable() {
        return kOwnWindowPlatform && useOwnWindow;
    }

    UltraCanvasWindowBase* UltraCanvasTooltipManager::RenderHost() {
        return (shownInOwnWindow && ownWindow) ? ownWindow.get() : targetWindow;
    }

    bool UltraCanvasTooltipManager::ShowInOwnWindow() {
        shownInOwnWindow = false;
        if (!OwnWindowAvailable() || !targetWindow) return false;
        // A tooltip of another window: the window is made again for it (on
        // Wayland it is that window's child, and goes with it).
        if (ownWindow && ownWindowTarget != targetWindow) {
            CloseOwnWindow();
        }
        if (ownWindow && !ownWindow->IsCreated()) {
            ownWindow.reset();
            ownWindowTarget = nullptr;
        }

        const int width = std::max(1, tooltipRect.width);
        const int height = std::max(1, tooltipRect.height);
        if (!ownWindow) {
            WindowConfig config;
            config.title = "Tooltip";
            config.type = WindowType::Tooltip;
            config.width = width;
            config.height = height;
            config.minWidth = 1;
            config.minHeight = 1;
            config.resizable = false;
            config.minimizable = false;
            config.maximizable = false;
            config.closable = false;
            config.backgroundColor = OwnWindowBackground(style);
            config.x = screenPosition.x;
            config.y = screenPosition.y;
#if defined(ULTRACANVAS_DISPLAY_WAYLAND)
            // On Wayland a window is placed only by its parent: the tooltip
            // is a popup of the window it belongs to (or a top-layer surface
            // beside a desktop on the layer shell). Elsewhere it has none: an
            // owned window on Windows would be tied into its owner's place in
            // the stack, the desktop's at the bottom.
            config.parentWindow = targetWindow;
#endif
            auto window = CreateWindow(config);
            if (!window || !window->IsCreated()) {
                debugOutput << "UltraCanvasTooltipManager: no window of its own; the tooltip is drawn in its window" << std::endl;
                if (window) window->Close();
                return false;
            }
            ownWindow = window;
            ownWindowTarget = targetWindow;
        } else {
            ownWindow->SetBackgroundColor(OwnWindowBackground(style));
            int currentWidth = 0, currentHeight = 0;
            ownWindow->GetWindowSize(currentWidth, currentHeight);
            if (currentWidth != width || currentHeight != height) {
                ownWindow->SetWindowSize(width, height);
            }
        }

        // Placed again now that the window, and so its size on the screen,
        // is known; then shown, above every other window.
        PlaceOnScreen();
        ownWindow->SetWindowPosition(screenPosition.x, screenPosition.y);
        shownInOwnWindow = true;
        ownWindow->Show();
        ownWindow->RequestWindowComposition();
        return true;
    }

    void UltraCanvasTooltipManager::CloseOwnWindow() {
        std::shared_ptr<UltraCanvasWindowBase> window = std::move(ownWindow);
        ownWindow.reset();
        ownWindowTarget = nullptr;
        if (shownInOwnWindow) {
            shownInOwnWindow = false;
            renderCtx.reset();
        }
        if (window && window->IsCreated()) {
            window->Close();
        }
    }

    void UltraCanvasTooltipManager::RefreshShown() {
        if (shownInOwnWindow && ownWindow) {
            ownWindow->SetBackgroundColor(OwnWindowBackground(style));
            int currentWidth = 0, currentHeight = 0;
            ownWindow->GetWindowSize(currentWidth, currentHeight);
            const int width = std::max(1, tooltipRect.width);
            const int height = std::max(1, tooltipRect.height);
            if (currentWidth != width || currentHeight != height) {
                ownWindow->SetWindowSize(width, height);
                PlaceOnScreen();
                ownWindow->SetWindowPosition(screenPosition.x, screenPosition.y);
            }
            ownWindow->RequestWindowComposition();
        } else if (targetWindow) {
            targetWindow->RequestWindowComposition();
        }
    }

    void UltraCanvasTooltipManager::ForgetWindow(UltraCanvasWindowBase* win) {
        if (!win) return;
        // The own window closed by someone else (every window closed at once,
        // say): let go of it; the tooltip it showed is gone.
        if (ownWindow.get() == win) {
            if (shownInOwnWindow) {
                shownInOwnWindow = false;
                visible = false;
                pendingHide = false;
                CancelHideTimer();
                renderCtx.reset();
            }
            ownWindow.reset();
            ownWindowTarget = nullptr;
        }
        // The window the tooltip belongs to: the tooltip goes, and its own
        // window with it - a window left over would keep the application
        // running after its last real window closed.
        if (win == targetWindow || win == ownWindowTarget) {
            if (win == targetWindow) {
                HideTooltipImmediately();
                targetWindow = nullptr;
            }
            CloseOwnWindow();
        }
    }

    void UltraCanvasTooltipManager::WindowScaleChanged(UltraCanvasWindowBase* win) {
        if (win && win == ownWindow.get()) {
            // Moved to a monitor of another scale: drawn again at it
            renderCtx.reset();
            if (shownInOwnWindow) ownWindow->RequestWindowComposition();
            return;
        }
        // The tooltip's surface was made for the window's old scale
        HideTooltipImmediately();
    }

    void UltraCanvasTooltipManager::PlaceOnScreen() {
        if (!targetWindow) return;
        // Native screen units per logical pixel of the window the tooltip
        // belongs to (physical px on X11 and Windows, as SetWindowPosition
        // takes them)
        int targetNativeWidth = 0, targetNativeHeight = 0;
        targetWindow->GetNativeWindowSize(targetNativeWidth, targetNativeHeight);
        const double scale = (targetWindow->GetConfig().width > 0 && targetNativeWidth > 0)
                                 ? static_cast<double>(targetNativeWidth) / targetWindow->GetConfig().width
                                 : 1.0;
        auto native = [scale](int logical) { return static_cast<int>(std::lround(logical * scale)); };

        int originX = 0, originY = 0;
        targetWindow->GetContentScreenOrigin(originX, originY);
        const int pointerX = originX + native(cursorPosition.x);
        const int pointerY = originY + native(cursorPosition.y);

        // The tooltip's size on the screen: its own window's, once there is
        // one, else the logical size at the scale of the window it belongs to
        int width = native(tooltipRect.width);
        int height = native(tooltipRect.height);
        if (ownWindow && ownWindowTarget == targetWindow) {
            ownWindow->GetNativeWindowSize(width, height);
        }

        int screenX = 0, screenY = 0, screenWidth = 0, screenHeight = 0;
        targetWindow->GetScreenBounds(screenX, screenY, screenWidth, screenHeight);

        // As inside a window (UpdateTooltipPosition): above and to the right
        // of the pointer; left of it when there is no room on the right;
        // below the pointer's arrow when there is none above - only now the
        // room is the monitor's, not the window's.
        int x = pointerX + native(style.offsetX);
        int y = pointerY - native(style.offsetY) - height;
        if (screenWidth > 0 && screenHeight > 0) {
            if (x + width > screenX + screenWidth) {
                x = pointerX - native(style.offsetX) - width;
            }
            if (y < screenY) {
                y = pointerY + native(kPointerArrowHeight);
                if (y + height > screenY + screenHeight) {
                    y = screenY + screenHeight - height;
                }
            }
            x = std::max(x, screenX);
            y = std::max(y, screenY);
        }
        screenPosition = Point2Di(x, y);
    }

    IRenderContext* UltraCanvasTooltipManager::Render(UltraCanvasWindowBase* win) {
        if (!visible || currentContent.Empty() || !win || win != RenderHost()) return nullptr;

        if (renderCtx) {
            return renderCtx.get();
        }

        const TooltipStyle drawn = DrawnStyle(style, shownInOwnWindow);
        int marginLeft, marginTop, marginRight, marginBottom;
        GetShadowMargins(drawn, marginLeft, marginTop, marginRight, marginBottom);

        renderCtx = CreateRenderContext({tooltipRect.width + marginLeft + marginRight,
                                         tooltipRect.height + marginTop + marginBottom}, win->GetNativeSurface());

        Rect2Di contentRect(marginLeft, marginTop, tooltipRect.width, tooltipRect.height);

        // Draw shadow first
        if (drawn.hasShadow && drawn.shadowColor.a > 0) {
            Rect2Di shadowRect(
                    contentRect.x + drawn.shadowOffset.x,
                    contentRect.y + drawn.shadowOffset.y,
                    contentRect.width,
                    contentRect.height
            );

            if (drawn.shadowBlur <= 0) {
                renderCtx->DrawFilledRectangle(shadowRect, drawn.shadowColor, 0, Colors::Transparent, drawn.cornerRadius);
            } else {
                // No blur primitive in IRenderContext, so approximate a
                // Gaussian falloff by stacking translucent rounded rectangles,
                // each inflated by one more pixel. The per-layer alpha is
                // chosen so the fully overlapped core reaches shadowColor.a.
                int layers = drawn.shadowBlur;
                float coreAlpha = drawn.shadowColor.a / 255.0f;
                float layerAlpha = 1.0f - std::pow(1.0f - coreAlpha, 1.0f / static_cast<float>(layers));
                Color layerColor = drawn.shadowColor;
                layerColor.a = static_cast<uint8_t>(std::clamp(layerAlpha * 255.0f + 0.5f, 1.0f, 255.0f));
                for (int i = layers; i >= 1; --i) {
                    Rect2Di inflated(shadowRect.x - i, shadowRect.y - i,
                                     shadowRect.width + 2 * i, shadowRect.height + 2 * i);
                    renderCtx->DrawFilledRectangle(inflated, layerColor, 0, Colors::Transparent,
                                                   drawn.cornerRadius + static_cast<float>(i));
                }
            }
        }

        // Draw tooltip background
        renderCtx->DrawFilledRectangle(contentRect, style.backgroundColor, style.borderWidth, style.borderColor, drawn.cornerRadius);

        // Draw content blocks
        const int contentWidth = tooltipRect.width - style.paddingLeft - style.paddingRight;
        const double baseX = contentRect.x + style.paddingLeft;
        const double baseY = contentRect.y + style.paddingTop;

        for (const auto& bl : blockLayouts) {
            switch (bl.type) {
                case TooltipBlockType::Title:
                case TooltipBlockType::Text:
                case TooltipBlockType::PlainText:
                    if (bl.textLayout) {
                        renderCtx->SetTextPaint(style.textColor);
                        renderCtx->DrawTextLayout(*bl.textLayout, Point2Dd(baseX, baseY + bl.y));
                    }
                    break;

                case TooltipBlockType::Bullet:
                    if (bulletGlyphLayout) {
                        renderCtx->SetTextPaint(style.textColor);
                        renderCtx->DrawTextLayout(*bulletGlyphLayout, Point2Dd(baseX, baseY + bl.y));
                    }
                    if (bl.textLayout) {
                        renderCtx->SetTextPaint(style.textColor);
                        renderCtx->DrawTextLayout(*bl.textLayout, Point2Dd(baseX + kBulletIndent, baseY + bl.y));
                    }
                    break;

                case TooltipBlockType::Row: {
                    if (bl.swatch.a > 0) {
                        Rect2Di swatchRect(static_cast<int>(baseX),
                                           static_cast<int>(baseY) + bl.y + (bl.height - kSwatchSize) / 2,
                                           kSwatchSize, kSwatchSize);
                        renderCtx->DrawFilledRectangle(swatchRect, bl.swatch, 0, Colors::Transparent, 2.0f);
                    }

                    const int n = bl.columns;
                    const bool wide = (n == 3);
                    const int* colWidth = wide ? rowGrid3.data() : rowGrid2.data();
                    const int* colPrefix = wide ? rowPrefix3.data() : rowPrefix2.data();
                    const TooltipColumnAlign* align = wide ? rowAlign3.data() : rowAlign2.data();
                    int colX[kMaxRowColumns] = {0, 0, 0};
                    ComputeColumnOrigins(colWidth, n, contentWidth, rowLabelIndent,
                                         style.columnGap, colX);

                    for (int i = 0; i < n; ++i) {
                        if (!bl.cells[i]) continue;
                        // Labels are muted, the value columns use the main text color
                        renderCtx->SetTextPaint(i == 0 ? style.secondaryTextColor : style.textColor);
                        int x = AlignedCellX(colX[i], colWidth[i], bl.cellWidth[i],
                                             bl.cellPrefixWidth[i], colPrefix[i], align[i]);
                        renderCtx->DrawTextLayout(*bl.cells[i], Point2Dd(baseX + x, baseY + bl.y));
                    }
                    break;
                }

                case TooltipBlockType::Separator: {
                    Rect2Di sepRect(static_cast<int>(baseX), static_cast<int>(baseY) + bl.y, contentWidth, 1);
                    renderCtx->DrawFilledRectangle(sepRect, style.separatorColor, 0, Colors::Transparent, 0);
                    break;
                }
            }
        }

        return renderCtx.get();
    }

    Point2Di UltraCanvasTooltipManager::GetCompositePosition() {
        // In its own window the tooltip fills it, from the corner
        if (shownInOwnWindow) return Point2Di(0, 0);
        int marginLeft, marginTop, marginRight, marginBottom;
        GetShadowMargins(style, marginLeft, marginTop, marginRight, marginBottom);
        return Point2Di(tooltipRect.x - marginLeft, tooltipRect.y - marginTop);
    }

    void UltraCanvasTooltipManager::SetStyle(const TooltipStyle &newStyle) {
        if (style != newStyle) {
            renderCtx.reset();
        }
        style = newStyle;
        if (visible && targetWindow) {
            CalculateTooltipLayout(); // Recalculate if currently visible
            UpdateTooltipPosition(cursorPosition);
            RefreshShown();
            targetWindow->RequestRedraw();
        }
    }


    void UltraCanvasTooltipManager::CalculateTooltipLayout() {
        blockLayouts.clear();
        bulletGlyphLayout.reset();
        rowLabelIndent = 0;
        rowGrid2 = {0, 0};
        rowGrid3 = {0, 0, 0};
        rowPrefix2 = {0, 0};
        rowPrefix3 = {0, 0, 0};
        rowAlign2 = currentContent.columnAlign2Override
            ? *currentContent.columnAlign2Override : style.columnAlign2;
        rowAlign3 = currentContent.columnAlign3Override
            ? *currentContent.columnAlign3Override : style.columnAlign3;
        if (currentContent.Empty() || !targetWindow) return;

        auto ctx = targetWindow->GetRenderContext();
        const int innerMax = std::max(20, style.maxWidth - style.paddingLeft - style.paddingRight);

        // Rows with a color swatch shift every label so the columns stay aligned
        bool anySwatch = false;
        bool anyBullet = false;
        for (const auto& block : currentContent.blocks) {
            if (block.type == TooltipBlockType::Row && block.swatch.a > 0) anySwatch = true;
            if (block.type == TooltipBlockType::Bullet) anyBullet = true;
        }
        rowLabelIndent = anySwatch ? kSwatchSize + kSwatchGap : 0;

        if (anyBullet) {
            bulletGlyphLayout = ctx->CreateTextLayout(SpanMarkup(style, style.fontSize, false, "•"), true);
        }

        // Pass 1: create layouts and measure natural widths. The two- and
        // three-column rows are measured separately, into their own grid.
        int maxBlockWidth = 0;
        std::array<int, 2> natural2 = {0, 0};
        std::array<int, 3> natural3 = {0, 0, 0};
        // Decimal columns split their measurement either side of the anchor
        std::array<int, 2> prefix2 = {0, 0}, suffix2 = {0, 0};
        std::array<int, 3> prefix3 = {0, 0, 0}, suffix3 = {0, 0, 0};
        bool anyRow2 = false, anyRow3 = false;
        for (const auto& block : currentContent.blocks) {
            TooltipBlockLayout bl;
            bl.type = block.type;
            bl.swatch = block.swatch;

            switch (block.type) {
                case TooltipBlockType::Title:
                    bl.textLayout = ctx->CreateTextLayout(
                        SpanMarkup(style, style.titleFontSize, true, EscapeMarkup(block.text)), true);
                    break;
                case TooltipBlockType::Text:
                    // Free text: Pango markup is interpreted, not escaped
                    bl.textLayout = ctx->CreateTextLayout(
                        SpanMarkup(style, style.fontSize, false, block.text), true);
                    break;
                case TooltipBlockType::PlainText:
                    // Plain data: escape so < > & render literally
                    bl.textLayout = ctx->CreateTextLayout(
                        SpanMarkup(style, style.fontSize, false, EscapeMarkup(block.text)), true);
                    break;
                case TooltipBlockType::Bullet:
                    bl.textLayout = ctx->CreateTextLayout(
                        SpanMarkup(style, style.fontSize, false, EscapeMarkup(block.text)), true);
                    break;
                case TooltipBlockType::Row: {
                    bl.columns = std::clamp(block.columns, 2, kMaxRowColumns);
                    const std::string* cellText[kMaxRowColumns] = {
                        &block.text, &block.value, &block.value2};
                    const bool wide = (bl.columns == 3);
                    int* natural = wide ? natural3.data() : natural2.data();
                    int* prefix = wide ? prefix3.data() : prefix2.data();
                    int* suffix = wide ? suffix3.data() : suffix2.data();
                    const TooltipColumnAlign* align = wide ? rowAlign3.data() : rowAlign2.data();
                    if (wide) anyRow3 = true; else anyRow2 = true;
                    for (int i = 0; i < bl.columns; ++i) {
                        bl.cells[i] = ctx->CreateTextLayout(
                            SpanMarkup(style, style.fontSize, false, EscapeMarkup(*cellText[i])), true);
                        const int w = static_cast<int>(bl.cells[i]->GetLayoutSize().width);
                        natural[i] = std::max(natural[i], w);

                        if (align[i] != TooltipColumnAlign::Decimal) continue;
                        // Measure the integer part on its own; the column then
                        // reserves the widest integer part plus the widest
                        // fractional part, which is wider than any single cell.
                        const size_t anchor = DecimalAnchor(*cellText[i]);
                        int pw = 0;
                        if (anchor > 0) {
                            auto head = ctx->CreateTextLayout(
                                SpanMarkup(style, style.fontSize, false,
                                           EscapeMarkup(cellText[i]->substr(0, anchor))), true);
                            pw = std::min(w, static_cast<int>(head->GetLayoutSize().width));
                        }
                        bl.cellPrefixWidth[i] = pw;
                        prefix[i] = std::max(prefix[i], pw);
                        suffix[i] = std::max(suffix[i], w - pw);
                    }
                    break;
                }
                case TooltipBlockType::Separator:
                    break;
            }
            blockLayouts.push_back(std::move(bl));
        }

        // A Decimal column has to hold the widest integer part and the widest
        // fraction at once, which no single cell need be that wide.
        for (int i = 0; i < 2; ++i) {
            if (rowAlign2[i] == TooltipColumnAlign::Decimal) {
                natural2[i] = std::max(natural2[i], prefix2[i] + suffix2[i]);
            }
        }
        for (int i = 0; i < 3; ++i) {
            if (rowAlign3[i] == TooltipColumnAlign::Decimal) {
                natural3[i] = std::max(natural3[i], prefix3[i] + suffix3[i]);
            }
        }
        rowPrefix2 = prefix2;
        rowPrefix3 = prefix3;

        // Column widths per grid, and the width each grid needs. The floor on
        // the available space keeps a cell from collapsing to zero width when
        // padding and gaps eat the whole of maxWidth.
        if (anyRow2) {
            DistributeColumns(natural2.data(), 2,
                              std::max(2, innerMax - rowLabelIndent - style.columnGap),
                              rowGrid2.data());
            maxBlockWidth = std::max(maxBlockWidth,
                rowLabelIndent + rowGrid2[0] + style.columnGap + rowGrid2[1]);
        }
        if (anyRow3) {
            DistributeColumns(natural3.data(), 3,
                              std::max(3, innerMax - rowLabelIndent - 2 * style.columnGap),
                              rowGrid3.data());
            maxBlockWidth = std::max(maxBlockWidth,
                rowLabelIndent + rowGrid3[0] + rowGrid3[1] + rowGrid3[2] + 2 * style.columnGap);
        }

        // Pass 2: wrap over-wide blocks and take final measurements
        for (auto& bl : blockLayouts) {
            switch (bl.type) {
                case TooltipBlockType::Title:
                case TooltipBlockType::Text:
                case TooltipBlockType::PlainText: {
                    int w = static_cast<int>(bl.textLayout->GetLayoutSize().width);
                    if (w > innerMax) {
                        w = WrapToWidth(*bl.textLayout, innerMax);
                    }
                    maxBlockWidth = std::max(maxBlockWidth, w);
                    break;
                }
                case TooltipBlockType::Bullet: {
                    int w = static_cast<int>(bl.textLayout->GetLayoutSize().width);
                    if (w > innerMax - kBulletIndent) {
                        w = WrapToWidth(*bl.textLayout, innerMax - kBulletIndent);
                    }
                    maxBlockWidth = std::max(maxBlockWidth, kBulletIndent + w);
                    break;
                }
                case TooltipBlockType::Row: {
                    const int* colWidth = (bl.columns == 3) ? rowGrid3.data() : rowGrid2.data();
                    const TooltipColumnAlign* align =
                        (bl.columns == 3) ? rowAlign3.data() : rowAlign2.data();
                    for (int i = 0; i < bl.columns; ++i) {
                        int w = static_cast<int>(bl.cells[i]->GetLayoutSize().width);
                        if (w > colWidth[i]) {
                            // Cell wraps inside its column: its own lines follow
                            // the column alignment too, not just the cell box,
                            // so the cell is as wide as the column it fills.
                            WrapToWidth(*bl.cells[i], colWidth[i]);
                            bl.cells[i]->SetAlignment(ToTextAlignment(align[i]));
                            w = colWidth[i];
                        }
                        bl.cellWidth[i] = w;
                    }
                    break;
                }
                case TooltipBlockType::Separator:
                    break;
            }
        }

        // Pass 3: stack the blocks vertically
        int y = 0;
        bool first = true;
        for (auto& bl : blockLayouts) {
            if (!first) y += style.rowSpacing;
            first = false;

            switch (bl.type) {
                case TooltipBlockType::Title:
                case TooltipBlockType::Text:
                case TooltipBlockType::PlainText:
                case TooltipBlockType::Bullet:
                    bl.height = static_cast<int>(bl.textLayout->GetLayoutSize().height);
                    break;
                case TooltipBlockType::Row: {
                    bl.height = bl.swatch.a > 0 ? kSwatchSize : 0;
                    for (int i = 0; i < bl.columns; ++i) {
                        bl.height = std::max(bl.height,
                            static_cast<int>(bl.cells[i]->GetLayoutSize().height));
                    }
                    break;
                }
                case TooltipBlockType::Separator:
                    y += kSeparatorMargin;
                    bl.height = 1;
                    break;
            }
            bl.y = y;
            y += bl.height;
            if (bl.type == TooltipBlockType::Separator) {
                y += kSeparatorMargin;
            }
        }

        tooltipRect.width = std::max(maxBlockWidth + style.paddingLeft + style.paddingRight, 20);
        tooltipRect.height = std::max(y + style.paddingTop + style.paddingBottom, 15);
    }

    void UltraCanvasTooltipManager::UpdateTooltipPosition(const Point2Di &pointer) {
        if (!targetWindow) return;
        cursorPosition = pointer;
        int windowWidth = targetWindow->GetWidth();
        int windowHeight = targetWindow->GetHeight();

        // Above and to the right of the pointer, with a gap: the line the
        // pointer is on - the text being read - stays in view, and so does
        // the pointer's arrow, which hangs down and right from its tip. Below
        // the pointer the tooltip covered the lower half of that line, and
        // its shadow the rest. The gap is measured from the tooltip's body;
        // its soft shadow, mostly below it, fades out inside the gap.
        tooltipRect.x = cursorPosition.x + style.offsetX;
        tooltipRect.y = cursorPosition.y - style.offsetY - tooltipRect.height;

        if (windowWidth > 0 && windowHeight > 0) {
            // No room on the right: to the left of the pointer instead.
            if (tooltipRect.x + tooltipRect.width > windowWidth) {
                tooltipRect.x = cursorPosition.x - style.offsetX - tooltipRect.width;
            }
            // No room above (near the top of the window): below the pointer,
            // under its arrow rather than over it.
            if (tooltipRect.y < 0) {
                tooltipRect.y = cursorPosition.y + kPointerArrowHeight;
                if (tooltipRect.y + tooltipRect.height > windowHeight) {
                    tooltipRect.y = windowHeight - tooltipRect.height;
                }
            }

            // Ensure tooltip is not off-screen
            tooltipRect.x = std::max(tooltipRect.x, 0);
            tooltipRect.y = std::max(tooltipRect.y, 0);
        }

        // In its own window the room is the monitor's: placed on the screen,
        // and the window follows while the tooltip is up
        if (OwnWindowAvailable()) {
            PlaceOnScreen();
            if (shownInOwnWindow && ownWindow) {
                ownWindow->SetWindowPosition(screenPosition.x, screenPosition.y);
            }
        }
    }

} // namespace UltraCanvas
