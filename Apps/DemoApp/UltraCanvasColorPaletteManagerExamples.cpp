// Apps/DemoApp/UltraCanvasColorPaletteManagerExamples.cpp
// Demonstration of the colour palette manager: palette rows under a colour
// picker, the gallery of ready-made schemes behind "+", "-" on every row
// once there are several, and the right-click editor for one entry.
// Version: 1.1.0 - the shared page look (UltraCanvasDemoPageKit): the three
//                  examples as kit cards in a wrapping row, notes sized to fit
// Version: 1.0.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
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
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateColorPaletteManagerExamples() {
        auto page = DemoKit::CreatePage("ColorPaletteExamples", "Colour Palettes",
            "Rows of palettes under a colour picker. The + at the end of the last row opens a "
            "gallery of colour schemes - up to a whole 256-colour GIF palette - and each scheme's "
            "own + adds it as a row. With several rows, - takes a row away. Left click a swatch to "
            "pick its colour; right click it to change that entry in a small foreground-only picker.");

        // The picker and the palette rows keep the width of UltraPaint's
        // Colour card; the cards share the page width around them.
        const float innerW = 280.0f;
        auto row = DemoKit::AddRow(page, "ColorPaletteRow");

        // ===== Column 1: under a colour picker, as in UltraPaint =====
        auto card = DemoKit::AddCard(row, "PaletteUnderPickerCard",
                                     "Under a colour picker (as in UltraPaint)");
        auto picker = CreateColorPicker("PalettePicker", Color(0x1E, 0x9C, 0xD6, 0xFF), 0, 0, innerW, 300);
        picker->SetStyle(ColorPickerStyle::Light());
        picker->SetUIScale(0.78f);
        picker->SetSlidersCollapsible(true, false);
        picker->SetAutoHeight(true);
        DemoKit::AddFixed(card, picker);

        auto palettes = CreateColorPaletteManager("PaletteUnderPicker", 0, 0, innerW);
        // Outlined and accented as the picker above it, and the entry editor
        // looks like it too.
        ColorPaletteManagerStyle style = palettes->GetManagerStyle();
        style.swatchBorder = picker->GetStyle().fieldBorderColor;
        style.swatchHoverBorder = picker->GetStyle().mutedTextColor;
        style.swatchSelectedBorder = picker->GetStyle().accentColor;
        style.editorPickerStyle = picker->GetStyle();
        palettes->SetManagerStyle(style);
        DemoKit::AddFixed(card, palettes);

        auto status = DemoKit::AddText(card, "PaletteUnderPickerStatus",
                                       "1 palette. Click + to add one.");

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
        auto severalCard = DemoKit::AddCard(row, "SeveralPalettesCard",
                                            "Several rows: - on each, + on the last");
        auto several = CreateColorPaletteManager("SeveralPalettes", 0, 0, innerW);
        several->SetPalettes({UltraCanvasColorPaletteManager::DefaultPalette(), Preset("PICO-8"),
                              Preset("Game Boy"), Preset("Sunset"), Preset("Pastel")});
        DemoKit::AddFixed(severalCard, several);
        DemoKit::AddCaption(severalCard, "SeveralPalettesNote",
            "Retro and mood schemes from the gallery. Right click a swatch to change it; "
            "Revert puts it back.");

        // ===== Column 3: indexed palettes =====
        auto gifCard = DemoKit::AddCard(row, "GifPaletteCard", "Indexed palettes: up to 256 colours");
        auto gif = CreateColorPaletteManager("GifPalette", 0, 0, innerW);
        gif->SetPalettes({Preset("Web-safe 216"), Preset("GIF 256 (RGB 3-3-2)")});
        DemoKit::AddFixed(gifCard, gif);
        DemoKit::AddCaption(gifCard, "GifPaletteNote",
            "A large palette wraps into a block of small swatches instead of "
            "shrinking to hairlines.");

        return page;
    }

} // namespace UltraCanvas
