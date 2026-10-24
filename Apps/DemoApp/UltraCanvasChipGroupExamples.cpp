// Apps/DemoApp/UltraCanvasChipGroupExamples.cpp
// Demonstration of UltraCanvasChipGroup: a row of chips under one selection
// rule - single (a category bar), single with nothing allowed (a filter that
// can be switched off), multiple (tags to match) - with the selection read
// out below each.
// Version: 1.0.0
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "UltraCanvasChipGroup.h"
#include "UltraCanvasLabel.h"
#include <sstream>

namespace UltraCanvas {

    namespace {
        std::string Describe(const UltraCanvasChipGroup& group) {
            std::ostringstream oss;
            const std::vector<int> sel = group.GetSelectedIndices();
            if (sel.empty()) return "Selected: (none)";
            oss << "Selected: ";
            for (size_t i = 0; i < sel.size(); ++i) {
                if (i) oss << ", ";
                oss << group.GetChip(sel[i])->GetLabel();
            }
            return oss.str();
        }
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateChipGroupExamples() {
        auto page = DemoKit::CreatePage("ChipGroupExamples", "Chip Group Examples",
                "Real chips in a wrapping row; the group decides what a click means for the others "
                "and reports the selection.");

        // ===== 1. SINGLE SELECTION =====
        auto singleCard = DemoKit::AddCard(page, "CgL1", "Single selection: a category bar",
                                           "One chip at a time; clicking the selected one keeps it.");
        auto single = CreateChipGroup("CgSingle");
        for (const char* name : {"All", "Video", "Audio", "Graphics", "Office", "Games", "Developer"}) single->AddChip(name);
        single->SetSelectedIndex(0);
        DemoKit::Add(singleCard, single);
        auto singleOut = DemoKit::AddText(singleCard, "CgSingleOut", Describe(*single));
        singleOut->SetBackgroundColor(Color(240, 240, 240));
        singleOut->SetPadding(3);
        single->onSelectionChanged = [singleOut, g = single.get()](int) { singleOut->SetText(Describe(*g)); };

        // ===== 2. SINGLE, NOTHING ALLOWED =====
        auto noneCard = DemoKit::AddCard(page, "CgL2", "Single selection, none allowed: a filter",
                                         "Clicking the selected chip switches the filter off.");
        auto none = CreateChipGroup("CgNone");
        none->SetAllowNoSelection(true);
        none->SetChipVariant(ChipVariant::Outlined);
        for (const char* name : {"Free", "Paid", "Installed", "Has updates"}) none->AddChip(name);
        DemoKit::Add(noneCard, none);
        auto noneOut = DemoKit::AddText(noneCard, "CgNoneOut", Describe(*none));
        noneOut->SetBackgroundColor(Color(240, 240, 240));
        noneOut->SetPadding(3);
        none->onSelectionChanged = [noneOut, g = none.get()](int) { noneOut->SetText(Describe(*g)); };

        // ===== 3. MULTIPLE =====
        auto multiCard = DemoKit::AddCard(page, "CgL3", "Multiple selection: tags",
                                          "Any number; the readout lists them in order.");
        auto multi = CreateChipGroup("CgMulti");
        multi->SetSelectionMode(ChipGroupSelectionMode::Multiple);
        for (const char* name : {"c++", "ui", "cross-platform", "vector", "video", "audio", "database", "network"}) multi->AddChip(name);
        multi->SetSelectedIndices({0, 1});
        DemoKit::Add(multiCard, multi);
        auto multiOut = DemoKit::AddText(multiCard, "CgMultiOut", Describe(*multi));
        multiOut->SetBackgroundColor(Color(240, 240, 240));
        multiOut->SetPadding(3);
        multi->onSelectedIndicesChanged = [multiOut, g = multi.get()](const std::vector<int>&) { multiOut->SetText(Describe(*g)); };

        DemoKit::AddNotes(page, "CgNotes", "Implementation notes", {
            "UltraCanvasChipGroup is a container of real UltraCanvasChip children: each keeps its own hover, focus and Space / Return.",
            "Single mode deselects the other chip; SetAllowNoSelection(true) lets a second click clear the selection.",
            "Multiple mode toggles each chip; onSelectedIndicesChanged reports every selected index.",
            "SetChipStyle / SetChipVariant apply to every chip, now and later; the row wraps when the chips do not fit.",
        });
        return page;
    }

} // namespace UltraCanvas
