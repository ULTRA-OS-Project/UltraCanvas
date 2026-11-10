// Apps/DemoApp/UltraCanvasChipExamples.cpp
// Demonstration of UltraCanvasChip and UltraCanvasTagInput: filled/outlined
// chips, closable chips, selectable filter chips, and a tag/token input field
// (type + Enter to add, × or Backspace to remove) with wrapping.
// Version: 1.1.0 - the shared page look (UltraCanvasDemoPageKit): one card per
//                  example, chips in wrapping rows, tag fields across the card
// Version: 1.0.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "UltraCanvasChip.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include <sstream>

namespace UltraCanvas {

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateChipExamples() {
        auto page = DemoKit::CreatePage("ChipExamples", Tr("Chip / Tag Examples"),
                Tr("Compact chips (filled / outlined / closable / selectable) and a tag-input field: "
                "type and press Enter or comma to add a tag, click × or press Backspace to remove."));

        // ===== 1. BASIC CHIPS (filled / outlined / closable) =====
        auto basicCard = DemoKit::AddCard(page, "ChL1", Tr("Chips: filled, outlined, closable:"));
        // Chips size their own width to the label, so a row just lines them up.
        auto basicRow = DemoKit::AddRow(basicCard, "ChBasicRow", 10);

        auto c1 = CreateChip("Chip1", 0, 0, Tr("Filled"));
        DemoKit::Add(basicRow, c1);

        auto c2 = CreateChip("Chip2", 0, 0, Tr("Outlined"));
        c2->SetVariant(ChipVariant::Outlined);
        DemoKit::Add(basicRow, c2);

        auto c3 = CreateChip("Chip3", 0, 0, Tr("Removable"), true);
        c3->onClose = [c3 = c3.get()]() { c3->SetVisible(false); };
        DemoKit::Add(basicRow, c3);

        auto c4 = CreateChip("Chip4", 0, 0, Tr("Outlined ×"), true);
        c4->SetVariant(ChipVariant::Outlined);
        c4->onClose = [c4 = c4.get()]() { c4->SetVisible(false); };
        DemoKit::Add(basicRow, c4);

        // ===== 2. FILTER CHIPS (selectable) =====
        auto filterCard = DemoKit::AddCard(page, "ChL2", Tr("Filter chips (selectable — click to toggle):"));
        auto filterRow = DemoKit::AddRow(filterCard, "ChFilterRow", 10);

        auto filterReadout = DemoKit::CreateText("ChFilterOut", Tr("Selected: (none)"));
        filterReadout->SetBackgroundColor(Color(240, 240, 240));
        filterReadout->SetPadding(3);

        std::vector<std::string> filters = {Tr("Design"), Tr("Engineering"), Tr("Sales"), Tr("Support"), Tr("Research")};
        auto selectedSet = std::make_shared<std::vector<std::string>>();
        for (size_t i = 0; i < filters.size(); ++i) {
            auto fc = CreateFilterChip("ChFilter" + std::to_string(i), 0, 0, filters[i]);
            fc->SetVariant(ChipVariant::Outlined);
            std::string name = filters[i];
            fc->onSelectedChanged = [filterReadout, selectedSet, name](bool sel) {
                if (sel) selectedSet->push_back(name);
                else selectedSet->erase(std::remove(selectedSet->begin(), selectedSet->end(), name),
                                        selectedSet->end());
                std::ostringstream oss;
                for (size_t k = 0; k < selectedSet->size(); ++k) {
                    if (k) oss << ", ";
                    oss << (*selectedSet)[k];
                }
                filterReadout->SetText(selectedSet->empty() ? Tr("Selected: (none)")
                                                            : TrF("Selected: {0}", oss.str()));
            };
            DemoKit::Add(filterRow, fc);
        }
        DemoKit::Add(filterCard, filterReadout);

        // ===== 3. TAG INPUT =====
        auto tagCard = DemoKit::AddCard(page, "ChL3", Tr("Tag input (type + Enter / comma; × or Backspace to remove):"));
        // The field grows its own height as the chips wrap; across the card
        // it has the room to show that.
        auto tagInput = CreateTagInput("ChTags", 0, 0, 620, 40);
        tagInput->SetTags({"c++", "ultracanvas", "ui"});
        tagInput->SetPlaceholder(Tr("Add a tag…"));
        DemoKit::AddStretched(tagCard, tagInput);

        auto tagReadout = DemoKit::AddText(tagCard, "ChTagOut", Tr("3 tags"));
        tagReadout->SetBackgroundColor(Color(240, 240, 240));
        tagReadout->SetPadding(3);
        tagInput->onTagsChanged = [tagReadout](const std::vector<std::string>& tags) {
            std::ostringstream oss;
            for (size_t k = 0; k < tags.size(); ++k) { if (k) oss << ", "; oss << tags[k]; }
            tagReadout->SetText(LocalisationDetail::Substitute(
                    TrN("{n} tag: {0}", "{n} tags: {0}", static_cast<long long>(tags.size())), {oss.str()}));
        };

        // ===== 4. TAG INPUT WITH LIMIT / NO DUPLICATES =====
        auto limitCard = DemoKit::AddCard(page, "ChL4", Tr("Tag input — max 5, no duplicates:"));
        auto tagInput2 = CreateTagInput("ChTags2", 0, 0, 620, 40);
        tagInput2->SetMaxTags(5);
        tagInput2->SetAllowDuplicates(false);
        tagInput2->SetPlaceholder(Tr("Up to 5 unique tags…"));
        DemoKit::AddStretched(limitCard, tagInput2);

        // ===== INSTRUCTIONS =====
        DemoKit::AddNotes(page, "ChipInstructions", Tr("API"), {
                Tr("Chip: CreateChip / CreateFilterChip; SetVariant(Filled|Outlined); "
                "SetClosable / SetSelectable / SetIcon; onClick / onClose / onSelectedChanged"),
                Tr("TagInput: CreateTagInput; AddTag / RemoveTag / SetTags / SetMaxTags / "
                "SetAllowDuplicates / validator; onTagsChanged / onTagAdded / onTagRemoved"),
                Tr("The tag field wraps chips across rows and grows its height to fit.")});

        return page;
    }

} // namespace UltraCanvas
