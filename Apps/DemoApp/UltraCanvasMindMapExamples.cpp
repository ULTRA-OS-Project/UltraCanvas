// Apps/DemoApp/UltraCanvasMindMapExamples.cpp
// Demo examples for UltraCanvasMindMap — mind map diagram
// Version: 1.0.0
// Last Modified: 2026-07-30
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "Plugins/Diagrams/UltraCanvasMindMap.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasTabbedContainer.h"
#include <sstream>

namespace UltraCanvas {

namespace {

// Wires the shared status read-out every tab uses.
void AttachStatus(const std::shared_ptr<UltraCanvasMindMap>& map,
                  const std::shared_ptr<UltraCanvasLabel>& status) {
    map->onTopicClick = [map, status](const std::string& id) {
        const MindMapTopic* topic = map->GetTopic(id);
        if (!topic) return;
        std::ostringstream out;
        out << "\"" << topic->text << "\"  ·  depth " << topic->depth;
        int descendants = map->Model().CountDescendants(id);
        if (descendants > 0) out << "  ·  " << descendants << " descendants";
        status->SetText(out.str());
    };
    map->onCollapseChanged = [map, status](const std::string& id, bool collapsed) {
        const MindMapTopic* topic = map->GetTopic(id);
        status->SetText(std::string(collapsed ? "Collapsed " : "Expanded ") +
                        (topic ? "\"" + topic->text + "\"" : id));
    };
    map->onTopicTextChanged = [status](const std::string&, const std::string& previous) {
        status->SetText("Renamed (was \"" + previous + "\")");
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// TAB 1 — Classic balanced map, editable (reference image 4)
// ─────────────────────────────────────────────────────────────────────────────
std::shared_ptr<UltraCanvasContainer> MakeEditableTab(
        std::shared_ptr<UltraCanvasLabel> status) {
    auto tab = std::make_shared<UltraCanvasContainer>("MindMapEditableTab", 0, 0, 1020, 700);

    auto desc = std::make_shared<UltraCanvasLabel>("MindMapEditableDesc", 10, 8, 990, 22);
    desc->SetText("Balanced structure. Double-click or F2 to rename, Enter for a sibling, "
                  "Tab for a child, Delete to remove, drag onto another topic to reparent, "
                  "Ctrl+Z / Ctrl+Shift+Z to undo and redo.");
    desc->SetFontSize(11);
    desc->SetWrap(TextWrap::WrapWord);
    tab->AddChild(desc);

    auto map = std::make_shared<UltraCanvasMindMap>("MindMapEditable", 10, 34, 990, 654);
    map->SetTheme(MindMapTheme::Colorful);
    map->SetStructure(MindMapStructure::Balanced);
    map->SetBalancePolicy(MindMapBalancePolicy::MinimiseSubtreeWeight);
    map->SetControlsVisible(true);
    map->SetMinimapVisible(true);

    map->BuildFromOutline({
        {0, "Enforce fire safety regulations"},
        {1, "Inspection"},
        {2, "Periodic inspection"},
        {2, "Inspection by complaints"},
        {2, "Special inspection"},
        {3, "Prior to a big event"},
        {1, "Personnel"},
        {2, "Manage inspector works"},
        {2, "Conduct inspections"},
        {2, "Assist inspectors"},
        {2, "Draft inspection form"},
        {1, "Risk"},
        {2, "Types of risk"},
        {3, "Low risk"},
        {3, "High risk"},
        {2, "Business nature"},
        {2, "Property type"},
        {1, "Inspection report"},
        {2, "Keep in department"},
        {2, "Send to archive"},
        {2, "May include photos"},
    });

    AttachStatus(map, status);
    tab->AddChild(map);
    return tab;
}

// ─────────────────────────────────────────────────────────────────────────────
// TAB 2 — Logic chart with numbered badges and an icon (reference image 8)
// ─────────────────────────────────────────────────────────────────────────────
std::shared_ptr<UltraCanvasContainer> MakeLogicChartTab(
        std::shared_ptr<UltraCanvasLabel> status) {
    auto tab = std::make_shared<UltraCanvasContainer>("MindMapLogicTab", 0, 0, 1020, 700);

    auto desc = std::make_shared<UltraCanvasLabel>("MindMapLogicDesc", 10, 8, 990, 22);
    desc->SetText("LogicRight structure: the whole tree grows to one side. Main topics carry "
                  "automatic ordinal badges in their branch hue; leaves use the underline "
                  "shape with no box.");
    desc->SetFontSize(11);
    desc->SetWrap(TextWrap::WrapWord);
    tab->AddChild(desc);

    auto map = std::make_shared<UltraCanvasMindMap>("MindMapLogic", 10, 34, 990, 654);
    map->SetTheme(MindMapTheme::Default);
    map->SetStructure(MindMapStructure::LogicRight);
    map->SetNumbering(MindMapNumbering::Badge);
    map->SetControlsVisible(true);

    map->BuildFromOutline({
        {0, "Mind Mapping"},
        {1, "Analysis"},
        {2, "A structured approach to brainstorming"},
        {2, "Define what you know and do not know"},
        {1, "Objectives"},
        {2, "Define the goal clearly"},
        {2, "Identify success criteria"},
        {1, "Strategy"},
        {2, "Brainstorm different strategies"},
        {2, "Evaluate pros and cons of each"},
        {1, "Action"},
        {2, "Break the strategy into steps"},
        {2, "Identify required resources"},
        {2, "Assign responsibilities"},
        {2, "Set timelines"},
        {1, "Revision"},
        {2, "Review and refine the scheme"},
        {2, "Make adjustments as needed"},
        {2, "Update responsibilities and timelines"},
        {1, "Solution"},
        {2, "Implement the plan"},
        {2, "Track progress against the goal"},
        {2, "Celebrate success"},
        {2, "Continuously improve"},
    });

    // Leaves as plain underlined text, the software mind-map convention.
    MindMapTopicStyle leaf = map->GetLevelStyle(2);
    leaf.shape = MindMapNodeShape::Underline;
    leaf.maxWidth = 300.0;
    map->SetLevelStyle(2, leaf);
    map->SetLevelStyle(3, leaf);

    // A legend, parked off to the side as a floating topic.
    std::string legend = map->AddFloatingTopic("Mind mapping · brainstorming", 620.0, -300.0);
    MindMapTopicStyle legendStyle = map->GetLevelStyle(1);
    legendStyle.fillColor = Color(232, 245, 233, 255);
    legendStyle.borderColor = Color(120, 180, 130, 255);
    legendStyle.textColor = Color(45, 90, 55, 255);
    map->SetTopicStyle(legend, legendStyle);

    AttachStatus(map, status);
    tab->AddChild(map);
    return tab;
}

// ─────────────────────────────────────────────────────────────────────────────
// TAB 3 — Radial map (reference image 10)
// ─────────────────────────────────────────────────────────────────────────────
std::shared_ptr<UltraCanvasContainer> MakeRadialTab(
        std::shared_ptr<UltraCanvasLabel> status) {
    auto tab = std::make_shared<UltraCanvasContainer>("MindMapRadialTab", 0, 0, 1020, 700);

    auto desc = std::make_shared<UltraCanvasLabel>("MindMapRadialDesc", 10, 8, 990, 22);
    desc->SetText("Radial structure: branches distributed over a full circle, one ring per "
                  "generation, each wedge sized by its subtree's leaf count. Circular nodes "
                  "with straight spokes and dot terminators.");
    desc->SetFontSize(11);
    desc->SetWrap(TextWrap::WrapWord);
    tab->AddChild(desc);

    auto map = std::make_shared<UltraCanvasMindMap>("MindMapRadial", 10, 34, 990, 654);
    map->SetTheme(MindMapTheme::Colorful);
    map->SetStructure(MindMapStructure::Radial);
    map->SetConnectorStyle(MindMapConnectorStyle::Straight);
    map->Style().endDecoration = MindMapEndDecoration::Dot;
    map->Style().connectorColorMode = MindMapConnectorColorMode::InheritFromBranch;
    map->SetControlsVisible(true);

    map->BuildFromOutline({
        {0, "Mind Map Diagrams"},
        {1, "Simple Text"}, {2, "Detail"},
        {1, "Simple Text"}, {2, "Detail"},
        {1, "Simple Text"}, {2, "Detail"},
        {1, "Simple Text"}, {2, "Detail"},
        {1, "Simple Text"}, {2, "Detail"},
        {1, "Simple Text"}, {2, "Detail"},
        {1, "Simple Text"}, {2, "Detail"},
        {1, "Simple Text"}, {2, "Detail"},
    });

    // Uniform circles: a min size larger than any label makes every node match.
    for (int level = 0; level <= 2; ++level) {
        MindMapTopicStyle levelStyle = map->GetLevelStyle(level);
        levelStyle.shape = MindMapNodeShape::Circle;
        levelStyle.minWidth = (level == 0) ? 150.0 : 96.0;
        levelStyle.minHeight = (level == 0) ? 150.0 : 96.0;
        levelStyle.maxWidth = 110.0;
        map->SetLevelStyle(level, levelStyle);
    }

    MindMapLayoutOptions options = map->GetLayoutOptions();
    options.radialRingGap = 190.0;
    map->SetLayoutOptions(options);

    AttachStatus(map, status);
    tab->AddChild(map);
    return tab;
}

// ─────────────────────────────────────────────────────────────────────────────
// TAB 4 — Cross-branch relationships (reference image 6)
// ─────────────────────────────────────────────────────────────────────────────
std::shared_ptr<UltraCanvasContainer> MakeRelationshipsTab(
        std::shared_ptr<UltraCanvasLabel> status) {
    auto tab = std::make_shared<UltraCanvasContainer>("MindMapRelTab", 0, 0, 1020, 700);

    auto desc = std::make_shared<UltraCanvasLabel>("MindMapRelDesc", 10, 8, 990, 22);
    desc->SetText("Non-hierarchical relationships: dashed labelled arcs linking topics that "
                  "belong to different branches, drawn independently of the branch strokes. "
                  "Rounded-elbow connectors.");
    desc->SetFontSize(11);
    desc->SetWrap(TextWrap::WrapWord);
    tab->AddChild(desc);

    auto map = std::make_shared<UltraCanvasMindMap>("MindMapRel", 10, 34, 990, 654);
    map->SetTheme(MindMapTheme::Pastel);
    map->SetStructure(MindMapStructure::Balanced);
    map->SetConnectorStyle(MindMapConnectorStyle::RoundedElbow);
    map->SetControlsVisible(true);

    std::string root = map->SetCentralTopic("Mind Map");
    struct Branch { const char* label; const char* leaves[3]; };
    Branch branches[] = {
        {"Subtopic A", {"Item A1", "Item A2", "Item A3"}},
        {"Subtopic B", {"Item B1", "Item B2", "Item B3"}},
        {"Subtopic C", {"Item C1", "Item C2", "Item C3"}},
        {"Subtopic D", {"Item D1", "Item D2", "Item D3"}},
    };

    std::vector<std::vector<std::string>> leafIds;
    for (const auto& branch : branches) {
        std::string branchId = map->AddTopic(root, branch.label);
        std::vector<std::string> ids;
        for (const char* leaf : branch.leaves) {
            ids.push_back(map->AddTopic(branchId, leaf));
        }
        leafIds.push_back(ids);
    }

    // The two cross-branch "Connection" arcs from the reference design.
    if (leafIds.size() >= 4) {
        map->AddRelationship("connection_1", leafIds[0][2], leafIds[2][0], "Connection");
        map->AddRelationship("connection_2", leafIds[1][2], leafIds[3][0], "Connection");
    }

    AttachStatus(map, status);
    tab->AddChild(map);
    return tab;
}

// ─────────────────────────────────────────────────────────────────────────────
// TAB 5 — Themes and structures gallery
// ─────────────────────────────────────────────────────────────────────────────
std::shared_ptr<UltraCanvasContainer> MakeGalleryTab(
        std::shared_ptr<UltraCanvasLabel> status) {
    auto tab = std::make_shared<UltraCanvasContainer>("MindMapGalleryTab", 0, 0, 1020, 700);

    auto desc = std::make_shared<UltraCanvasLabel>("MindMapGalleryDesc", 10, 8, 990, 22);
    desc->SetText("The same map under every theme, structure and connector style. "
                  "Use the buttons to switch; the layout is recomputed and re-fitted each time.");
    desc->SetFontSize(11);
    desc->SetWrap(TextWrap::WrapWord);
    tab->AddChild(desc);

    auto map = std::make_shared<UltraCanvasMindMap>("MindMapGallery", 10, 96, 990, 592);
    map->SetControlsVisible(true);
    map->BuildFromOutline({
        {0, "Product launch"},
        {1, "Research"},
        {2, "User interviews"},
        {2, "Competitor scan"},
        {1, "Design"},
        {2, "Wireframes"},
        {2, "Prototype"},
        {1, "Build"},
        {2, "Backend"},
        {2, "Frontend"},
        {1, "Launch"},
        {2, "Marketing"},
        {2, "Support"},
    });
    AttachStatus(map, status);

    struct ThemeButton { const char* label; MindMapTheme theme; };
    const ThemeButton themes[] = {
        {"Default",      MindMapTheme::Default},
        {"Professional", MindMapTheme::Professional},
        {"Colorful",     MindMapTheme::Colorful},
        {"Pastel",       MindMapTheme::Pastel},
        {"Dark",         MindMapTheme::Dark},
        {"Hand drawn",   MindMapTheme::HandDrawn},
    };
    int x = 10;
    for (const auto& entry : themes) {
        auto button = std::make_shared<UltraCanvasButton>(
            std::string("MindMapTheme_") + entry.label, x, 34, 108, 24, entry.label);
        MindMapTheme value = entry.theme;
        std::string label = entry.label;
        button->onClick = [map, status, value, label]() {
            map->SetTheme(value);
            map->FitView();
            status->SetText("Theme: " + label);
        };
        tab->AddChild(button);
        x += 112;
    }

    struct StructureButton { const char* label; MindMapStructure structure; };
    const StructureButton structures[] = {
        {"Balanced",    MindMapStructure::Balanced},
        {"Logic right", MindMapStructure::LogicRight},
        {"Logic left",  MindMapStructure::LogicLeft},
        {"Org chart",   MindMapStructure::OrgChartDown},
        {"Radial",      MindMapStructure::Radial},
    };
    x = 10;
    for (const auto& entry : structures) {
        auto button = std::make_shared<UltraCanvasButton>(
            std::string("MindMapStruct_") + entry.label, x, 62, 108, 24, entry.label);
        MindMapStructure value = entry.structure;
        std::string label = entry.label;
        button->onClick = [map, status, value, label]() {
            map->SetStructure(value);
            map->FitView();
            status->SetText("Structure: " + label);
        };
        tab->AddChild(button);
        x += 112;
    }

    struct ConnectorButton { const char* label; MindMapConnectorStyle connector; };
    const ConnectorButton connectors[] = {
        {"Curve",    MindMapConnectorStyle::Curve},
        {"Straight", MindMapConnectorStyle::Straight},
        {"Elbow",    MindMapConnectorStyle::Elbow},
        {"Rounded",  MindMapConnectorStyle::RoundedElbow},
        {"Tapered",  MindMapConnectorStyle::TaperedBranch},
    };
    x = 580;
    for (const auto& entry : connectors) {
        auto button = std::make_shared<UltraCanvasButton>(
            std::string("MindMapConn_") + entry.label, x, 62, 82, 24, entry.label);
        MindMapConnectorStyle value = entry.connector;
        std::string label = entry.label;
        button->onClick = [map, status, value, label]() {
            map->SetConnectorStyle(value);
            status->SetText("Connector: " + label);
        };
        tab->AddChild(button);
        x += 86;
    }

    tab->AddChild(map);
    return tab;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// ENTRY POINT
// ─────────────────────────────────────────────────────────────────────────────
std::shared_ptr<UltraCanvasUIElement>
UltraCanvasDemoApplication::CreateMindMapExamples() {
    auto main = std::make_shared<UltraCanvasContainer>("MindMapExamples");
    main->SetPadding(5, 5, 0, 5);

    main->layout.SetGrid()
            .SetGridColumns({
                    CSSLayout::GridTrackSize{.kind = CSSLayout::GridTrackSizeKind::Fr,
                                             .value = CSSLayout::Dimension::Fr(1)},
                    CSSLayout::GridTrackSize{.kind = CSSLayout::GridTrackSizeKind::Fixed,
                                             .value = CSSLayout::Dimension::Px(350)}})
            .SetGridRows({
                    CSSLayout::GridTrackSize{.kind = CSSLayout::GridTrackSizeKind::Fixed,
                                             .value = CSSLayout::Dimension::Px(34)},
                    CSSLayout::GridTrackSize{.kind = CSSLayout::GridTrackSizeKind::Fixed,
                                             .value = CSSLayout::Dimension::Px(44)},
                    CSSLayout::GridTrackSize{.kind = CSSLayout::GridTrackSizeKind::Fr,
                                             .value = CSSLayout::Dimension::Fr(1)}})
            .SetGridGap(6);

    auto title = std::make_shared<UltraCanvasLabel>("MindMapTitle");
    title->SetText("Mind Map");
    title->SetFontSize(18);
    title->SetFontWeight(FontWeight::Bold);
    title->SetTextColor(Color(80, 50, 140));
    main->AddChild(title);
    title->layoutItem.SetGridRowColSimplified(0, 0);

    auto subtitle = std::make_shared<UltraCanvasLabel>("MindMapSub");
    subtitle->SetText("Rooted topic tree on a pan/zoom canvas. Balanced, logic, org chart and "
                      "radial structures; per-branch colour cascade; collapse, in-place "
                      "editing and drag-to-reparent.");
    subtitle->SetFontSize(11);
    subtitle->SetWrap(TextWrap::WrapWord);
    main->AddChild(subtitle);
    subtitle->layoutItem.SetGridRowColSimplified(1, 0);

    auto statusLabel = std::make_shared<UltraCanvasLabel>("MindMapStatus");
    statusLabel->SetText("Click a topic to inspect it.");
    statusLabel->SetFontSize(10);
    statusLabel->SetBackgroundColor(Color(245, 245, 245));
    statusLabel->SetBorders(1.0f);
    statusLabel->SetPadding(6.0f);
    statusLabel->SetAlignment(TextAlignment::Center);
    main->AddChild(statusLabel);
    statusLabel->layoutItem.SetGridRowColSimplified(0, 1, 2, 1);

    auto tabs = std::make_shared<UltraCanvasTabbedContainer>("MindMapTabs");
    tabs->SetTabPosition(TabPosition::Top);
    tabs->SetTabStyle(TabStyle::Modern);
    tabs->SetTabMaxWidth(300);

    tabs->AddTab("Editable map",        MakeEditableTab(statusLabel));
    tabs->AddTab("Logic chart",         MakeLogicChartTab(statusLabel));
    tabs->AddTab("Radial",              MakeRadialTab(statusLabel));
    tabs->AddTab("Relationships",       MakeRelationshipsTab(statusLabel));
    tabs->AddTab("Themes & structures", MakeGalleryTab(statusLabel));

    main->AddChild(tabs);
    tabs->layoutItem.SetGridRowColSimplified(2, 0, 1, 2);
    return main;
}

} // namespace UltraCanvas
