// include/Plugins/Diagrams/UltraCanvasTickerBoard.h
// Ticker board: a strip of items - a label, a value, a change - crawling
// across a band without end. The stock ticker along the bottom of a news
// channel, the headline crawl, the LED sign over a shop door. Two faces:
// Text draws the strip with a font; DotMatrix lights it on a fixed grid of
// 5x7 LED dots that the text steps across one column at a time, the way a
// real sign does. An item with only a label is a plain crawling message.
//
// The board moves by itself: a timer advances it at pixelsPerSecond from the
// first frame, pauses while the pointer rests on it (pauseOnHover), and stops
// with Stop(). Items can be replaced while it rolls - UpdateItem() keeps the
// position - so a live feed changes the numbers without a jump.
//
// For a still LED readout of one value use UltraCanvasGaugeDiagramElement in
// GaugeMode::Digital; this element exists to move.
//
// Version: 1.0.0
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasTimer.h"
#include "UltraCanvasCommonTypes.h"
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

// ===== TICKER BOARD FACE =====
    enum class TickerBoardFace {
        Text,       // the strip set in a font (fontFamily, fontSize) on a plain band
        DotMatrix   // the strip lit on a grid of round LED dots, 7 rows high,
                    // glyphs 5 dots wide, stepping one column at a time
    };

// ===== TICKER DIRECTION =====
    enum class TickerDirection {
        Leftward,   // the usual crawl: items enter at the right and leave at the left
        Rightward
    };

// ===== TICKER TREND =====
    // Colours the change field (and picks its arrow): upColor, downColor or
    // flatColor from the style.
    enum class TickerTrend {
        Flat,
        Up,
        Down
    };

// ===== TICKER ITEM =====
    struct TickerItem {
        std::string label;          // "AAPL", "Platform 4", a headline
        std::string value;          // "189.30" - already formatted, drawn after the label
        std::string change;         // "+1.25 (+0.66%)" - drawn after the value in the trend colour
        TickerTrend trend = TickerTrend::Flat;
        // Fully transparent means "take the style's colour".
        Color labelColor = Color(0, 0, 0, 0);
        Color valueColor = Color(0, 0, 0, 0);
        std::string tooltip;        // shown while the pointer rests on the item

        TickerItem() = default;
        explicit TickerItem(const std::string& text) : label(text) {}
        TickerItem(const std::string& labelText, const std::string& valueText,
                   const std::string& changeText = "", TickerTrend t = TickerTrend::Flat)
                : label(labelText), value(valueText), change(changeText), trend(t) {}
    };

    // A quote with its numbers formatted here, dot-decimal, `decimals` places:
    // price 189.3, change 1.25 gives value "189.30", change "+1.25 (+0.66%)"
    // and trend Up. The percentage is against the previous price.
    TickerItem MakeTickerQuote(const std::string& symbol, double price, double change,
                               int decimals = 2, bool withPercent = true);

// ===== TICKER BOARD STYLE =====
    struct TickerBoardStyle {
        TickerBoardFace face = TickerBoardFace::Text;
        TickerDirection direction = TickerDirection::Leftward;
        float pixelsPerSecond = 60.0f;       // crawl speed; DotMatrix moves in whole columns
        unsigned int frameIntervalMs = 33;   // ~30 fps
        bool pauseOnHover = true;            // hold still while the pointer is on the board

        // Colours. The change field takes upColor / downColor / flatColor by
        // the item's trend; the arrow before it takes the same colour.
        Color backgroundColor = Color(18, 20, 26, 255);
        Color labelColor      = Color(232, 234, 238, 255);
        Color valueColor      = Color(255, 255, 255, 255);
        Color upColor         = Color(56, 214, 122, 255);
        Color downColor       = Color(244, 84, 84, 255);
        Color flatColor       = Color(162, 168, 178, 255);
        Color separatorColor  = Color(96, 102, 112, 255);
        float cornerRadius    = 0.0f;        // of the band; 0 is square

        bool showArrows = true;              // a triangle before the change when the trend is not Flat
        std::string separator = "\xE2\x80\xA2";   // between items (a bullet); empty for a gap only

        // ----- Text face -----
        std::string fontFamily;              // empty = the default UI font; a comma list falls back
        float fontSize = 0.0f;               // 0 = sized from the band height
        FontWeight labelWeight = FontWeight::Bold;
        FontWeight valueWeight = FontWeight::Normal;
        float fieldGap = 10.0f;              // px between label, value and change
        float itemGap  = 36.0f;              // px either side of the separator

        // ----- DotMatrix face -----
        float dotPitch = 0.0f;               // dot centre to centre; 0 = fit 7 rows to the height
        float dotFill  = 0.78f;              // dot diameter as a share of the pitch (0.3..1)
        bool  showOffDots = true;            // draw the unlit grid in offDotColor
        Color offDotColor = Color(255, 255, 255, 16);
        int   glyphGapColumns = 1;           // blank columns between glyphs
        int   fieldGapColumns = 3;           // blank columns between label, value and change
        int   itemGapColumns  = 8;           // blank columns either side of the separator
    };

