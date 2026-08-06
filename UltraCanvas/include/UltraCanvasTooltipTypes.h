// include/UltraCanvasTooltipTypes.h
// Tooltip style and structured tooltip content types.
// Shared by UltraCanvasTooltipManager (rendering) and UltraCanvasUIElement
// (per-element tooltip storage) — keep this header lightweight.
// Version: 1.0.0
// Last Modified: 2026-08-06
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include <optional>
#include <string>
#include <vector>

namespace UltraCanvas {

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
        int columnGap = 14;             // gap between label and value columns
        int rowSpacing = 2;             // vertical gap between content blocks

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
// lays out natively — bold title, aligned label/value rows with optional
// color swatch, bullet list items, free markup text and separators.
//
// Text passed to AddTitle/AddRow/AddBullet is treated as plain data and is
// markup-escaped by the renderer; only AddText interprets Pango markup.

    enum class TooltipBlockType {
        Title,      // bold, slightly larger text line
        Text,       // free text line/paragraph; Pango markup is interpreted
        Row,        // two-column table row: muted label left, value right-aligned
        Bullet,     // list item with a bullet glyph and hanging indent
        Separator   // thin horizontal hairline
    };

    struct TooltipBlock {
        TooltipBlockType type = TooltipBlockType::Text;
        std::string text;                       // Title/Text/Bullet content; Row label
        std::string value;                      // Row value
        Color swatch = Color(0, 0, 0, 0);       // Row color swatch; alpha 0 = none

        bool operator==(const TooltipBlock& other) const {
            return type == other.type
                && text == other.text
                && value == other.value
                && swatch == other.swatch;
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

        TooltipContent& AddRow(const std::string& label, const std::string& value) {
            blocks.push_back({TooltipBlockType::Row, label, value});
            return *this;
        }

        TooltipContent& AddRow(const Color& swatch, const std::string& label, const std::string& value) {
            blocks.push_back({TooltipBlockType::Row, label, value, swatch});
            return *this;
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

        bool Empty() const { return blocks.empty(); }
        void Clear() { blocks.clear(); styleOverride.reset(); }

        bool operator==(const TooltipContent& other) const {
            return blocks == other.blocks && styleOverride == other.styleOverride;
        }
        bool operator!=(const TooltipContent& other) const {
            return !(*this == other);
        }
    };

} // namespace UltraCanvas
