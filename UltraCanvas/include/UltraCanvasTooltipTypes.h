// include/UltraCanvasTooltipTypes.h
// Tooltip style and structured tooltip content types.
// Shared by UltraCanvasTooltipManager (rendering) and UltraCanvasUIElement
// (per-element tooltip storage) — keep this header lightweight.
// Version: 1.1.0
// Last Modified: 2026-08-07
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include <array>
#include <optional>
#include <string>
#include <vector>

namespace UltraCanvas {

// ===== TABLE COLUMN ALIGNMENT =====
// Horizontal placement of a row cell inside its column box. Two- and
// three-column rows are measured as two independent tables, so each has its
// own alignment vector (see TooltipStyle::columnAlign2 / columnAlign3).
    enum class TooltipColumnAlign {
        Left,
        Center,
        Right,
        // Numbers line up on their decimal separator instead of their edge.
        // The anchor is the last '.' or ',' followed by a digit, so both
        // "1,204.50" and "1.204,50" align correctly; a cell without a
        // separator anchors at its end, so integers meet the separator
        // column. The whole column's anchor is the widest integer part
        // found in it, and the column reserves room for the widest
        // fractional part, so no cell has to be clipped to stay aligned.
        Decimal
    };

// ===== TOOLTIP CONFIGURATION =====
    struct TooltipStyle {
        // Appearance (defaults follow the modern dark tooltip look of
        // current desktop environments; see Light() for a light variant)
        Color backgroundColor = Color(45, 45, 48, 245);     // Dark neutral, slightly translucent
        Color borderColor = Color(255, 255, 255, 36);       // Subtle light hairline
        Color textColor = Color(242, 242, 242, 255);
        Color secondaryTextColor = Color(168, 168, 174, 255); // Row labels, muted
        Color separatorColor = Color(255, 255, 255, 46);
        Color shadowColor = Color(0, 0, 0, 90);

        // Typography
        std::string fontFamily = "Sans";
        float fontSize = 11.0f;
        float titleFontSize = 12.0f;    // AddTitle() blocks (rendered bold)

        // Layout
        int paddingLeft = 10;
        int paddingRight = 10;
        int paddingTop = 7;
        int paddingBottom = 7;
        int maxWidth = 450;
        int borderWidth = 1;
        float cornerRadius = 6.0f;
        int columnGap = 14;             // gap between adjacent table columns
        int rowSpacing = 2;             // vertical gap between content blocks

        // Column alignment of label/value rows, index 0 = left-most column.
        // Two-column rows (AddRow(label, value)) and three-column rows
        // (AddRow(label, value, value2)) form separate tables, each with its
        // own alignment. The defaults reproduce the classic tooltip look:
        // labels left, numbers flush right.
        std::array<TooltipColumnAlign, 2> columnAlign2 = {
            TooltipColumnAlign::Left, TooltipColumnAlign::Right};
        std::array<TooltipColumnAlign, 3> columnAlign3 = {
            TooltipColumnAlign::Left, TooltipColumnAlign::Right, TooltipColumnAlign::Right};

        // Shadow: soft drop shadow below the tooltip. shadowBlur is the
        // spread in pixels; 0 gives the legacy hard-edged shadow.
        bool hasShadow = true;
        Point2Di shadowOffset = Point2Di(0, 3);
        int shadowBlur = 10;

        // Behavior
        unsigned int showDelay = 300;        // milliseconds to wait before showing
        unsigned int hideDelay = 200;        // milliseconds to wait before hiding
        int offsetX = 10;              // Offset from cursor
        int offsetY = 10;
        bool followCursor = false;     // Whether tooltip follows mouse movement

        TooltipStyle() = default;

        // Alignment of two-column rows, left column first
        TooltipStyle& SetColumnAlignment(TooltipColumnAlign c1, TooltipColumnAlign c2) {
            columnAlign2 = {c1, c2};
            return *this;
        }

        // Alignment of three-column rows, left column first
        TooltipStyle& SetColumnAlignment(TooltipColumnAlign c1, TooltipColumnAlign c2,
                                         TooltipColumnAlign c3) {
            columnAlign3 = {c1, c2, c3};
            return *this;
        }

        // Preset: the default dark theme, spelled out for readability
        static TooltipStyle Dark() {
            return TooltipStyle();
        }

        // Preset: light theme for apps where a dark tooltip feels too heavy
        static TooltipStyle Light() {
            TooltipStyle s;
            s.backgroundColor = Color(255, 255, 255, 250);
            s.borderColor = Color(0, 0, 0, 28);
            s.textColor = Color(36, 41, 47, 255);
            s.secondaryTextColor = Color(110, 117, 124, 255);
            s.separatorColor = Color(0, 0, 0, 30);
            s.shadowColor = Color(0, 0, 0, 60);
            return s;
        }

        bool operator==(const TooltipStyle& other) const {
            return backgroundColor == other.backgroundColor
                && borderColor == other.borderColor
                && textColor == other.textColor
                && secondaryTextColor == other.secondaryTextColor
                && separatorColor == other.separatorColor
                && shadowColor == other.shadowColor
                && fontFamily == other.fontFamily
                && fontSize == other.fontSize
                && titleFontSize == other.titleFontSize
                && paddingLeft == other.paddingLeft
                && paddingRight == other.paddingRight
                && paddingTop == other.paddingTop
                && paddingBottom == other.paddingBottom
                && maxWidth == other.maxWidth
                && borderWidth == other.borderWidth
                && cornerRadius == other.cornerRadius
                && columnGap == other.columnGap
                && rowSpacing == other.rowSpacing
                && columnAlign2 == other.columnAlign2
                && columnAlign3 == other.columnAlign3
                && hasShadow == other.hasShadow
                && shadowOffset == other.shadowOffset
                && shadowBlur == other.shadowBlur
                && showDelay == other.showDelay
                && hideDelay == other.hideDelay
                && offsetX == other.offsetX
                && offsetY == other.offsetY
                && followCursor == other.followCursor;
        }