// ===== TICKER BOARD ELEMENT =====
    class UltraCanvasTickerBoard : public UltraCanvasUIElement {
    public:
        // ===== ACCESSIBILITY =====
        // Read as text: the items in order, as a screen reader would read a label.
        AccessibleRole GetAccessibleRole() const override { return AccessibleRole::Label; }
        std::string GetAccessibleValueText() const override;

        UltraCanvasTickerBoard(const std::string& identifier, float x, float y, float w, float h);
        ~UltraCanvasTickerBoard() override;

        // ===== ITEMS =====
        void SetItems(const std::vector<TickerItem>& newItems);
        size_t AddItem(const TickerItem& item);          // returns its index
        // Replaces one item without moving the strip: a live feed updates the
        // numbers in place and the reader never sees the board jump.
        void UpdateItem(size_t index, const TickerItem& item);
        void RemoveItem(size_t index);
        void ClearItems();
        const std::vector<TickerItem>& GetItems() const { return items; }
        size_t GetItemCount() const { return items.size(); }
        // One label-only item: a crawling message.
        void SetText(const std::string& message);

        // ===== MOTION =====
        // The board starts moving on its first frame. Start() and Stop() are
        // idempotent; a stopped board draws where it halted.
        void Start();
        void Stop();
        void SetRunning(bool running) { running ? Start() : Stop(); }
        bool IsRunning() const { return running; }
        // Position of the strip in pixels (DotMatrix: in whole columns' worth
        // of pixels), wrapped to the strip length. 0 puts the first item's
        // label at the left edge.
        void SetOffset(double pixels);
        double GetOffset() const { return offset; }
        // The length of one pass of the strip in pixels (DotMatrix: columns
        // times the dot pitch), as measured for the last frame drawn; 0
        // before the first frame.
        double GetStripLength() const { return stripWidth; }

        // ===== STYLE =====
        TickerBoardStyle& GetStyle() { return style; }
        const TickerBoardStyle& GetStyle() const { return style; }
        void SetStyle(const TickerBoardStyle& s);
        void SetFace(TickerBoardFace face);
        void SetDirection(TickerDirection direction);
        void SetSpeed(float pixelsPerSecond);

        // ===== CALLBACKS =====
        std::function<void(size_t index)> onItemClick;     // left button released on an item
        // The item under the pointer changed; SIZE_MAX when it left every item.
        std::function<void(size_t index)> onItemHover;

        // The item under board-local x, or SIZE_MAX between items. Uses the
        // layout of the last frame drawn.
        size_t GetItemAt(int localX) const;

        // ===== BASE OVERRIDES =====
        bool AcceptsFocus() const override { return false; }
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        bool OnEvent(const UCEvent& event) override;

    private:
        // ----- Text face layout: one run per drawn string -----
        struct TextRun {
            std::string text;
            Color color;
            FontWeight weight = FontWeight::Normal;
            double x = 0.0;         // strip space
            double width = 0.0;
            size_t item = SIZE_MAX; // SIZE_MAX for a separator
        };
        // ----- DotMatrix face layout: one entry per strip column -----
        struct DotColumn {
            uint8_t bits = 0;       // bit n lit = row n, row 0 at the top
            Color color;
            size_t item = SIZE_MAX;
        };

        std::vector<TickerItem> items;
        TickerBoardStyle style;

        bool running = true;
        TimerId timerId = InvalidTimerId;
        std::chrono::steady_clock::time_point lastTick{};
        double offset = 0.0;

        bool layoutDirty = true;
        std::vector<TextRun> runs;
        double stripWidth = 0.0;    // Text: px; DotMatrix: columns * pitch
        std::vector<DotColumn> columns;
        double dotPitchUsed = 0.0;
        FontStyle fontUsed;         // the font the runs were measured with
        float heightUsed = 0.0f;

        size_t hoveredItem = SIZE_MAX;
        bool tooltipShown = false;

        void EnsureTimer();
        void StopTimer();
        void Advance();
        void MarkLayoutDirty();

        FontStyle ResolveFont(float height) const;
        double ResolvePitch(float height) const;
        Color TrendColor(TickerTrend trend) const;
        void BuildTextLayout(IRenderContext* ctx, float height);
        void BuildDotLayout(float height);
        void AppendGlyphColumns(const std::string& utf8, const Color& color, size_t item);
        void AppendBlankColumns(int count);
        void RenderText(IRenderContext* ctx, float w, float h);
        void RenderDotMatrix(IRenderContext* ctx, float w, float h);
        void SetHoveredItem(size_t item, const Point2Di& windowPos);
    };

// ===== FACTORY =====
    // A band at (x, y) of w x h; 36-48 px tall reads well for Text, 40-64 for
    // DotMatrix (7 rows of dots fit to the height).
    inline std::shared_ptr<UltraCanvasTickerBoard> CreateTickerBoard(
            const std::string& identifier, float x, float y, float w, float h,
            TickerBoardFace face = TickerBoardFace::Text) {
        auto board = std::make_shared<UltraCanvasTickerBoard>(identifier, x, y, w, h);
        board->SetFace(face);
        return board;
    }

} // namespace UltraCanvas
