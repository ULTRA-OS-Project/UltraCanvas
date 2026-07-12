// Apps/DemoApp/UltraCanvasChipExamples.cpp
// Demonstration of UltraCanvasChip and UltraCanvasTagInput: filled/outlined
// chips, closable chips, selectable filter chips, and a tag/token input field
// (type + Enter to add, × or Backspace to remove) with wrapping.
// Version: 1.0.0
// Last Modified: 2026-07-07
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasChip.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include <sstream>

namespace UltraCanvas {

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateChipExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("ChipExamples", 0, 0, 1000, 620);
        container->SetPadding(0, 0, 10, 0);

        auto title = CreateLabel("ChipTitle", 20, 10, 0, 30);
        title->SetText("Chip / Tag Examples");
        title->SetFontSize(18);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        auto subtitle = CreateLabel("ChipSubtitle", 20, 42, 940, 40);
        subtitle->SetText("Compact chips (filled / outlined / closable / selectable) and a tag-input field: "
                          "type and press Enter or comma to add a tag, click × or press Backspace to remove.");
        subtitle->SetFontSize(12);
        container->AddChild(subtitle);

        auto section = [&](const std::string& id, const std::string& text, int y) {
            auto l = CreateLabel(id, 20, y, 500, 20);
            l->SetText(text);
            l->SetFontWeight(FontWeight::Bold);
            l->SetFontSize(12);
            container->AddChild(l);
        };

        // ===== 1. BASIC CHIPS (filled / outlined / closable) =====
        section("ChL1", "Chips: filled, outlined, closable:", 92);

        auto c1 = CreateChip("Chip1", 20, 116, "Filled");
        container->AddChild(c1);

        auto c2 = CreateChip("Chip2", 130, 116, "Outlined");
        c2->SetVariant(ChipVariant::Outlined);
        container->AddChild(c2);

        auto c3 = CreateChip("Chip3", 250, 116, "Removable", true);
        c3->onClose = [c3]() { c3->SetVisible(false); };
        container->AddChild(c3);

        auto c4 = CreateChip("Chip4", 390, 116, "Outlined ×", true);
        c4->SetVariant(ChipVariant::Outlined);
        c4->onClose = [c4]() { c4->SetVisible(false); };
        container->AddChild(c4);

        // ===== 2. FILTER CHIPS (selectable) =====
        section("ChL2", "Filter chips (selectable — click to toggle):", 176);
        auto filterReadout = CreateLabel("ChFilterOut", 20, 236, 900, 24);
        filterReadout->SetText("Selected: (none)");
        filterReadout->SetBackgroundColor(Color(240, 240, 240));
        filterReadout->SetPadding(3);
        container->AddChild(filterReadout);

        std::vector<std::string> filters = {"Design", "Engineering", "Sales", "Support", "Research"};
        auto selectedSet = std::make_shared<std::vector<std::string>>();
        int fx = 20;
        for (size_t i = 0; i < filters.size(); ++i) {
            auto fc = CreateFilterChip("ChFilter" + std::to_string(i), fx, 200, filters[i]);
            fc->SetVariant(ChipVariant::Outlined);
            std::string name = filters[i];
            fc->onSelectedChanged = [filterReadout, selectedSet, name](bool sel) {
                if (sel) selectedSet->push_back(name);
                else selectedSet->erase(std::remove(selectedSet->begin(), selectedSet->end(), name),
                                        selectedSet->end());
                std::ostringstream oss;
                oss << "Selected: ";
                if (selectedSet->empty()) oss << "(none)";
                for (size_t k = 0; k < selectedSet->size(); ++k) {
                    if (k) oss << ", ";
                    oss << (*selectedSet)[k];
                }
                filterReadout->SetText(oss.str());
            };
            container->AddChild(fc);
            fx += static_cast<int>(name.size()) * 9 + 44;   // rough spacing for auto-width chips
        }

        // ===== 3. TAG INPUT =====
        section("ChL3", "Tag input (type + Enter / comma; × or Backspace to remove):", 288);
        auto tagInput = CreateTagInput("ChTags", 20, 314, 620, 40);
        tagInput->SetTags({"c++", "ultracanvas", "ui"});
        tagInput->SetPlaceholder("Add a tag…");
        container->AddChild(tagInput);

        auto tagReadout = CreateLabel("ChTagOut", 660, 322, 320, 24);
        tagReadout->SetText("3 tags");
        tagReadout->SetBackgroundColor(Color(240, 240, 240));
        tagReadout->SetPadding(3);
        container->AddChild(tagReadout);
        tagInput->onTagsChanged = [tagReadout](const std::vector<std::string>& tags) {
            std::ostringstream oss;
            oss << tags.size() << (tags.size() == 1 ? " tag: " : " tags: ");
            for (size_t k = 0; k < tags.size(); ++k) { if (k) oss << ", "; oss << tags[k]; }
            tagReadout->SetText(oss.str());
        };

        // ===== 4. TAG INPUT WITH LIMIT / NO DUPLICATES =====
        section("ChL4", "Tag input — max 5, no duplicates:", 420);
        auto tagInput2 = CreateTagInput("ChTags2", 20, 446, 620, 40);
        tagInput2->SetMaxTags(5);
        tagInput2->SetAllowDuplicates(false);
        tagInput2->SetPlaceholder("Up to 5 unique tags…");
        container->AddChild(tagInput2);

        // ===== INSTRUCTIONS =====
        auto instructions = CreateLabel("ChipInstructions", 20, 520, 940, 80);
        instructions->SetText(
                "API:\n"
                "\xE2\x80\xA2 Chip: CreateChip / CreateFilterChip; SetVariant(Filled|Outlined); "
                "SetClosable / SetSelectable / SetIcon; onClick / onClose / onSelectedChanged\n"
                "\xE2\x80\xA2 TagInput: CreateTagInput; AddTag / RemoveTag / SetTags / SetMaxTags / "
                "SetAllowDuplicates / validator; onTagsChanged / onTagAdded / onTagRemoved\n"
                "\xE2\x80\xA2 The tag field wraps chips across rows and grows its height to fit.");
        instructions->SetFontSize(11);
        instructions->SetBackgroundColor(Color(255, 255, 240));
        instructions->SetPadding(6);
        container->AddChild(instructions);

        return container;
    }

} // namespace UltraCanvas
