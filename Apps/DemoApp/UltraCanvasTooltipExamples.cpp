// Apps/DemoApp/UltraCanvasTooltipExamples.cpp
// Tooltip system demonstration for the main demo app: plain text, inline
// Pango markup, and structured TooltipContent (title, label/value rows with
// color swatches, bullet lists, separators) plus style presets.
// Version: 1.0.0
// Last Modified: 2026-08-06
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasTooltipManager.h"

namespace UltraCanvas {

    // Helper: a caption under each hover target explaining what to look for.
    static std::shared_ptr<UltraCanvasLabel> TooltipCaption(const std::string& id, float x, float y,
                                                            float w, const std::string& text) {
        auto lbl = std::make_shared<UltraCanvasLabel>(id, x, y, w, 34.0f);
        lbl->SetText(text);
        lbl->SetFontSize(11);
        lbl->SetTextColor(Color(100, 100, 100, 255));
        return lbl;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateTooltipExamples() {
        auto mainContainer = std::make_shared<UltraCanvasContainer>("TooltipExamples", 0, 0, 1000, 1000);

        // ===== PAGE TITLE =====
        auto title = std::make_shared<UltraCanvasLabel>("TooltipTitle", 20, 10, 700, 35);
        title->SetText("UltraCanvas Tooltip Showcase");
        title->SetFontSize(18);
        title->SetFontWeight(FontWeight::Bold);
        title->SetTextColor(Color(50, 50, 150, 255));
        mainContainer->AddChild(title);

        auto subtitle = std::make_shared<UltraCanvasLabel>("TooltipSubtitle", 20, 45, 920, 25);
        subtitle->SetText("Hover each button. Tooltips support plain text, inline Pango markup and "
                          "structured content: title, label/value rows, color swatches, bullet lists "
                          "and separators.");
        subtitle->SetFontSize(12);
        subtitle->SetTextColor(Color(100, 100, 100, 255));
        mainContainer->AddChild(subtitle);

        const float btnW = 200.0f;
        const float btnH = 40.0f;
        const float colX[3] = {30.0f, 280.0f, 530.0f};
        float rowY = 90.0f;

        // ===== 1. PLAIN TEXT =====
        {
            auto btn = CreateButton("ttPlain", colX[0], rowY, btnW, btnH, "Plain text");
            btn->SetTooltip("A simple single-line tooltip.");
            mainContainer->AddChild(btn);
            mainContainer->AddChild(TooltipCaption("ttPlainCap", colX[0], rowY + btnH + 4, btnW,
                                                   "SetTooltip(\"...\") — one line of text"));
        }

        // ===== 2. INLINE MARKUP =====
        {
            auto btn = CreateButton("ttMarkup", colX[1], rowY, btnW, btnH, "Inline markup");
            btn->SetTooltip("Pango markup: <b>bold</b>, <i>italic</i>, <u>underline</u> and "
                            "<span foreground=\"#7fd7ff\">colored</span> text.");
            mainContainer->AddChild(btn);
            mainContainer->AddChild(TooltipCaption("ttMarkupCap", colX[1], rowY + btnH + 4, btnW,
                                                   "Plain string with Pango markup tags"));
        }

        // ===== 3. MULTI-LINE / WRAPPED =====
        {
            auto btn = CreateButton("ttWrap", colX[2], rowY, btnW, btnH, "Long text");
            btn->SetTooltip("Long tooltip text wraps automatically at the style's maxWidth. "
                            "Explicit line breaks work too:\nline two\nline three.");
            mainContainer->AddChild(btn);
            mainContainer->AddChild(TooltipCaption("ttWrapCap", colX[2], rowY + btnH + 4, btnW,
                                                   "Word wrapping and explicit \\n breaks"));
        }
        rowY += btnH + 55.0f;

        // ===== 4. STRUCTURED: TITLE + LABEL/VALUE TABLE =====
        {
            auto btn = CreateButton("ttTable", colX[0], rowY, btnW, btnH, "Title + table");
            TooltipContent content;
            content.AddTitle("Batch S10")
                   .AddRow("temperature", "209.79 °C")
                   .AddRow("pressure", "2.26551 bar")
                   .AddRow("flow", "53.1416 l/min")
                   .AddRow("vibration", "0.279816 mm/s")
                   .AddRow("yield", "95.2212 %");
            btn->SetTooltipContent(content);
            mainContainer->AddChild(btn);
            mainContainer->AddChild(TooltipCaption("ttTableCap", colX[0], rowY + btnH + 4, btnW,
                                                   "AddTitle() + AddRow(label, value) —\naligned columns, right-aligned values"));
        }

        // ===== 5. STRUCTURED: COLOR SWATCHES (chart series style) =====
        {
            auto btn = CreateButton("ttSwatch", colX[1], rowY, btnW, btnH, "Color swatches");
            TooltipContent content;
            content.AddTitle("Sales 2026-08")
                   .AddRow(Color(66, 133, 244, 255), "Europe", "1 204 t")
                   .AddRow(Color(219, 68, 55, 255), "Americas", "986 t")
                   .AddRow(Color(244, 180, 0, 255), "Asia", "1 890 t")
                   .AddRow(Color(15, 157, 88, 255), "Africa", "312 t");
            btn->SetTooltipContent(content);
            mainContainer->AddChild(btn);
            mainContainer->AddChild(TooltipCaption("ttSwatchCap", colX[1], rowY + btnH + 4, btnW,
                                                   "AddRow(swatchColor, label, value) —\nchart-series legend look"));
        }

        // ===== 6. STRUCTURED: BULLETS + SEPARATOR + FOOTER =====
        {
            auto btn = CreateButton("ttBullets", colX[2], rowY, btnW, btnH, "List + separator");
            TooltipContent content;
            content.AddTitle("Validation failed")
                   .AddBullet("Name must not be empty")
                   .AddBullet("Port must be between 1 and 65535, wrapping onto a second line when needed")
                   .AddBullet("Password too short")
                   .AddSeparator()
                   .AddText("<i>Fix the fields marked red.</i>");
            btn->SetTooltipContent(content);
            mainContainer->AddChild(btn);
            mainContainer->AddChild(TooltipCaption("ttBulletsCap", colX[2], rowY + btnH + 4, btnW,
                                                   "AddBullet() items, AddSeparator(),\nAddText() footer with markup"));
        }
        rowY += btnH + 55.0f;

        // ===== 7. LIGHT STYLE PRESET =====
        {
            auto btn = CreateButton("ttLight", colX[0], rowY, btnW, btnH, "Light preset");
            TooltipContent content;
            content.AddTitle("TooltipStyle::Light()")
                   .AddRow("background", "white")
                   .AddRow("text", "dark gray")
                   .AddRow("shadow", "soft, subtle");
            content.SetStyle(TooltipStyle::Light());
            btn->SetTooltipContent(content);
            mainContainer->AddChild(btn);
            mainContainer->AddChild(TooltipCaption("ttLightCap", colX[0], rowY + btnH + 4, btnW,
                                                   "content.SetStyle(TooltipStyle::Light())\nper-element style override"));
        }

        // ===== 8. CUSTOM STYLE =====
        {
            auto btn = CreateButton("ttCustom", colX[1], rowY, btnW, btnH, "Custom style");
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
            mainContainer->AddChild(btn);
            mainContainer->AddChild(TooltipCaption("ttCustomCap", colX[1], rowY + btnH + 4, btnW,
                                                   "Any TooltipStyle field can be\noverridden per element"));
        }

        // ===== 9. ESCAPING: DATA IS SAFE =====
        {
            auto btn = CreateButton("ttEscape", colX[2], rowY, btnW, btnH, "Safe data");
            TooltipContent content;
            content.AddTitle("Labels & values are data")
                   .AddRow("expression", "a < b && b > c")
                   .AddRow("tag", "<b>not bold</b>")
                   .AddRow("path", "C:\\temp & Co");
            btn->SetTooltipContent(content);
            mainContainer->AddChild(btn);
            mainContainer->AddChild(TooltipCaption("ttEscapeCap", colX[2], rowY + btnH + 4, btnW,
                                                   "Row/title/bullet text is escaped —\nno markup injection from data"));
        }
        rowY += btnH + 55.0f;

        // ===== NOTES =====
        auto notes = std::make_shared<UltraCanvasLabel>("TooltipNotes", 20, rowY + 10, 920, 60);
        notes->SetText("API: element->SetTooltip(text) for plain/markup strings; "
                       "element->SetTooltipContent(TooltipContent) for structured tooltips. "
                       "Charts and custom widgets can call UltraCanvasTooltipManager::UpdateAndShowTooltip() "
                       "directly with either form. See Docs/UltraCanvas/UltraCanvasTooltipManager.md.");
        notes->SetFontSize(11);
        notes->SetTextColor(Color(120, 120, 120, 255));
        mainContainer->AddChild(notes);

        return mainContainer;
    }

} // namespace UltraCanvas