        bool operator!=(const TooltipStyle& other) const {
            return !(*this == other);
        }
    };

// ===== STRUCTURED TOOLTIP CONTENT =====
// A tooltip is either a plain string (optionally containing Pango markup for
// inline styling) or a TooltipContent: an ordered list of blocks the manager
// lays out natively — bold title, aligned two- or three-column table rows
// with optional color swatch, bullet list items, free markup text and
// separators.
//
// Text passed to AddTitle/AddRow/AddBullet is treated as plain data and is
// markup-escaped by the renderer; only AddText interprets Pango markup.

    enum class TooltipBlockType {
        Title,      // bold, slightly larger text line
        Text,       // free text line/paragraph; Pango markup is interpreted
        Row,        // table row of two or three aligned columns
        Bullet,     // list item with a bullet glyph and hanging indent
        Separator   // thin horizontal hairline
    };

    struct TooltipBlock {
        TooltipBlockType type = TooltipBlockType::Text;
        std::string text;                       // Title/Text/Bullet content; Row column 1
        std::string value;                      // Row column 2
        std::string value2;                     // Row column 3 (three-column rows only)
        Color swatch = Color(0, 0, 0, 0);       // Row color swatch; alpha 0 = none
        int columns = 2;                        // Row column count: 2 or 3

        bool operator==(const TooltipBlock& other) const {
            return type == other.type
                && text == other.text
                && value == other.value
                && value2 == other.value2
                && swatch == other.swatch
                && columns == other.columns;
        }
        bool operator!=(const TooltipBlock& other) const {
            return !(*this == other);
        }
    };

    struct TooltipContent {
        std::vector<TooltipBlock> blocks;

        // When set, this style is used instead of the manager's default when
        // the tooltip is shown (per-element/per-chart theming).
        std::optional<TooltipStyle> styleOverride;

        // When set, these win over the style's columnAlign2 / columnAlign3, so
        // a tooltip can pick its table alignment without replacing the theme.
        std::optional<std::array<TooltipColumnAlign, 2>> columnAlign2Override;
        std::optional<std::array<TooltipColumnAlign, 3>> columnAlign3Override;

        TooltipContent& AddTitle(const std::string& text) {
            blocks.push_back({TooltipBlockType::Title, text});
            return *this;
        }

        // Pango markup is interpreted (e.g. "<b>bold</b>", "<i>italic</i>",
        // "<span foreground=\"#ff8800\">colored</span>")
        TooltipContent& AddText(const std::string& markupText) {
            blocks.push_back({TooltipBlockType::Text, markupText});
            return *this;
        }

        // Two-column row: label in column 1, value in column 2
        TooltipContent& AddRow(const std::string& label, const std::string& value) {
            return AddRowBlock(Color(0, 0, 0, 0), label, value, std::string(), 2);
        }

        TooltipContent& AddRow(const Color& swatch, const std::string& label, const std::string& value) {
            return AddRowBlock(swatch, label, value, std::string(), 2);
        }

        // Three-column row: the two- and three-column rows of a tooltip are
        // measured as separate tables, each with its own column alignment.
        TooltipContent& AddRow(const std::string& label, const std::string& value,
                               const std::string& value2) {
            return AddRowBlock(Color(0, 0, 0, 0), label, value, value2, 3);
        }

        TooltipContent& AddRow(const Color& swatch, const std::string& label,
                               const std::string& value, const std::string& value2) {
            return AddRowBlock(swatch, label, value, value2, 3);
        }

        TooltipContent& AddBullet(const std::string& text) {
            blocks.push_back({TooltipBlockType::Bullet, text});
            return *this;
        }

        TooltipContent& AddSeparator() {
            blocks.push_back({TooltipBlockType::Separator});
            return *this;
        }

        TooltipContent& SetStyle(const TooltipStyle& style) {
            styleOverride = style;
            return *this;
        }

        // Column alignment for this tooltip's two-column rows. Set
        // independently of SetStyle(), so switching theme keeps the layout.
        TooltipContent& SetColumnAlignment(TooltipColumnAlign c1, TooltipColumnAlign c2) {
            columnAlign2Override = std::array<TooltipColumnAlign, 2>{c1, c2};
            return *this;
        }

        // Column alignment for this tooltip's three-column rows
        TooltipContent& SetColumnAlignment(TooltipColumnAlign c1, TooltipColumnAlign c2,
                                           TooltipColumnAlign c3) {
            columnAlign3Override = std::array<TooltipColumnAlign, 3>{c1, c2, c3};
            return *this;
        }

        bool Empty() const { return blocks.empty(); }
        void Clear() {
            blocks.clear();
            styleOverride.reset();
            columnAlign2Override.reset();
            columnAlign3Override.reset();
        }

        bool operator==(const TooltipContent& other) const {
            return blocks == other.blocks
                && styleOverride == other.styleOverride
                && columnAlign2Override == other.columnAlign2Override
                && columnAlign3Override == other.columnAlign3Override;
        }
        bool operator!=(const TooltipContent& other) const {
            return !(*this == other);
        }

    private:
        TooltipContent& AddRowBlock(const Color& swatch, const std::string& label,
                                    const std::string& value, const std::string& value2,
                                    int columns) {
            TooltipBlock block;
            block.type = TooltipBlockType::Row;
            block.text = label;
            block.value = value;
            block.value2 = value2;
            block.swatch = swatch;
            block.columns = columns;
            blocks.push_back(std::move(block));
            return *this;
        }
    };

} // namespace UltraCanvas
