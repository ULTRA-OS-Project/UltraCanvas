// Apps/DemoApp/UltraCanvasTooltipExamples.cpp
// Tooltip system demonstration for the main demo app: plain text, inline
// Pango markup, and structured TooltipContent (title, two- and three-column
// table rows with color swatches and per-column alignment including decimal
// alignment, bullet lists, separators) plus style presets.
// Version: 1.3.0 - the shared page look (UltraCanvasDemoPageKit): each hover
//                  target a small card with its caption, cards that wrap,
//                  buttons as wide as their text
// Version: 1.2.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasTooltipManager.h"

namespace UltraCanvas {

    // Helper: a hover target - a small card in the grid with the button (as
    // wide as its text, at least the 200 px it had) and a caption under it
    // explaining what to look for.
    static std::shared_ptr<UltraCanvasButton> AddTooltipTarget(const std::shared_ptr<UltraCanvasContainer>& grid,
                                                               const std::string& id, const std::string& text,
                                                               const std::string& caption) {
        auto card = DemoKit::AddCard(grid, id + "Card", "", "", 220.0f);
        auto btn = std::make_shared<UltraCanvasButton>(id, 0, 0, 0, 40, text);
        CSSLayout::BoxConstraints limits;
        limits.minWidth = CSSLayout::Dimension::Px(200);
        btn->boxConstraints = limits;
        DemoKit::AddFixed(card, btn);
        DemoKit::AddCaption(card, id + "Cap", caption);
        return btn;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateTooltipExamples() {
        auto mainContainer = DemoKit::CreatePage("TooltipExamples", "UltraCanvas Tooltip Showcase",
                "Hover each button. Tooltips support plain text, inline Pango markup and "
                "structured content: title, two- and three-column table rows with "
                "definable per-column alignment, color swatches, bullet lists and "
                "separators.");
        // The hover targets share the width, three or four to a line, and
        // wrap under each other in a narrow window.
        auto grid = DemoKit::AddRow(mainContainer, "TooltipGrid");

        // ===== 1. PLAIN TEXT =====
        {
            auto btn = AddTooltipTarget(grid, "ttPlain", "Plain text",
                                        "SetTooltip(\"...\") — one line of text");
            btn->SetTooltip("A simple single-line tooltip.");
        }

        // ===== 2. INLINE MARKUP =====
        {
            auto btn = AddTooltipTarget(grid, "ttMarkup", "Inline markup",
                                        "Plain string with Pango markup tags");
            btn->SetTooltip("Pango markup: <b>bold</b>, <i>italic</i>, <u>underline</u> and "
                            "<span foreground=\"#7fd7ff\">colored</span> text.");
        }

        // ===== 3. STRUCTURED: COLOR SWATCHES (chart series style) =====
        {
            auto btn = AddTooltipTarget(grid, "ttSwatch", "Color swatches",
                                        "AddRow(swatchColor, label, value) — chart-series legend look");
            TooltipContent content;
            content.AddTitle("Sales 2026-08")
                   .AddRow(Color(66, 133, 244, 255), "Europe", "1 204 t")
                   .AddRow(Color(219, 68, 55, 255), "Americas", "986 t")
                   .AddRow(Color(244, 180, 0, 255), "Asia", "1 890 t")
                   .AddRow(Color(15, 157, 88, 255), "Africa", "312 t");
            btn->SetTooltipContent(content);
        }

        // ===== 4. STRUCTURED: TITLE + LABEL/VALUE TABLE =====
        {
            auto btn = AddTooltipTarget(grid, "ttTable", "Title + table",
                                        "AddRow(label, value, unit) — right, right, left aligned");
            TooltipContent content;
            content.AddTitle("Batch S10")
                   .AddRow("Temperature", "209.79", "°C")
                   .AddRow("Pressure", "2.26551", "bar")
                   .AddRow("Flow", "53.1416", "l/min")
                   .AddRow("Vibration", "0.279816", "mm/s")
                   .AddRow("Yield", "95.2212", "%")
                   .SetColumnAlignment(TooltipColumnAlign::Right,
                                       TooltipColumnAlign::Right,
                                       TooltipColumnAlign::Left);
            btn->SetTooltipContent(content);
        }

        // ===== 5. DECIMAL (COMMA) ALIGNMENT =====
        {
            auto btn = AddTooltipTarget(grid, "ttDecimal", "Decimal alignment",
                                        "TooltipColumnAlign::Decimal — numbers aligned on the separator");
            TooltipContent content;
            content.AddTitle("Batch S10")
                   .AddRow("Temperature", "209.79", "°C")
                   .AddRow("Pressure", "2.26551", "bar")
                   .AddRow("Flow", "53.1416", "l/min")
                   .AddRow("Vibration", "0.279816", "mm/s")
                   .AddRow("Yield", "95.2212", "%")
                   // Same data as "Title + table" to its left, but the numbers
                   // line up on the decimal point instead of on their right edge.
                   .SetColumnAlignment(TooltipColumnAlign::Right,
                                       TooltipColumnAlign::Decimal,
                                       TooltipColumnAlign::Left);
            btn->SetTooltipContent(content);
        }

        // ===== 6. STRUCTURED: BULLETS + SEPARATOR + FOOTER =====
        {
            auto btn = AddTooltipTarget(grid, "ttBullets", "List + separator",
                                        "AddBullet() items, AddSeparator(), AddText() footer with markup");
            TooltipContent content;
            content.AddTitle("Validation failed")
                   .AddBullet("Name must not be empty")
                   .AddBullet("Port must be between 1 and 65535, wrapping onto a second line when needed")
                   .AddBullet("Password too short")
                   .AddSeparator()
                   .AddText("<i>Fix the fields marked red.</i>");
            btn->SetTooltipContent(content);
        }

        // ===== 7. LIGHT STYLE PRESET =====
        {
            auto btn = AddTooltipTarget(grid, "ttLight", "Light preset",
                                        "content.SetStyle(TooltipStyle::Light()) per-element style override");
            TooltipContent content;
            content.AddTitle("TooltipStyle::Light()")
                   .AddRow("background", "white")
                   .AddRow("text", "dark gray")
                   .AddRow("shadow", "soft, subtle");
            content.SetStyle(TooltipStyle::Light());
            btn->SetTooltipContent(content);
        }

        // ===== 8. CUSTOM STYLE =====
        {
            auto btn = AddTooltipTarget(grid, "ttCustom", "Custom style",
                                        "Any TooltipStyle field can be overridden per element");
            TooltipContent content;
            content.AddTitle("Custom TooltipStyle")
                   .AddRow("cornerRadius", "10 px")
                   .AddRow("shadowBlur", "16 px")
                   .AddRow("accent", "deep blue");
            TooltipStyle custom;
            custom.backgroundColor = Color(24, 42, 84, 248);
            custom.borderColor = Color(120, 170, 255, 70);
            custom.secondaryTextColor = Color(160, 190, 235, 255);
            custom.separatorColor = Color(120, 170, 255, 60);
            custom.cornerRadius = 10.0f;
            custom.shadowBlur = 16;
            content.SetStyle(custom);
            btn->SetTooltipContent(content);
        }

        // ===== 9. ESCAPING: DATA IS SAFE =====
        {
            auto btn = AddTooltipTarget(grid, "ttEscape", "Safe data",
                                        "Row/title/bullet text is escaped — no markup injection from data");
            TooltipContent content;
            content.AddTitle("Labels & values are data")
                   .AddRow("expression", "a < b && b > c")
                   .AddRow("tag", "<b>not bold</b>")
                   .AddRow("path", "C:\\temp & Co");
            btn->SetTooltipContent(content);
        }

        // ===== 10. THREE-COLUMN TABLE =====
        {
            auto btn = AddTooltipTarget(grid, "ttThreeCol", "3-column table",
                                        "AddRow(label, value, value2) — three aligned columns");
            TooltipContent content;
            content.AddTitle("Regions — Q3 vs Q2")
                   .AddRow(Color(66, 133, 244, 255), "Europe", "1 204 t", "+4.1 %")
                   .AddRow(Color(219, 68, 55, 255), "Americas", "986 t", "-1.7 %")
                   .AddRow(Color(244, 180, 0, 255), "Asia", "1 890 t", "+11.3 %")
                   .AddRow(Color(15, 157, 88, 255), "Africa", "312 t", "+0.4 %");
            btn->SetTooltipContent(content);
        }

        // ===== 11. CUSTOM COLUMN ALIGNMENT =====
        {
            auto btn = AddTooltipTarget(grid, "ttColAlign", "Column alignment",
                                        "SetColumnAlignment(Left, Center, Right) per column, 2- and 3-column forms");
            TooltipContent content;
            content.AddTitle("Build matrix")
                   .AddRow("platform", "arch", "status")
                   .AddSeparator()
                   .AddRow("Linux", "x86_64", "passed")
                   .AddRow("Windows", "arm64", "passed")
                   .AddRow("macOS", "x86_64", "failed")
                   .SetColumnAlignment(TooltipColumnAlign::Left,
                                       TooltipColumnAlign::Center,
                                       TooltipColumnAlign::Right);
            btn->SetTooltipContent(content);
        }

        // ===== 12. DECIMAL WITH MIXED PRECISION AND SEPARATORS =====
        {
            auto btn = AddTooltipTarget(grid, "ttDecimalMixed", "Decimal, mixed",
                                        "Mixed precision, thousands separators and integers share one anchor");
            TooltipContent content;
            content.AddTitle("Ledger")
                   .AddRow("Opening", "1,204.50", "€")
                   .AddRow("Fees", "-17.05", "€")
                   .AddRow("Interest", "0.9", "€")
                   .AddRow("Adjustment", "12", "€")      // no separator: anchors at its end
                   .AddRow("Closing", "1,200.35", "€")
                   .SetColumnAlignment(TooltipColumnAlign::Right,
                                       TooltipColumnAlign::Decimal,
                                       TooltipColumnAlign::Left);
            btn->SetTooltipContent(content);
        }

        // ===== NOTES =====
        DemoKit::AddCaption(mainContainer, "TooltipNotes",
                "API: element->SetTooltip(text) for plain/markup strings; "
                "element->SetTooltipContent(TooltipContent) for structured tooltips. "
                "Charts and custom widgets can call UltraCanvasTooltipManager::UpdateAndShowTooltip() "
                "directly with either form. See Docs/UltraCanvas/UltraCanvasTooltipManager.md.");

        return mainContainer;
    }

} // namespace UltraCanvas
