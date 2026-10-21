// Apps/DemoApp/UltraCanvasColorPaletteManagerExamples.cpp
// Demonstration of the colour palette manager: palette rows under a colour
// picker, the gallery of ready-made schemes behind "+", "-" on every row
// once there are several, and the right-click editor for one entry.
// Version: 1.0.0
// Last Modified: 2026-10-19
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasColorPaletteManager.h"
#include "UltraCanvasColorPicker.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"

#include <cstdio>

namespace UltraCanvas {

    namespace {
        // A preset by its name, as the gallery offers it.
        NamedColorPalette Preset(const std::string& name) {
            for (const auto& p : UltraCanvasColorPaletteManager::PresetPalettes()) {
                if (p.name == name) return p;
            }
            return UltraCanvasColorPaletteManager::DefaultPalette();
        }

        std::string Hex(const Color& c) {
            char buf[8];
            snprintf(buf, sizeof(buf), "#%02X%02X%02X", c.r, c.g, c.b);
            return buf;
        }

        std::shared_ptr<UltraCanvasLabel> Caption(const std::string& id, float x, float y,
                                                  float w, const std::string& text) {
            auto label = CreateLabel(id, x, y, w, 20);
            label->SetText(text);
            label->SetFontSize(12);
            label->SetFontWeight(FontWeight::Bold);
            return label;
        }

        std::shared_ptr<UltraCanvasLabel> Note(const std::string& id, float x, float y,
                                               float w, float h, const std::string& text) {
            auto label = CreateLabel(id, x, y, w, h);
            label->SetText(text);
            label->SetFontSize(11);
            label->SetTextColor(Color(90, 96, 110, 255));
            label->SetWrap(TextWrap::WrapWord);
            label->SetAlignment(TextAlignment::Left, VerticalAlignment::Top);
            return label;
        }

        // A white card, as UltraPaint's Colour card: what is in it stacks.
        std::shared_ptr<UltraCanvasContainer> Card(const std::string& id, float x, float y, float w) {
            auto card = std::make_shared<UltraCanvasContainer>(id, x, y, w, 0);
            card->layout.SetFlexColumn().SetFlexGap(8)
                        .SetFlexAlignItems(CSSLayout::AlignItems::Start);
            card->SetPadding(10);
            card->SetBackgroundColor(Colors::White);
            card->SetBorders(1.0f, Color(222, 224, 230, 255), 8.0f);
            return card;
        }
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateColorPaletteManagerExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("ColorPaletteExamples", 0, 0, 1000, 900);
        container->SetPadding(0, 0, 10, 0);

        auto title = CreateLabel("ColorPaletteTitle", 20, 10, 0, 30);
        title->SetText("Colour Palettes");
        title->SetFontSize(18);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        container->AddChild(Note("ColorPaletteIntro", 20, 44, 920, 54,
            "Rows of palettes under a colour picker. The + at the end of the last row opens a "
            "gallery of colour schemes - up to a whole 256-colour GIF palette - and each scheme's "
            "own + adds it as a row. With several rows, - takes a row away. Left click a swatch to "
            "pick its colour; right click it to change that entry in a small foreground-only picker."));

        const float col1X = 20.0f, col2X = 340.0f, col3X = 660.0f;
        const float rowY = 135.0f;
        const float cardW = 300.0f;
        const float innerW = cardW - 20.0f;

        // ===== Column 1: under a colour picker, as in UltraPaint =====
        container->AddChild(Caption("PaletteUnderPickerLabel", col1X, rowY - 25, cardW,
                                    "Under a colour picker (as in UltraPaint)"));
        auto card = Card("PaletteUnderPickerCard", col1X, rowY, cardW);
        auto picker = CreateColorPicker("PalettePicker", Color(0x1E, 0x9C, 0xD6, 0xFF), 0, 0, innerW, 300);
        picker->SetStyle(ColorPickerStyle::Light());
        picker->SetUIScale(0.78f);
        picker->SetSlidersCollapsible(true, false);
        picker->SetAutoHeight(true);
        picker->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        card->AddChild(picker);

        auto palettes = CreateColorPaletteManager("PaletteUnderPicker", 0, 0, innerW);
        palettes->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        // Outlined and accented as the picker above it, and the entry editor
        // looks like it too.
        ColorPaletteManagerStyle style = palettes->GetManagerStyle();
        style.swatchBorder = picker->GetStyle().fieldBorderColor;
        style.swatchHoverBorder = picker->GetStyle().mutedTextColor;
        style.swatchSelectedBorder = picker->GetStyle().accentColor;
        style.editorPickerStyle = picker->GetStyle();
        palettes->SetManagerStyle(style);
        card->AddChild(palettes);

        auto status = Note("PaletteUnderPickerStatus", 0, 0, innerW, 34,
                           "1 palette. Click + to add one.");
        status->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        card->AddChild(status);
        container->AddChild(card);

        // The picker follows a left click; the status line reports what
        // happened. Raw pointers: the callbacks live on the manager, which
        // sits in the same card.
        palettes->onColorSelected = [picker = picker.get(), status = status.get()](const Color& c) {
            picker->SetForegroundColor(c, false);
            status->SetText("Picked " + Hex(c) + ".");
        };
        palettes->onPalettesChanged = [m = palettes.get(), status = status.get()]() {
            const int n = m->GetPaletteCount();
            status->SetText(std::to_string(n) + (n == 1 ? " palette." : " palettes.") +
                            " The application saves them with SerializePalettes().");
        };

        // ===== Column 2: several palettes =====
        container->AddChild(Caption("SeveralPalettesLabel", col2X, rowY - 25, cardW,
                                    "Several rows: - on each, + on the last"));
        auto severalCard = Card("SeveralPalettesCard", col2X, rowY, cardW);
        auto several = CreateColorPaletteManager("SeveralPalettes", 0, 0, innerW);
        several->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        several->SetPalettes({UltraCanvasColorPaletteManager::DefaultPalette(), Preset("PICO-8"),
                              Preset("Game Boy"), Preset("Sunset"), Preset("Pastel")});
        severalCard->AddChild(several);
        severalCard->AddChild(Note("SeveralPalettesNote", 0, 0, innerW, 52,
            "Retro and mood schemes from the gallery. Right click a swatch to change it; "
            "Revert puts it back."));
        container->AddChild(severalCard);

        // ===== Column 3: indexed palettes =====
        container->AddChild(Caption("GifPaletteLabel", col3X, rowY - 25, cardW,
                                    "Indexed palettes: up to 256 colours"));
        auto gifCard = Card("GifPaletteCard", col3X, rowY, cardW);
        auto gif = CreateColorPaletteManager("GifPalette", 0, 0, innerW);
        gif->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        gif->SetPalettes({Preset("Web-safe 216"), Preset("GIF 256 (RGB 3-3-2)")});
        gifCard->AddChild(gif);
        gifCard->AddChild(Note("GifPaletteNote", 0, 0, innerW, 52,
            "A large palette wraps into a block of small swatches instead of "
            "shrinking to hairlines."));
        container->AddChild(gifCard);

        return container;
    }

} // namespace UltraCanvas
