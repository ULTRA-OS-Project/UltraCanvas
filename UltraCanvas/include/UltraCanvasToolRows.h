// include/UltraCanvasToolRows.h
// The rows a tool panel (UltraCanvasToolPanelStack) is built from, so that a
// panel shows a value where a toolbar shows only a button:
//
// - UltraCanvasToolButtonRow: a wrapping row of 28 x 28 icon buttons and
//   toggles (B I U S, the alignments), each with its name and shortcut as
//   its tooltip ("Bold (Ctrl+B)");
// - UltraCanvasToolValueRows: labelled values - "Spacing [1.15]", "Before
//   [0 pt]", a colour out of a row of swatches - laid out as a form grid
//   (UltraCanvasFormLayout.h) whose caption column is as wide as its widest
//   caption in any language; and full-width list rows (the paragraph style,
//   the font with UltraCanvasFontPicker) across both columns.
//
// Every control is the framework's own (UltraCanvasButton,
// UltraCanvasSpinner, UltraCanvasDropdown, UltraCanvasColorSwatchBar,
// UltraCanvasFontPicker) and is editable: a row takes a getter and a setter.
// The setter runs when the user changes the value; Sync() reads every getter
// again and shows what it returns without calling a setter, so the host
// mirrors the document after each change of the selection
// (UltraCanvasToolPanelStack::Sync calls every row's).
//
// doc-check: bool bold = false; double spacing = 1.15;
// doc-check: example
//     auto font = CreateToolButtonRow("font-buttons");
//     font->AddToggle("bold", "media/icons/textformat/bold.svg", "Bold", "Ctrl+B",
//                     [&] { return bold; }, [&](bool on) { bold = on; });
//     auto values = CreateToolValueRows("paragraph-values");
//     values->AddSpinner("spacing", "Spacing", 0.5, 4.0, 0.05, 2,
//                        [&] { return spacing; }, [&](double v) { spacing = v; });
//     auto content = CreateToolPanelContent("paragraph-content");
//     content->AddChild(font);
//     content->AddChild(values);
//
// Version: 1.0.0
// Last Modified: 2026-11-27
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasContainer.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasColorSwatchBar.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasFontPicker.h"
#include "UltraCanvasSpinner.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

    // One button of a tool row. With isChecked or setChecked it is a toggle
    // (pressed while isChecked() says so); otherwise a click runs onClick.
    struct ToolRowButton {
        std::string id;
        std::string iconPath;
        std::string name;         // its tooltip and its accessible name: "Bold"
        std::string shortcut;     // added to the tooltip: "Bold (Ctrl+B)"; empty: none
        std::string text;         // shown beside the icon (or alone); empty: icon only
        std::function<void()> onClick;
        std::function<bool()> isChecked;
        std::function<void(bool)> setChecked;
        std::function<bool()> isEnabled;   // read by Sync; empty: always enabled
    };

    // A wrapping row of tool buttons.
    class UltraCanvasToolButtonRow : public UltraCanvasContainer {
    public:
        AccessibleRole GetAccessibleRole() const override { return AccessibleRole::Toolbar; }

        explicit UltraCanvasToolButtonRow(const std::string& identifier, float buttonSize = 28.0f);

        std::shared_ptr<UltraCanvasButton> AddButton(const ToolRowButton& button);
        std::shared_ptr<UltraCanvasButton> AddButton(const std::string& id, const std::string& iconPath,
                                                     const std::string& name, const std::string& shortcut,
                                                     std::function<void()> onClick);
        std::shared_ptr<UltraCanvasButton> AddToggle(const std::string& id, const std::string& iconPath,
                                                     const std::string& name, const std::string& shortcut,
                                                     std::function<bool()> isChecked,
                                                     std::function<void(bool)> setChecked);
        // Space between two groups of buttons (the alignments, the indents).
        void AddGap(float width = 8.0f);
        std::shared_ptr<UltraCanvasButton> GetButton(const std::string& id) const;
        float GetButtonSize() const { return buttonSize; }

        // Each toggle pressed as its isChecked says, each button enabled as
        // its isEnabled says; no setter runs.
        void Sync();

        void ApplyTheme(const UltraCanvasTheme& theme) override;

    private:
        struct Entry {
            ToolRowButton spec;
            std::shared_ptr<UltraCanvasButton> button;
        };
        void StyleButton(UltraCanvasButton& button, const UltraCanvasTheme& theme) const;
        std::vector<Entry> entries;
        float buttonSize = 28.0f;
        int gapCount = 0;
    };

    // Labelled values and full-width lists, as a form grid.
    class UltraCanvasToolValueRows : public UltraCanvasContainer {
    public:
        explicit UltraCanvasToolValueRows(const std::string& identifier, float rowGap = 6.0f,
                                          float columnGap = 8.0f);

        // "caption [spinner]": a number with its unit as a suffix ("pt").
        std::shared_ptr<UltraCanvasSpinner> AddSpinner(const std::string& id, const std::string& caption,
                                                       double minValue, double maxValue, double step,
                                                       int decimals, std::function<double()> getValue,
                                                       std::function<void(double)> setValue,
                                                       const std::string& suffix = std::string());
        // "caption [dropdown]": one of a few choices, by index.
        std::shared_ptr<UltraCanvasDropdown> AddDropdown(const std::string& id, const std::string& caption,
                                                         const std::vector<std::string>& items,
                                                         std::function<int()> getIndex,
                                                         std::function<void(int)> setIndex);
        // "caption [swatches]": a colour out of a palette.
        std::shared_ptr<UltraCanvasColorSwatchBar> AddColorSwatches(const std::string& id, const std::string& caption,
                                                                    const std::vector<Color>& colors,
                                                                    std::function<Color()> getColor,
                                                                    std::function<void(const Color&)> setColor);
        // A full-width list: the paragraph style, the number format.
        std::shared_ptr<UltraCanvasDropdown> AddListRow(const std::string& id, const std::vector<std::string>& items,
                                                        std::function<int()> getIndex,
                                                        std::function<void(int)> setIndex);
        // A full-width font list; getFamily is the selection's family
        // (empty: a selection in several fonts, the box blank).
        std::shared_ptr<UltraCanvasFontPicker> AddFontPicker(const std::string& id,
                                                             std::function<std::string()> getFamily,
                                                             std::function<void(const FontPickerChoice&)> setFont);
        // A control of the host's own, with what Sync does for it.
        void AddRow(const std::string& id, const std::string& caption,
                    const std::shared_ptr<UltraCanvasUIElement>& control, std::function<void()> sync = {});
        void AddWideRow(const std::shared_ptr<UltraCanvasUIElement>& element, std::function<void()> sync = {});

        // Every control shows its getter's value again; no setter runs.
        void Sync();
        // True while Sync runs: a control of the host's own skips its setter then.
        bool IsSyncing() const { return syncing; }

    private:
        std::vector<std::function<void()>> syncers;
        bool syncing = false;
    };

    // ===== FACTORIES =====
    inline std::shared_ptr<UltraCanvasToolButtonRow> CreateToolButtonRow(const std::string& identifier,
                                                                         float buttonSize = 28.0f) {
        return std::make_shared<UltraCanvasToolButtonRow>(identifier, buttonSize);
    }
    inline std::shared_ptr<UltraCanvasToolValueRows> CreateToolValueRows(const std::string& identifier) {
        return std::make_shared<UltraCanvasToolValueRows>(identifier);
    }
    // A panel's content: its rows top to bottom, 6 px apart.
    std::shared_ptr<UltraCanvasContainer> CreateToolPanelContent(const std::string& identifier, float rowGap = 6.0f);

    // Sync on every tool row inside `container`, however deep.
    void SyncToolRows(UltraCanvasContainer* container);

} // namespace UltraCanvas
