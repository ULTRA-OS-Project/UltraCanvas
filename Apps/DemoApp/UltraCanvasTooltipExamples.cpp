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
        auto mainContainer = DemoKit::CreatePage("TooltipExamples", Tr("UltraCanvas Tooltip Showcase"),
                Tr("Hover each button. Tooltips support plain text, inline Pango markup and "
                "structured content: title, two- and three-column table rows with "
                "definable per-column alignment, color swatches, bullet lists and "
                "separators."));
        // The hover targets share the width, three or four to a line, and
        // wrap under each other in a narrow window.
        auto grid = DemoKit::AddRow(mainContainer, "TooltipGrid");

        // ===== 1. PLAIN TEXT =====
        {
            auto btn = AddTooltipTarget(grid, "ttPlain", Tr("Plain text"),
                                        Tr("SetTooltip(\"...\") — one line of text"));
            btn->SetTooltip(Tr("A simple single-line tooltip."));
        }

        // ===== 2. INLINE MARKUP =====
        {
            auto btn = AddTooltipTarget(grid, "ttMarkup", Tr("Inline markup"),
                                        Tr("Plain string with Pango markup tags"));
            btn->SetTooltip(Tr("Pango markup: <b>bold</b>, <i>italic</i>, <u>underline</u> and "
                            "<span foreground=\"#7fd7ff\">colored</span> text."));
        }

        // ===== 3. STRUCTURED: COLOR SWATCHES (chart series style) =====
        {
            auto btn = AddTooltipTarget(grid, "ttSwatch", Tr("Color swatches"),
                                        Tr("AddRow(swatchColor, label, value) — chart-series legend look"));
            TooltipContent content;
            content.AddTitle(Tr("Sales 2026-08"))
                   .AddRow(Color(66, 133, 244, 255), Tr("Europe"), "1 204 t")
                   .AddRow(Color(219, 68, 55, 255), Tr("Americas"), "986 t")
                   .AddRow(Color(244, 180, 0, 255), Tr("Asia"), "1 890 t")
                   .AddRow(Color(15, 157, 88, 255), Tr("Africa"), "312 t");
            btn->SetTooltipContent(content);
        }

        // ===== 4. STRUCTURED: TITLE + LABEL/VALUE TABLE =====
        {
            auto btn = AddTooltipTarget(grid, "ttTable", Tr("Title + table"),
                                        Tr("AddRow(label, value, unit) — right, right, left aligned"));
            TooltipContent content;
            content.AddTitle(Tr("Batch S10"))
                   .AddRow(Tr("Temperature"), "209.79", "°C")
                   .AddRow(Tr("Pressure"), "2.26551", "bar")
                   .AddRow(Tr("Flow"), "53.1416", "l/min")
                   .AddRow(Tr("Vibration"), "0.279816", "mm/s")
                   .AddRow(Tr("Yield"), "95.2212", "%")
                   .SetColumnAlignment(TooltipColumnAlign::Right,
                                       TooltipColumnAlign::Right,
                                       TooltipColumnAlign::Left);
            btn->SetTooltipContent(content);
        }

        // ===== 5. DECIMAL (COMMA) ALIGNMENT =====
        {
            auto btn = AddTooltipTarget(grid, "ttDecimal", Tr("Decimal alignment"),
                                        Tr("TooltipColumnAlign::Decimal — numbers aligned on the separator"));
            TooltipContent content;
            content.AddTitle(Tr("Batch S10"))
                   .AddRow(Tr("Temperature"), "209.79", "°C")
                   .AddRow(Tr("Pressure"), "2.26551", "bar")
                   .AddRow(Tr("Flow"), "53.1416", "l/min")
                   .AddRow(Tr("Vibration"), "0.279816", "mm/s")
                   .AddRow(Tr("Yield"), "95.2212", "%")
                   // Same data as "Title + table" to its left, but the numbers
                   // line up on the decimal point instead of on their right edge.
                   .SetColumnAlignment(TooltipColumnAlign::Right,
                                       TooltipColumnAlign::Decimal,
                                       TooltipColumnAlign::Left);
            btn->SetTooltipContent(content);
        }

        // ===== 6. STRUCTURED: BULLETS + SEPARATOR + FOOTER =====
        {
            auto btn = AddTooltipTarget(grid, "ttBullets", Tr("List + separator"),
                                        Tr("AddBullet() items, AddSeparator(), AddText() footer with markup"));
            TooltipContent content;
            content.AddTitle(Tr("Validation failed"))
                   .AddBullet(Tr("Name must not be empty"))
                   .AddBullet(Tr("Port must be between 1 and 65535, wrapping onto a second line when needed"))
                   .AddBullet(Tr("Password too short"))
                   .AddSeparator()
                   .AddText("<i>" + Tr("Fix the fields marked red.") + "</i>");
            btn->SetTooltipContent(content);
        }

        // ===== 7. LIGHT STYLE PRESET =====
        {
            auto btn = AddTooltipTarget(grid, "ttLight", Tr("Light preset"),
                                        Tr("content.SetStyle(TooltipStyle::Light()) per-element style override"));
            TooltipContent content;
            content.AddTitle("TooltipStyle::Light()")
                   .AddRow(Tr("background"), Tr("white"))
                   .AddRow(Tr("text"), Tr("dark gray"))
                   .AddRow(Tr("shadow"), Tr("soft, subtle"));
            content.SetStyle(TooltipStyle::Light());
            btn->SetTooltipContent(content);
        }

        // ===== 8. CUSTOM STYLE =====
        {
            auto btn = AddTooltipTarget(grid, "ttCustom", Tr("Custom style"),
                                        Tr("Any TooltipStyle field can be overridden per element"));
            TooltipContent content;
            content.AddTitle(Tr("Custom TooltipStyle"))
                   .AddRow("cornerRadius", "10 px")
                   .AddRow("shadowBlur", "16 px")
                   .AddRow(Tr("accent"), Tr("deep blue"));
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
            auto btn = AddTooltipTarget(grid, "ttEscape", Tr("Safe data"),
                                        Tr("Row/title/bullet text is escaped — no markup injection from data"));
            TooltipContent content;
            content.AddTitle(Tr("Labels & values are data"))
                   .AddRow(Tr("expression"), "a < b && b > c")
                   .AddRow(Tr("tag"), "<b>not bold</b>")
                   .AddRow(Tr("path"), "C:\\temp & Co");
            btn->SetTooltipContent(content);
        }

        // ===== 10. THREE-COLUMN TABLE =====
        {
            auto btn = AddTooltipTarget(grid, "ttThreeCol", Tr("3-column table"),
                                        Tr("AddRow(label, value, value2) — three aligned columns"));
            TooltipContent content;
            content.AddTitle(Tr("Regions — Q3 vs Q2"))
                   .AddRow(Color(66, 133, 244, 255), Tr("Europe"), "1 204 t", "+4.1 %")
                   .AddRow(Color(219, 68, 55, 255), Tr("Americas"), "986 t", "-1.7 %")
                   .AddRow(Color(244, 180, 0, 255), Tr("Asia"), "1 890 t", "+11.3 %")
                   .AddRow(Color(15, 157, 88, 255), Tr("Africa"), "312 t", "+0.4 %");
            btn->SetTooltipContent(content);
        }

        // ===== 11. CUSTOM COLUMN ALIGNMENT =====
        {
            auto btn = AddTooltipTarget(grid, "ttColAlign", Tr("Column alignment"),
                                        Tr("SetColumnAlignment(Left, Center, Right) per column, 2- and 3-column forms"));
            TooltipContent content;
            content.AddTitle(Tr("Build matrix"))
                   .AddRow(Tr("platform"), Tr("arch"), Tr("status"))
                   .AddSeparator()
                   .AddRow("Linux", "x86_64", Tr("passed"))
                   .AddRow("Windows", "arm64", Tr("passed"))
                   .AddRow("macOS", "x86_64", Tr("failed"))
                   .SetColumnAlignment(TooltipColumnAlign::Left,
                                       TooltipColumnAlign::Center,
                                       TooltipColumnAlign::Right);
            btn->SetTooltipContent(content);
        }

        // ===== 12. DECIMAL WITH MIXED PRECISION AND SEPARATORS =====
        {
            auto btn = AddTooltipTarget(grid, "ttDecimalMixed", Tr("Decimal, mixed"),
                                        Tr("Mixed precision, thousands separators and integers share one anchor"));
            TooltipContent content;
            content.AddTitle(Tr("Ledger"))
                   .AddRow(Tr("Opening"), "1,204.50", "€")
                   .AddRow(Tr("Fees"), "-17.05", "€")
                   .AddRow(Tr("Interest"), "0.9", "€")
                   .AddRow(Tr("Adjustment"), "12", "€")      // no separator: anchors at its end
                   .AddRow(Tr("Closing"), "1,200.35", "€")
                   .SetColumnAlignment(TooltipColumnAlign::Right,
                                       TooltipColumnAlign::Decimal,
                                       TooltipColumnAlign::Left);
            btn->SetTooltipContent(content);
        }

        // ===== NOTES =====
        DemoKit::AddCaption(mainContainer, "TooltipNotes",
                Tr("API: element->SetTooltip(text) for plain/markup strings; "
                "element->SetTooltipContent(TooltipContent) for structured tooltips. "
                "Charts and custom widgets can call UltraCanvasTooltipManager::UpdateAndShowTooltip() "
                "directly with either form. See Docs/UltraCanvas/UltraCanvasTooltipManager.md."));

        return mainContainer;
    }

} // namespace UltraCanvas
