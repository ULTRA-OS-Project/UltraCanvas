// Plugins/Diagrams/UltraCanvasTickerBoard.cpp
// Ticker board: a crawling strip of labels, values and changes, set in a font
// or lit on a 5x7 LED dot grid. Platform-independent.
// Version: 1.0.0
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasTickerBoard.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasTooltipManager.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace UltraCanvas {

    namespace {
        constexpr int kDotRows = 7;
        constexpr int kGlyphColumns = 5;

        // The classic 5x7 dot-matrix font, ASCII 32..126, column-major: five
        // bytes per glyph, bit n of a byte lights row n (row 0 at the top).
        const uint8_t kFont5x7[95][kGlyphColumns] = {
            {0x00,0x00,0x00,0x00,0x00}, // space
            {0x00,0x00,0x5F,0x00,0x00}, // !
            {0x00,0x07,0x00,0x07,0x00}, // "
            {0x14,0x7F,0x14,0x7F,0x14}, // #
            {0x24,0x2A,0x7F,0x2A,0x12}, // $
            {0x23,0x13,0x08,0x64,0x62}, // %
            {0x36,0x49,0x55,0x22,0x50}, // &
            {0x00,0x05,0x03,0x00,0x00}, // '
            {0x00,0x1C,0x22,0x41,0x00}, // (
            {0x00,0x41,0x22,0x1C,0x00}, // )
            {0x14,0x08,0x3E,0x08,0x14}, // *
            {0x08,0x08,0x3E,0x08,0x08}, // +
            {0x00,0x50,0x30,0x00,0x00}, // ,
            {0x08,0x08,0x08,0x08,0x08}, // -
            {0x00,0x60,0x60,0x00,0x00}, // .
            {0x20,0x10,0x08,0x04,0x02}, // /
            {0x3E,0x51,0x49,0x45,0x3E}, // 0
            {0x00,0x42,0x7F,0x40,0x00}, // 1
            {0x42,0x61,0x51,0x49,0x46}, // 2
            {0x21,0x41,0x45,0x4B,0x31}, // 3
            {0x18,0x14,0x12,0x7F,0x10}, // 4
            {0x27,0x45,0x45,0x45,0x39}, // 5
            {0x3C,0x4A,0x49,0x49,0x30}, // 6
            {0x01,0x71,0x09,0x05,0x03}, // 7
            {0x36,0x49,0x49,0x49,0x36}, // 8
            {0x06,0x49,0x49,0x29,0x1E}, // 9
            {0x00,0x36,0x36,0x00,0x00}, // :
            {0x00,0x56,0x36,0x00,0x00}, // ;
            {0x00,0x08,0x14,0x22,0x41}, // <
            {0x14,0x14,0x14,0x14,0x14}, // =
            {0x41,0x22,0x14,0x08,0x00}, // >
            {0x02,0x01,0x51,0x09,0x06}, // ?
            {0x32,0x49,0x79,0x41,0x3E}, // @
            {0x7E,0x11,0x11,0x11,0x7E}, // A
            {0x7F,0x49,0x49,0x49,0x36}, // B
            {0x3E,0x41,0x41,0x41,0x22}, // C
            {0x7F,0x41,0x41,0x22,0x1C}, // D
            {0x7F,0x49,0x49,0x49,0x41}, // E
            {0x7F,0x09,0x09,0x01,0x01}, // F
            {0x3E,0x41,0x41,0x51,0x32}, // G
            {0x7F,0x08,0x08,0x08,0x7F}, // H
            {0x00,0x41,0x7F,0x41,0x00}, // I
            {0x20,0x40,0x41,0x3F,0x01}, // J
            {0x7F,0x08,0x14,0x22,0x41}, // K
            {0x7F,0x40,0x40,0x40,0x40}, // L
            {0x7F,0x02,0x04,0x02,0x7F}, // M
            {0x7F,0x04,0x08,0x10,0x7F}, // N
            {0x3E,0x41,0x41,0x41,0x3E}, // O
            {0x7F,0x09,0x09,0x09,0x06}, // P
            {0x3E,0x41,0x51,0x21,0x5E}, // Q
            {0x7F,0x09,0x19,0x29,0x46}, // R
            {0x46,0x49,0x49,0x49,0x31}, // S
            {0x01,0x01,0x7F,0x01,0x01}, // T
            {0x3F,0x40,0x40,0x40,0x3F}, // U
            {0x1F,0x20,0x40,0x20,0x1F}, // V
            {0x7F,0x20,0x18,0x20,0x7F}, // W
            {0x63,0x14,0x08,0x14,0x63}, // X
            {0x03,0x04,0x78,0x04,0x03}, // Y
            {0x61,0x51,0x49,0x45,0x43}, // Z
            {0x00,0x00,0x7F,0x41,0x41}, // [
            {0x02,0x04,0x08,0x10,0x20}, // backslash
            {0x41,0x41,0x7F,0x00,0x00}, // ]
            {0x04,0x02,0x01,0x02,0x04}, // ^
            {0x40,0x40,0x40,0x40,0x40}, // _
            {0x00,0x01,0x02,0x04,0x00}, // `
            {0x20,0x54,0x54,0x54,0x78}, // a
            {0x7F,0x48,0x44,0x44,0x38}, // b
            {0x38,0x44,0x44,0x44,0x20}, // c
            {0x38,0x44,0x44,0x48,0x7F}, // d
            {0x38,0x54,0x54,0x54,0x18}, // e
            {0x08,0x7E,0x09,0x01,0x02}, // f
            {0x08,0x14,0x54,0x54,0x3C}, // g
            {0x7F,0x08,0x04,0x04,0x78}, // h
            {0x00,0x44,0x7D,0x40,0x00}, // i
            {0x20,0x40,0x44,0x3D,0x00}, // j
            {0x00,0x7F,0x10,0x28,0x44}, // k
            {0x00,0x41,0x7F,0x40,0x00}, // l
            {0x7C,0x04,0x18,0x04,0x78}, // m
            {0x7C,0x08,0x04,0x04,0x78}, // n
            {0x38,0x44,0x44,0x44,0x38}, // o
            {0x7C,0x14,0x14,0x14,0x08}, // p
            {0x08,0x14,0x14,0x18,0x7C}, // q
            {0x7C,0x08,0x04,0x04,0x08}, // r
            {0x48,0x54,0x54,0x54,0x20}, // s
            {0x04,0x3F,0x44,0x40,0x20}, // t
            {0x3C,0x40,0x40,0x20,0x7C}, // u
            {0x1C,0x20,0x40,0x20,0x1C}, // v
            {0x3C,0x40,0x30,0x40,0x3C}, // w
            {0x44,0x28,0x10,0x28,0x44}, // x
            {0x0C,0x50,0x50,0x50,0x3C}, // y
            {0x44,0x64,0x54,0x4C,0x44}, // z
            {0x00,0x08,0x36,0x41,0x00}, // {
            {0x00,0x00,0x7F,0x00,0x00}, // |
            {0x00,0x41,0x36,0x08,0x00}, // }
            {0x08,0x04,0x08,0x10,0x08}, // ~
        };

        const uint8_t kGlyphUpArrow[kGlyphColumns]   = {0x10,0x18,0x1C,0x18,0x10}; // U+25B2
        const uint8_t kGlyphDownArrow[kGlyphColumns] = {0x04,0x0C,0x1C,0x0C,0x04}; // U+25BC
        const uint8_t kGlyphBullet[kGlyphColumns]    = {0x00,0x18,0x18,0x00,0x00}; // U+2022
        const uint8_t kGlyphDegree[kGlyphColumns]    = {0x00,0x06,0x09,0x09,0x06}; // U+00B0
        const uint8_t kGlyphEuro[kGlyphColumns]      = {0x14,0x3E,0x55,0x55,0x41}; // U+20AC
        const uint8_t kGlyphPound[kGlyphColumns]     = {0x48,0x7E,0x49,0x41,0x42}; // U+00A3
        const uint8_t kGlyphUnknown[kGlyphColumns]   = {0x7F,0x41,0x41,0x41,0x7F}; // a box

        // Latin-1 letters with diacritics drawn as their base letter: a sign
        // with 35 dots per glyph has no room for an accent, and "Zurich" beats
        // a box.
        const char kLatin1Base[64] = {
            'A','A','A','A','A','A','A','C','E','E','E','E','I','I','I','I',
            'D','N','O','O','O','O','O','x','O','U','U','U','U','Y','T','s',
            'a','a','a','a','a','a','a','c','e','e','e','e','i','i','i','i',
            'd','n','o','o','o','o','o','/','o','u','u','u','u','y','t','y'
        };

        // One code point from a UTF-8 string; advances `i`. A malformed byte
        // counts as one unknown character so the walk always ends.
        uint32_t NextCodePoint(const std::string& s, size_t& i) {
            const unsigned char c = static_cast<unsigned char>(s[i]);
            int extra = 0;
            uint32_t cp = c;
            if (c >= 0xF0)      { extra = 3; cp = c & 0x07; }
            else if (c >= 0xE0) { extra = 2; cp = c & 0x0F; }
            else if (c >= 0xC0) { extra = 1; cp = c & 0x1F; }
            else if (c >= 0x80) { ++i; return 0xFFFD; }
            ++i;
            for (int k = 0; k < extra; ++k) {
                if (i >= s.size()) return 0xFFFD;
                const unsigned char cc = static_cast<unsigned char>(s[i]);
                if ((cc & 0xC0) != 0x80) return 0xFFFD;
                cp = (cp << 6) | (cc & 0x3F);
                ++i;
            }
            return cp;
        }

        const uint8_t* GlyphFor(uint32_t cp) {
            if (cp >= 32 && cp <= 126) return kFont5x7[cp - 32];
            if (cp >= 0xC0 && cp <= 0xFF) {
                const char base = kLatin1Base[cp - 0xC0];
                return kFont5x7[static_cast<unsigned char>(base) - 32];
            }
            switch (cp) {
                case 0x25B2: return kGlyphUpArrow;
                case 0x25BC: return kGlyphDownArrow;
                case 0x2022: return kGlyphBullet;
                case 0x00B0: return kGlyphDegree;
                case 0x20AC: return kGlyphEuro;
                case 0x00A3: return kGlyphPound;
                case 0x00A0: return kFont5x7[0];
                default:     return kGlyphUnknown;
            }
        }

        constexpr const char* kUpArrowUtf8   = "\xE2\x96\xB2";
        constexpr const char* kDownArrowUtf8 = "\xE2\x96\xBC";

        bool IsTransparent(const Color& c) { return c.a == 0; }

        // x wrapped into [0, period).
        double Wrap(double x, double period) {
            if (period <= 0.0) return 0.0;
            double r = std::fmod(x, period);
            if (r < 0.0) r += period;
            return r;
        }
    }

// ===== QUOTE FORMATTING =====

    TickerItem MakeTickerQuote(const std::string& symbol, double price, double change,
                               int decimals, bool withPercent) {
        decimals = std::clamp(decimals, 0, 8);
        // Shown to a person, so the stream formats in the application's
        // locale, as the rule for displayed numbers asks.
        std::ostringstream value;   // locale-ok: a price a person reads
        value << std::fixed << std::setprecision(decimals) << price;

        std::ostringstream delta;   // locale-ok: a change a person reads
        delta << std::fixed << std::setprecision(decimals);
        if (change > 0.0) delta << '+';
        delta << change;
        const double previous = price - change;
        if (withPercent && previous != 0.0) {
            const double pct = change / std::fabs(previous) * 100.0;
            delta << " (" << (pct > 0.0 ? "+" : "") << std::setprecision(2) << pct << "%)";
        }

        TickerItem item(symbol, value.str(), delta.str());
        item.trend = change > 0.0 ? TickerTrend::Up
                   : change < 0.0 ? TickerTrend::Down
                   : TickerTrend::Flat;
        return item;
    }

// ===== LIFECYCLE =====

    UltraCanvasTickerBoard::UltraCanvasTickerBoard(const std::string& identifier,
                                                   float x, float y, float w, float h)
            : UltraCanvasUIElement(identifier, x, y, w, h) {
        mouseCursor = UCMouseCursor::Default;
    }

    UltraCanvasTickerBoard::~UltraCanvasTickerBoard() {
        // The timer callback captures `this`; it must not outlive the element.
        StopTimer();
    }

    std::string UltraCanvasTickerBoard::GetAccessibleValueText() const {
        std::string text;
        for (const auto& item : items) {
            if (!text.empty()) text += "; ";
            text += item.label;
            if (!item.value.empty())  text += " " + item.value;
            if (!item.change.empty()) text += " " + item.change;
        }
        return text;
    }

// ===== ITEMS =====

    void UltraCanvasTickerBoard::SetItems(const std::vector<TickerItem>& newItems) {
        items = newItems;
        hoveredItem = SIZE_MAX;
        MarkLayoutDirty();
    }

    size_t UltraCanvasTickerBoard::AddItem(const TickerItem& item) {
        items.push_back(item);
        MarkLayoutDirty();
        return items.size() - 1;
    }

    void UltraCanvasTickerBoard::UpdateItem(size_t index, const TickerItem& item) {
        if (index >= items.size()) return;
        items[index] = item;
        MarkLayoutDirty();
    }

    void UltraCanvasTickerBoard::RemoveItem(size_t index) {
        if (index >= items.size()) return;
        items.erase(items.begin() + static_cast<std::ptrdiff_t>(index));
        hoveredItem = SIZE_MAX;
        MarkLayoutDirty();
    }

    void UltraCanvasTickerBoard::ClearItems() {
        items.clear();
        hoveredItem = SIZE_MAX;
        MarkLayoutDirty();
    }

    void UltraCanvasTickerBoard::SetText(const std::string& message) {
        SetItems({TickerItem(message)});
    }

    void UltraCanvasTickerBoard::MarkLayoutDirty() {
        layoutDirty = true;
        RequestRedraw();
    }

// ===== MOTION =====

    void UltraCanvasTickerBoard::EnsureTimer() {
        if (timerId != InvalidTimerId) return;
        auto* app = UltraCanvasApplication::GetInstance();
        if (!app) return;
        lastTick = std::chrono::steady_clock::now();
        timerId = app->StartTimer(std::max(1u, style.frameIntervalMs), true,
                                  [this](TimerId) { Advance(); });
    }

    void UltraCanvasTickerBoard::StopTimer() {
        if (timerId == InvalidTimerId) return;
        if (auto* app = UltraCanvasApplication::GetInstance()) app->StopTimer(timerId);
        timerId = InvalidTimerId;
    }

    void UltraCanvasTickerBoard::Start() {
        running = true;
        EnsureTimer();
        RequestRedraw();
    }

    void UltraCanvasTickerBoard::Stop() {
        running = false;
        StopTimer();
        RequestRedraw();
    }

    void UltraCanvasTickerBoard::SetOffset(double pixels) {
        offset = stripWidth > 0.0 ? Wrap(pixels, stripWidth) : pixels;
        RequestRedraw();
    }

    void UltraCanvasTickerBoard::Advance() {
        // Distance from elapsed time, not a per-tick step, so a late timer
        // never slows the crawl down.
        const auto now = std::chrono::steady_clock::now();
        const double seconds = std::chrono::duration<double>(now - lastTick).count();
        lastTick = now;
        if (!running) return;
        if (style.pauseOnHover && IsHovered()) return;
        if (stripWidth <= 0.0) return;

        const double sign = style.direction == TickerDirection::Leftward ? 1.0 : -1.0;
        const double before = offset;
        offset = Wrap(offset + sign * seconds * std::max(0.0f, style.pixelsPerSecond), stripWidth);
        if (style.face == TickerBoardFace::DotMatrix && dotPitchUsed > 0.0) {
            // The grid only changes when the strip crosses a column boundary.
            if (std::floor(before / dotPitchUsed) == std::floor(offset / dotPitchUsed)) return;
        }
        RequestRedraw();
    }

// ===== STYLE =====

    void UltraCanvasTickerBoard::SetStyle(const TickerBoardStyle& s) {
        const bool intervalChanged = s.frameIntervalMs != style.frameIntervalMs;
        style = s;
        if (intervalChanged && timerId != InvalidTimerId) {
            StopTimer();
            EnsureTimer();
        }
        MarkLayoutDirty();
    }

    void UltraCanvasTickerBoard::SetFace(TickerBoardFace face) {
        if (style.face == face) return;
        style.face = face;
        // The strip is measured in different units per face; start it over.
        offset = 0.0;
        MarkLayoutDirty();
    }

    void UltraCanvasTickerBoard::SetDirection(TickerDirection direction) {
        style.direction = direction;
        RequestRedraw();
    }

    void UltraCanvasTickerBoard::SetSpeed(float pixelsPerSecond) {
        style.pixelsPerSecond = std::max(0.0f, pixelsPerSecond);
    }

    Color UltraCanvasTickerBoard::TrendColor(TickerTrend trend) const {
        switch (trend) {
            case TickerTrend::Up:   return style.upColor;
            case TickerTrend::Down: return style.downColor;
            case TickerTrend::Flat: break;
        }
        return style.flatColor;
    }

    FontStyle UltraCanvasTickerBoard::ResolveFont(float height) const {
        FontStyle font;
        font.fontFamily = style.fontFamily;
        font.fontSize = style.fontSize > 0.0f ? style.fontSize
                                              : std::max(8.0f, std::round(height * 0.5f));
        font.fontWeight = style.valueWeight;
        return font;
    }

    double UltraCanvasTickerBoard::ResolvePitch(float height) const {
        if (style.dotPitch > 0.0f) return style.dotPitch;
        // Seven rows and half a row of margin above and below.
        return std::max(2.0, static_cast<double>(height) / (kDotRows + 1));
    }

// ===== LAYOUT: TEXT FACE =====

    void UltraCanvasTickerBoard::BuildTextLayout(IRenderContext* ctx, float height) {
        runs.clear();
        columns.clear();
        fontUsed = ResolveFont(height);
        heightUsed = height;
        double x = 0.0;

        auto measure = [&](const std::string& text, FontWeight weight) {
            FontStyle f = fontUsed;
            f.fontWeight = weight;
            ctx->SetFontStyle(f);
            return static_cast<double>(ctx->GetTextLineWidth(text));
        };
        auto add = [&](const std::string& text, const Color& color, FontWeight weight,
                       size_t item, double gapAfter) {
            if (text.empty()) return;
            TextRun run;
            run.text = text;
            run.color = color;
            run.weight = weight;
            run.x = x;
            run.width = measure(text, weight);
            run.item = item;
            runs.push_back(run);
            x += run.width + gapAfter;
        };

        for (size_t i = 0; i < items.size(); ++i) {
            const TickerItem& item = items[i];
            const double xItemStart = x;
            const Color labelColor = IsTransparent(item.labelColor) ? style.labelColor : item.labelColor;
            const Color valueColor = IsTransparent(item.valueColor) ? style.valueColor : item.valueColor;
            add(item.label, labelColor, style.labelWeight, i, style.fieldGap);
            add(item.value, valueColor, style.valueWeight, i, style.fieldGap);
            if (!item.change.empty()) {
                std::string change = item.change;
                if (style.showArrows && item.trend == TickerTrend::Up)
                    change = std::string(kUpArrowUtf8) + " " + change;
                else if (style.showArrows && item.trend == TickerTrend::Down)
                    change = std::string(kDownArrowUtf8) + " " + change;
                add(change, TrendColor(item.trend), style.valueWeight, i, style.fieldGap);
            }
            if (x > xItemStart) x -= style.fieldGap;   // no gap after the last field
            x += style.itemGap;
            add(style.separator, style.separatorColor, style.valueWeight, SIZE_MAX, style.itemGap);
        }
        stripWidth = std::max(x, 1.0);
        layoutDirty = false;
    }

// ===== LAYOUT: DOT MATRIX FACE =====

    void UltraCanvasTickerBoard::AppendBlankColumns(int count) {
        for (int i = 0; i < count; ++i) columns.push_back(DotColumn{});
    }

    void UltraCanvasTickerBoard::AppendGlyphColumns(const std::string& utf8, const Color& color,
                                                    size_t item) {
        size_t i = 0;
        bool first = true;
        while (i < utf8.size()) {
            const uint32_t cp = NextCodePoint(utf8, i);
            if (!first) AppendBlankColumns(std::max(0, style.glyphGapColumns));
            first = false;
            if (cp == ' ' || cp == 0xA0) {
                // A word space is three columns, not a glyph's five: a sign
                // reads tighter than a typewriter.
                AppendBlankColumns(3);
                continue;
            }
            const uint8_t* glyph = GlyphFor(cp);
            // Proportional: drop the glyph's own blank edge columns ('i', '!',
            // '.') so the text does not gap around narrow letters.
            int from = 0, to = kGlyphColumns - 1;
            while (from < kGlyphColumns && glyph[from] == 0) ++from;
            while (to >= from && glyph[to] == 0) --to;
            if (from > to) { from = 0; to = kGlyphColumns - 1; }
            for (int c = from; c <= to; ++c) {
                DotColumn col;
                col.bits = glyph[c];
                col.color = color;
                col.item = item;
                columns.push_back(col);
            }
        }
    }

    void UltraCanvasTickerBoard::BuildDotLayout(float height) {
        runs.clear();
        columns.clear();
        heightUsed = height;
        dotPitchUsed = ResolvePitch(height);

        for (size_t i = 0; i < items.size(); ++i) {
            const TickerItem& item = items[i];
            const Color labelColor = IsTransparent(item.labelColor) ? style.labelColor : item.labelColor;
            const Color valueColor = IsTransparent(item.valueColor) ? style.valueColor : item.valueColor;
            bool any = false;
            auto field = [&](const std::string& text, const Color& color) {
                if (text.empty()) return;
                if (any) AppendBlankColumns(std::max(0, style.fieldGapColumns));
                AppendGlyphColumns(text, color, i);
                any = true;
            };
            field(item.label, labelColor);
            field(item.value, valueColor);
            if (!item.change.empty()) {
                std::string change = item.change;
                if (style.showArrows && item.trend == TickerTrend::Up)
                    change = std::string(kUpArrowUtf8) + " " + change;
                else if (style.showArrows && item.trend == TickerTrend::Down)
                    change = std::string(kDownArrowUtf8) + " " + change;
                field(change, TrendColor(item.trend));
            }
            AppendBlankColumns(std::max(0, style.itemGapColumns));
            if (!style.separator.empty()) {
                AppendGlyphColumns(style.separator, style.separatorColor, SIZE_MAX);
                AppendBlankColumns(std::max(0, style.itemGapColumns));
            }
        }
        if (columns.empty()) AppendBlankColumns(1);
        stripWidth = static_cast<double>(columns.size()) * dotPitchUsed;
        layoutDirty = false;
    }

// ===== RENDERING =====

    void UltraCanvasTickerBoard::Render(IRenderContext* ctx, const Rect2Df& /*dirtyRect*/) {
        if (!ctx) return;
        const float w = GetWidth(), h = GetHeight();
        if (w <= 1.0f || h <= 1.0f) return;

        if (running) EnsureTimer();

        ctx->PushState();
        ctx->SetFillPaint(style.backgroundColor);
        if (style.cornerRadius > 0.0f) {
            ctx->FillRoundedRectangle(Rect2Dd(0, 0, w, h), style.cornerRadius);
        } else {
            ctx->FillRectangle(Rect2Dd(0, 0, w, h));
        }
        ctx->ClipRect(Rect2Dd(0, 0, w, h));

        if (style.face == TickerBoardFace::Text) {
            if (layoutDirty || heightUsed != h) BuildTextLayout(ctx, h);
            RenderText(ctx, w, h);
        } else {
            if (layoutDirty || heightUsed != h) BuildDotLayout(h);
            RenderDotMatrix(ctx, w, h);
        }
        ctx->PopState();
    }

    void UltraCanvasTickerBoard::RenderText(IRenderContext* ctx, float w, float h) {
        if (runs.empty() || stripWidth <= 0.0) return;
        // Screen x of the strip's origin, in (-stripWidth, 0]; copies follow
        // to the right until the band is full.
        double origin = -Wrap(offset, stripWidth);
        if (origin > 0.0) origin -= stripWidth;

        const int textY = ctx->TextTopCentredOnCaps(Rect2Dd(0, 0, w, h), fontUsed);
        ctx->SetTextAlignment(TextAlignment::Left);
        FontWeight current = fontUsed.fontWeight;
        FontStyle font = fontUsed;
        ctx->SetFontStyle(font);

        for (double copy = origin; copy < w; copy += stripWidth) {
            for (const TextRun& run : runs) {
                const double x = copy + run.x;
                if (x + run.width < 0.0 || x > w) continue;
                if (run.weight != current) {
                    current = run.weight;
                    font.fontWeight = current;
                    ctx->SetFontStyle(font);
                }
                ctx->SetTextPaint(run.color);
                ctx->DrawText(run.text, Point2Dd(x, textY));
            }
        }
    }

    void UltraCanvasTickerBoard::RenderDotMatrix(IRenderContext* ctx, float w, float h) {
        if (columns.empty() || dotPitchUsed <= 0.0) return;
        const double pitch = dotPitchUsed;
        const double radius = pitch * std::clamp(style.dotFill, 0.3f, 1.0f) / 2.0;
        const int gridColumns = static_cast<int>(std::ceil(w / pitch));
        const double top = (h - kDotRows * pitch) / 2.0 + pitch / 2.0;   // centre of row 0
        const long long stripColumns = static_cast<long long>(columns.size());
        // The strip column shown in grid column 0.
        const long long first = static_cast<long long>(std::floor(Wrap(offset, stripWidth) / pitch));

        if (style.showOffDots && style.offDotColor.a > 0) {
            ctx->SetFillPaint(style.offDotColor);
            for (int g = 0; g < gridColumns; ++g) {
                const double cx = pitch / 2.0 + g * pitch;
                for (int r = 0; r < kDotRows; ++r) {
                    ctx->FillCircle(Point2Dd(cx, top + r * pitch), radius);
                }
            }
        }

        for (int g = 0; g < gridColumns; ++g) {
            const long long index = ((first + g) % stripColumns + stripColumns) % stripColumns;
            const DotColumn& col = columns[static_cast<size_t>(index)];
            if (col.bits == 0) continue;
            ctx->SetFillPaint(col.color);
            const double cx = pitch / 2.0 + g * pitch;
            for (int r = 0; r < kDotRows; ++r) {
                if (col.bits & (1u << r)) ctx->FillCircle(Point2Dd(cx, top + r * pitch), radius);
            }
        }
    }

// ===== HIT TESTING AND EVENTS =====

    size_t UltraCanvasTickerBoard::GetItemAt(int localX) const {
        if (stripWidth <= 0.0 || localX < 0 || localX >= static_cast<int>(GetWidth())) return SIZE_MAX;
        if (style.face == TickerBoardFace::Text) {
            const double stripX = Wrap(offset + localX, stripWidth);
            for (const TextRun& run : runs) {
                if (stripX >= run.x && stripX < run.x + run.width) return run.item;
            }
            return SIZE_MAX;
        }
        if (columns.empty() || dotPitchUsed <= 0.0) return SIZE_MAX;
        const long long stripColumns = static_cast<long long>(columns.size());
        const long long first = static_cast<long long>(std::floor(Wrap(offset, stripWidth) / dotPitchUsed));
        const long long g = static_cast<long long>(localX / dotPitchUsed);
        const long long index = ((first + g) % stripColumns + stripColumns) % stripColumns;
        return columns[static_cast<size_t>(index)].item;
    }

    void UltraCanvasTickerBoard::SetHoveredItem(size_t item, const Point2Di& windowPos) {
        if (item != hoveredItem) {
            hoveredItem = item;
            if (onItemHover) onItemHover(item);
        }
        const bool hasTooltip = item != SIZE_MAX && item < items.size() && !items[item].tooltip.empty();
        if (hasTooltip) {
            UltraCanvasTooltipManager::UpdateAndShowTooltip(window, items[item].tooltip, windowPos);
            tooltipShown = true;
        } else if (tooltipShown) {
            UltraCanvasTooltipManager::HideTooltip();
            tooltipShown = false;
        }
    }

    bool UltraCanvasTickerBoard::OnEvent(const UCEvent& event) {
        if (!IsVisible() || IsDisabled()) return false;
        switch (event.type) {
            case UCEventType::MouseEnter:
                SetHovered(true);
                // Holding still restarts the clock, so the pause is not
                // caught up in one leap when the pointer leaves.
                lastTick = std::chrono::steady_clock::now();
                return true;
            case UCEventType::MouseLeave:
                SetHovered(false);
                SetHoveredItem(SIZE_MAX, event.pointerWindow);
                lastTick = std::chrono::steady_clock::now();
                return true;
            case UCEventType::MouseMove:
                SetHoveredItem(GetItemAt(event.pointer.x), event.pointerWindow);
                return hoveredItem != SIZE_MAX;
            case UCEventType::MouseUp:
                if (event.button == UCMouseButton::Left && onItemClick) {
                    const size_t item = GetItemAt(event.pointer.x);
                    if (item != SIZE_MAX) {
                        onItemClick(item);
                        return true;
                    }
                }
                return false;
            default:
                break;
        }
        return UltraCanvasUIElement::OnEvent(event);
    }

} // namespace UltraCanvas
